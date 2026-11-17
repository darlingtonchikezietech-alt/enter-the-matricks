// UETMSaveStore: reads and writes the settings, progress and results slots, and connects them to a level.
// Owned by T-0023. UETMSaveSubsystem owns one for the whole app; tests make their own with test slot names.
//
// In a level: BindToMission() saves every checkpoint the mission records, so it survives a crash or a
// headset power-off. After a restart the campaign flow (T-0031) loads GetSavedCheckpoint().MapName, starts
// the encounter, then calls ResumeInto(), which restores the checkpoint (boss checkpoints use the P-04
// preset, never the saved unwinnable state).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Save/SaveTypes.h"
#include "ETMSaveStore.generated.h"

class UMissionStateSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnETMSettingsChanged, const FETMSettings&, Settings);

UCLASS(BlueprintType)
class ETM_API UETMSaveStore : public UObject
{
	GENERATED_BODY()

public:
	/** Reads all three slots. Missing, damaged or old-version slots give defaults; nothing throws. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Save") void LoadAll();

	// ---- Settings ----
	UFUNCTION(BlueprintPure, Category = "ETM|Save") const FETMSettings& GetSettings() const { return Settings; }
	/** Stores and writes the settings, then applies them to any loaded world (haptics, flash, difficulty). */
	UFUNCTION(BlueprintCallable, Category = "ETM|Save") bool SetSettings(const FETMSettings& NewSettings);
	/** Pushes the current settings into a world's rig, mission and objective. Call after a level loads. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Save") void ApplySettings(UWorld* World) const;

	// ---- Checkpoint ----
	/** Saves each checkpoint the mission records, from now on. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Save") void BindToMission(UMissionStateSubsystem* Mission);
	UFUNCTION(BlueprintCallable, Category = "ETM|Save") bool SaveCheckpoint(const FMissionCheckpoint& Checkpoint, const FString& MapName);
	UFUNCTION(BlueprintPure, Category = "ETM|Save") bool HasSavedCheckpoint() const { return bHasCheckpoint; }
	UFUNCTION(BlueprintPure, Category = "ETM|Save") const FETMSavedCheckpoint& GetSavedCheckpoint() const { return SavedCheckpoint; }
	/** Restores the saved checkpoint into a running mission. Returns false if there is none. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Save") bool ResumeInto(UMissionStateSubsystem* Mission) const;
	/** Forget the checkpoint (level completed, or "new game"). */
	UFUNCTION(BlueprintCallable, Category = "ETM|Save") bool ClearCheckpoint();

	// ---- Results ----
	/** Stores a run. Returns true if it is a new best (completed runs only, compared by Score). */
	UFUNCTION(BlueprintCallable, Category = "ETM|Save") bool RecordResult(const FETMMissionResult& Result);
	UFUNCTION(BlueprintPure, Category = "ETM|Save") FETMLevelRecord GetLevelRecord(FName Level) const;

	/** Slot names, so tests never touch a player's real saves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Save") FString SettingsSlot = TEXT("ETM_Settings");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Save") FString ProgressSlot = TEXT("ETM_Progress");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Save") FString ResultsSlot = TEXT("ETM_Results");
	/** Write on a background thread so a checkpoint never stalls a frame on the headset. Tests turn it off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Save") bool bAsyncWrites = true;

	/** Assisted difficulty (bible section 6: "reduced incoming damage"). Prototype value: the bible gives no number. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Save") float AssistedDamageScale = 0.7f;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Save") FOnETMSettingsChanged OnSettingsChanged;

	/** Writes any save object to a slot in this store's checked format (main copy plus backup). */
	bool WriteSlot(class USaveGame* Save, const FString& Slot);

	/** Writes that failed since startup (disk full, permissions). Shown to QA; the game carries on. */
	UFUNCTION(BlueprintPure, Category = "ETM|Save") int32 GetFailedWrites() const { return FailedWrites; }

private:
	UFUNCTION() void HandleCheckpointRecorded(const FMissionCheckpoint& Checkpoint);
	bool Write(class USaveGame* Save, const FString& Slot);
	FString CurrentMapName() const;

	UPROPERTY(Transient) TWeakObjectPtr<UMissionStateSubsystem> BoundMission;
	FETMSettings Settings;
	FETMSavedCheckpoint SavedCheckpoint;
	bool bHasCheckpoint = false;
	TMap<FName, FETMLevelRecord> Levels;
	int32 FailedWrites = 0;
};
