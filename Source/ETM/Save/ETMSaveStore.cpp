#include "Save/ETMSaveStore.h"
#include "Save/ETMSaveGames.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Combat/IntegrityComponent.h"
#include "Player/XRSeatedRigPawn.h"
#include "Weapons/Feedback/CannonFeedbackComponent.h"
#include "ETM.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Async/Async.h"
#include "Misc/Crc.h"
#include "Misc/ScopeLock.h"

namespace
{
	// Every slot is wrapped: an ETMS marker, the payload length, a CRC32 of the payload, then the engine's
	// own save data. The engine asserts (crashes) on some damaged save files, so nothing reaches its parser
	// unless the wrapper checks out. Each write also goes to "<slot>_bak", so a write cut short by a power
	// loss leaves the previous copy to fall back on.
	constexpr uint32 WrapperMagic = 0x534D5445; // "ETMS" little-endian
	constexpr int32 HeaderBytes = 12;

	FString BackupOf(const FString& Slot) { return Slot + TEXT("_bak"); }

	bool Unwrap(const TArray<uint8>& Wrapped, TArray<uint8>& OutPayload)
	{
		if (Wrapped.Num() < HeaderBytes) return false;
		uint32 Magic = 0, Length = 0, Crc = 0;
		FMemory::Memcpy(&Magic, Wrapped.GetData(), 4);
		FMemory::Memcpy(&Length, Wrapped.GetData() + 4, 4);
		FMemory::Memcpy(&Crc, Wrapped.GetData() + 8, 4);
		if (Magic != WrapperMagic || static_cast<int64>(Length) != Wrapped.Num() - HeaderBytes) return false;
		if (FCrc::MemCrc32(Wrapped.GetData() + HeaderBytes, Length) != Crc) return false;
		OutPayload.Reset(Length);
		OutPayload.Append(Wrapped.GetData() + HeaderBytes, Length);
		return true;
	}

	TArray<uint8> Wrap(const TArray<uint8>& Payload)
	{
		TArray<uint8> Wrapped;
		Wrapped.SetNumUninitialized(HeaderBytes + Payload.Num());
		const uint32 Magic = WrapperMagic;
		const uint32 Length = Payload.Num();
		const uint32 Crc = FCrc::MemCrc32(Payload.GetData(), Payload.Num());
		FMemory::Memcpy(Wrapped.GetData(), &Magic, 4);
		FMemory::Memcpy(Wrapped.GetData() + 4, &Length, 4);
		FMemory::Memcpy(Wrapped.GetData() + 8, &Crc, 4);
		FMemory::Memcpy(Wrapped.GetData() + HeaderBytes, Payload.GetData(), Payload.Num());
		return Wrapped;
	}

	/** One copy, header and CRC checked, then parsed. Null if missing or damaged. */
	USaveGame* LoadOneRaw(const FString& Slot)
	{
		TArray<uint8> Wrapped, Payload;
		if (!UGameplayStatics::DoesSaveGameExist(Slot, 0) || !UGameplayStatics::LoadDataFromSlot(Wrapped, Slot, 0)) return nullptr;
		if (!Unwrap(Wrapped, Payload))
		{
			UE_LOG(LogETM, Warning, TEXT("Save slot '%s' is damaged (checksum)."), *Slot);
			return nullptr;
		}
		return UGameplayStatics::LoadGameFromMemory(Payload);
	}

	/** One copy, checked. Null if missing, damaged, the wrong class or the wrong version. */
	template <typename T>
	T* LoadOne(const FString& Slot)
	{
		if (!UGameplayStatics::DoesSaveGameExist(Slot, 0)) return nullptr;
		T* Loaded = Cast<T>(LoadOneRaw(Slot));
		if (!Loaded)
		{
			UE_LOG(LogETM, Warning, TEXT("Save slot '%s' is unreadable."), *Slot);
			return nullptr;
		}
		if (Loaded->Version != T::CurrentVersion)
		{
			UE_LOG(LogETM, Warning, TEXT("Save slot '%s' is version %d, expected %d."), *Slot, Loaded->Version, T::CurrentVersion);
			return nullptr;
		}
		return Loaded;
	}

	/** The slot, else its backup, else null (defaults). */
	template <typename T>
	T* LoadSlot(const FString& Slot)
	{
		if (T* Main = LoadOne<T>(Slot)) return Main;
		if (T* Backup = LoadOne<T>(BackupOf(Slot)))
		{
			UE_LOG(LogETM, Warning, TEXT("Save slot '%s' restored from its backup."), *Slot);
			return Backup;
		}
		return nullptr;
	}

	/** Writes main then backup. Serialised across threads; a write older than the newest queued one is dropped. */
	FCriticalSection WriteLock;
	TMap<FString, uint64> LatestWrite;

	bool WriteWrapped(const TArray<uint8>& Wrapped, const FString& Slot, uint64 Sequence)
	{
		FScopeLock Lock(&WriteLock);
		if (LatestWrite.FindRef(Slot) != Sequence) return true; // superseded by a newer save of the same slot
		const bool bMain = UGameplayStatics::SaveDataToSlot(Wrapped, Slot, 0);
		const bool bBackup = UGameplayStatics::SaveDataToSlot(Wrapped, BackupOf(Slot), 0);
		return bMain && bBackup;
	}
}

void UETMSaveStore::LoadAll()
{
	Settings = FETMSettings();
	if (const UETMSettingsSaveGame* Saved = LoadSlot<UETMSettingsSaveGame>(SettingsSlot))
	{
		Settings = Saved->Settings;
		Settings.Sanitize();
	}

	bHasCheckpoint = false;
	SavedCheckpoint = FETMSavedCheckpoint();
	if (const UETMProgressSaveGame* Saved = LoadSlot<UETMProgressSaveGame>(ProgressSlot))
	{
		bHasCheckpoint = Saved->bHasCheckpoint && Saved->Saved.Checkpoint.bValid;
		if (bHasCheckpoint) SavedCheckpoint = Saved->Saved;
	}

	Levels.Reset();
	if (const UETMResultsSaveGame* Saved = LoadSlot<UETMResultsSaveGame>(ResultsSlot))
	{
		Levels = Saved->Levels;
	}
}

bool UETMSaveStore::WriteSlot(USaveGame* Save, const FString& Slot)
{
	return Write(Save, Slot);
}

bool UETMSaveStore::SaveChecked(USaveGame* Save, const FString& Slot)
{
	TArray<uint8> Payload;
	if (!Save || !UGameplayStatics::SaveGameToMemory(Save, Payload)) return false;
	uint64 Sequence = 0;
	{
		FScopeLock Lock(&WriteLock);
		Sequence = ++LatestWrite.FindOrAdd(Slot);
	}
	return WriteWrapped(Wrap(Payload), Slot, Sequence);
}

USaveGame* UETMSaveStore::LoadChecked(const FString& Slot)
{
	if (USaveGame* Main = LoadOneRaw(Slot)) return Main;
	if (USaveGame* Backup = LoadOneRaw(BackupOf(Slot)))
	{
		UE_LOG(LogETM, Warning, TEXT("Save slot '%s' restored from its backup."), *Slot);
		return Backup;
	}
	return nullptr;
}

bool UETMSaveStore::Write(USaveGame* Save, const FString& Slot)
{
	// Serialise on the game thread (UObjects), write on a worker (file I/O).
	TArray<uint8> Payload;
	if (!Save || !UGameplayStatics::SaveGameToMemory(Save, Payload))
	{
		++FailedWrites;
		return false;
	}
	TArray<uint8> Wrapped = Wrap(Payload);
	uint64 Sequence = 0;
	{
		FScopeLock Lock(&WriteLock);
		Sequence = ++LatestWrite.FindOrAdd(Slot);
	}

	if (bAsyncWrites)
	{
		TWeakObjectPtr<UETMSaveStore> WeakThis(this);
		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [Wrapped = MoveTemp(Wrapped), Slot, Sequence, WeakThis]()
		{
			if (!WriteWrapped(Wrapped, Slot, Sequence))
			{
				AsyncTask(ENamedThreads::GameThread, [WeakThis, Slot]()
				{
					if (WeakThis.IsValid()) ++WeakThis->FailedWrites;
					UE_LOG(LogETM, Warning, TEXT("Could not write save slot '%s'."), *Slot);
				});
			}
		});
		return true;
	}
	const bool bOk = WriteWrapped(Wrapped, Slot, Sequence);
	if (!bOk)
	{
		++FailedWrites;
		UE_LOG(LogETM, Warning, TEXT("Could not write save slot '%s'."), *Slot);
	}
	return bOk;
}

// ---------------------------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------------------------

bool UETMSaveStore::SetSettings(const FETMSettings& NewSettings)
{
	Settings = NewSettings;
	Settings.Sanitize();
	UETMSettingsSaveGame* Save = NewObject<UETMSettingsSaveGame>();
	Save->Settings = Settings;
	const bool bOk = Write(Save, SettingsSlot);
	OnSettingsChanged.Broadcast(Settings);
	return bOk;
}

void UETMSaveStore::ApplySettings(UWorld* World) const
{
	if (!World) return;

	// Haptics and reduced flash on every rig's shot feedback.
	for (TActorIterator<AXRSeatedRigPawn> It(World); It; ++It)
	{
		for (UCannonFeedbackComponent* Feedback : { It->LeftFeedback.Get(), It->RightFeedback.Get() })
		{
			if (!Feedback) continue;
			Feedback->HapticStrength = Settings.HapticStrength;
			Feedback->bReducedFlash = Settings.bReducedFlash;
		}
		// Assisted: reduced incoming damage on the rig (bible section 6).
		if (It->RigIntegrity) It->RigIntegrity->IncomingDamageScale = Settings.Difficulty == EETMDifficulty::Assisted ? AssistedDamageScale : 1.f;
	}

	// Difficulty: the P-04 boss preset floor (Standard 60%, Assisted 80%) and objective damage.
	if (UMissionStateSubsystem* Mission = World->GetSubsystem<UMissionStateSubsystem>())
	{
		Mission->BossObjectiveFloor = Settings.Difficulty == EETMDifficulty::Assisted ? 0.8f : 0.6f;
		if (AMissionObjective* Objective = Mission->GetObjective())
		{
			Objective->Integrity->IncomingDamageScale = Settings.Difficulty == EETMDifficulty::Assisted ? AssistedDamageScale : 1.f;
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Checkpoint
// ---------------------------------------------------------------------------------------------

FString UETMSaveStore::CurrentMapName() const
{
	const UWorld* World = BoundMission.IsValid() ? BoundMission->GetWorld() : nullptr;
	return World ? UGameplayStatics::GetCurrentLevelName(World, /*bRemovePrefixString*/ true) : FString();
}

void UETMSaveStore::BindToMission(UMissionStateSubsystem* Mission)
{
	if (UMissionStateSubsystem* Old = BoundMission.Get())
	{
		Old->OnCheckpointRecorded.RemoveDynamic(this, &UETMSaveStore::HandleCheckpointRecorded);
	}
	BoundMission = Mission;
	if (Mission)
	{
		Mission->OnCheckpointRecorded.AddUniqueDynamic(this, &UETMSaveStore::HandleCheckpointRecorded);
	}
}

void UETMSaveStore::HandleCheckpointRecorded(const FMissionCheckpoint& Checkpoint)
{
	SaveCheckpoint(Checkpoint, CurrentMapName());
}

bool UETMSaveStore::SaveCheckpoint(const FMissionCheckpoint& Checkpoint, const FString& MapName)
{
	if (!Checkpoint.bValid) return false;
	SavedCheckpoint.MapName = MapName;
	SavedCheckpoint.Checkpoint = Checkpoint;
	SavedCheckpoint.Difficulty = Settings.Difficulty;
	SavedCheckpoint.SavedAt = FDateTime::UtcNow();
	bHasCheckpoint = true;

	UETMProgressSaveGame* Save = NewObject<UETMProgressSaveGame>();
	Save->bHasCheckpoint = true;
	Save->Saved = SavedCheckpoint;
	return Write(Save, ProgressSlot);
}

bool UETMSaveStore::ResumeInto(UMissionStateSubsystem* Mission) const
{
	if (!Mission || !bHasCheckpoint) return false;
	// The difficulty in force when it was saved decides the boss preset floor.
	Mission->BossObjectiveFloor = SavedCheckpoint.Difficulty == EETMDifficulty::Assisted ? 0.8f : 0.6f;
	return Mission->ResumeFromCheckpoint(SavedCheckpoint.Checkpoint);
}

bool UETMSaveStore::ClearCheckpoint()
{
	bHasCheckpoint = false;
	SavedCheckpoint = FETMSavedCheckpoint();
	UETMProgressSaveGame* Save = NewObject<UETMProgressSaveGame>();
	return Write(Save, ProgressSlot);
}

// ---------------------------------------------------------------------------------------------
// Results
// ---------------------------------------------------------------------------------------------

bool UETMSaveStore::RecordResult(const FETMMissionResult& Result)
{
	FETMLevelRecord& Record = Levels.FindOrAdd(Result.Level);
	Record.Last = Result;
	if (Record.Last.When.GetTicks() == 0) Record.Last.When = FDateTime::UtcNow();

	bool bNewBest = false;
	if (Result.bCompleted)
	{
		++Record.Completions;
		if (!Record.bHasBest || Result.Score > Record.Best.Score)
		{
			Record.Best = Record.Last;
			Record.bHasBest = true;
			bNewBest = true;
		}
	}

	UETMResultsSaveGame* Save = NewObject<UETMResultsSaveGame>();
	Save->Levels = Levels;
	Write(Save, ResultsSlot);
	return bNewBest;
}

FETMLevelRecord UETMSaveStore::GetLevelRecord(FName Level) const
{
	const FETMLevelRecord* Record = Levels.Find(Level);
	return Record ? *Record : FETMLevelRecord();
}
