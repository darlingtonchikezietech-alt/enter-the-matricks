// Automation tests for the save system: settings, checkpoints and results survive a restart; a boss checkpoint
// resumes into the P-04 preset; damaged or old saves fall back to defaults.
// A "restart" here is a brand-new UETMSaveStore reading the slot files back from disk.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Save; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Save/ETMSaveStore.h"
#include "Save/ETMSaveGames.h"
#include "Encounters/EncounterDirector.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Player/XRSeatedRigPawn.h"
#include "Weapons/Feedback/CannonFeedbackComponent.h"
#include "Combat/IntegrityComponent.h"
#include "Combat/DefenseComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/PlatformFileManager.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const EAutomationTestFlags ETMSaveTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	/** A store on test-only slots, writing synchronously. Deletes its slots when the test ends. */
	struct FTestStore
	{
		FString Prefix;
		FTestStore(const TCHAR* InPrefix) : Prefix(InPrefix) { Clean(); }
		~FTestStore() { Clean(); }

		UETMSaveStore* Make() const
		{
			UETMSaveStore* Store = NewObject<UETMSaveStore>();
			Store->SettingsSlot = Prefix + TEXT("_Settings");
			Store->ProgressSlot = Prefix + TEXT("_Progress");
			Store->ResultsSlot = Prefix + TEXT("_Results");
			Store->bAsyncWrites = false;
			Store->LoadAll();
			return Store;
		}
		void Clean() const
		{
			for (const TCHAR* Suffix : { TEXT("_Settings"), TEXT("_Progress"), TEXT("_Results") })
			{
				UGameplayStatics::DeleteGameInSlot(Prefix + Suffix, 0);
				UGameplayStatics::DeleteGameInSlot(Prefix + Suffix + TEXT("_bak"), 0);
			}
		}
	};

	struct FTestWorld
	{
		UWorld* World = nullptr;
		UMissionStateSubsystem* Mission = nullptr;
		AMissionObjective* Objective = nullptr;
		AActor* RigActor = nullptr;
		UIntegrityComponent* Rig = nullptr;
		AEncounterDirector* Director = nullptr;

		explicit FTestWorld(bool bWithRigStandIn = true)
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ETMSaveTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true); // no game mode, so mark it begun or spawned actors skip BeginPlay
			Mission = World->GetSubsystem<UMissionStateSubsystem>();
			Objective = World->SpawnActor<AMissionObjective>(FVector(600.f, 0.f, 0.f), FRotator::ZeroRotator);
			if (!bWithRigStandIn) return;

			RigActor = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(RigActor);
			RigActor->SetRootComponent(Root);
			Root->RegisterComponent();
			UDefenseComponent* Brace = NewObject<UDefenseComponent>(RigActor);
			Brace->RegisterComponent();
			Rig = NewObject<UIntegrityComponent>(RigActor);
			Rig->MaxIntegrity = 300.f;
			Rig->RegisterComponent();
			Rig->SetDefenseComponent(Brace);
			Rig->ResetToMax();
			Mission->RegisterRig(Rig, Brace);

			// Two phases: the opening, and the boss (index 1) that the saved checkpoint points at.
			Director = World->SpawnActor<AEncounterDirector>();
			Director->RigActor = RigActor;
			FEncounterPhase Opening;
			Opening.Name = TEXT("Opening");
			FEncounterPhase Boss;
			Boss.Name = TEXT("Gatebreaker");
			Boss.bCompleteWhenCleared = false;
			Director->Phases = { Opening, Boss };
		}

		~FTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->SetBegunPlay(false);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMSaveSettingsTest, "ETM.Save.Settings.SurviveRestart", ETMSaveTestFlags)
bool FETMSaveSettingsTest::RunTest(const FString&)
{
	FTestStore Slots(TEXT("ETMTest_SettingsRestart"));
	UETMSaveStore* First = Slots.Make();
	TestEqual(TEXT("Fresh install: Standard"), First->GetSettings().Difficulty, EETMDifficulty::Standard);
	TestEqual(TEXT("Fresh install: haptics on"), First->GetSettings().HapticStrength, 1.f);

	FETMSettings Wanted;
	Wanted.Difficulty = EETMDifficulty::Assisted;
	Wanted.HapticStrength = 0.3f;
	Wanted.bReducedFlash = true;
	Wanted.DialogueVolume = 0.9f;
	Wanted.EffectsVolume = 0.4f;
	Wanted.MusicVolume = 0.2f;
	Wanted.SubtitleScale = 1.5f;
	Wanted.bToggleVent = true;
	Wanted.bSwapHands = true;
	TestTrue(TEXT("Settings write"), First->SetSettings(Wanted));

	UETMSaveStore* AfterRestart = Slots.Make();
	const FETMSettings& Got = AfterRestart->GetSettings();
	TestEqual(TEXT("Difficulty survives"), Got.Difficulty, EETMDifficulty::Assisted);
	TestEqual(TEXT("Haptic strength survives"), Got.HapticStrength, 0.3f);
	TestTrue(TEXT("Reduced flash survives"), Got.bReducedFlash);
	TestEqual(TEXT("Effects volume survives"), Got.EffectsVolume, 0.4f);
	TestEqual(TEXT("Music volume survives"), Got.MusicVolume, 0.2f);
	TestEqual(TEXT("Subtitle size survives"), Got.SubtitleScale, 1.5f);
	TestTrue(TEXT("Toggle-to-vent survives"), Got.bToggleVent);
	TestTrue(TEXT("Hand swap survives"), Got.bSwapHands);

	// Out-of-range values (a hand-edited file) are pulled back into range.
	UETMSettingsSaveGame* Bad = NewObject<UETMSettingsSaveGame>();
	Bad->Settings.HapticStrength = 5.f;
	Bad->Settings.MusicVolume = -2.f;
	Bad->Settings.SubtitleScale = 10.f;
	AfterRestart->WriteSlot(Bad, Slots.Prefix + TEXT("_Settings"));
	const FETMSettings Clamped = Slots.Make()->GetSettings();
	TestEqual(TEXT("Haptics clamped"), Clamped.HapticStrength, 1.f);
	TestEqual(TEXT("Volume clamped"), Clamped.MusicVolume, 0.f);
	TestEqual(TEXT("Subtitle size clamped"), Clamped.SubtitleScale, 2.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMSaveDamagedTest, "ETM.Save.Robustness.DamagedAndOldSaves", ETMSaveTestFlags)
bool FETMSaveDamagedTest::RunTest(const FString&)
{
	FTestStore Slots(TEXT("ETMTest_Damaged"));
	const FString Dir = FPaths::ProjectSavedDir() / TEXT("SaveGames");
	const FString MainFile = Dir / (Slots.Prefix + TEXT("_Settings.sav"));
	const FString BackupFile = Dir / (Slots.Prefix + TEXT("_Settings_bak.sav"));
	TArray<uint8> Garbage;
	for (int32 Index = 0; Index < 4096; ++Index) Garbage.Add(static_cast<uint8>((Index * 37 + 11) & 0xFF));

	// A good save first, so there is a backup to fall back on.
	UETMSaveStore* Store = Slots.Make();
	FETMSettings Good;
	Good.Difficulty = EETMDifficulty::Veteran;
	Store->SetSettings(Good);
	TestTrue(TEXT("Backup copy written"), FPaths::FileExists(BackupFile));

	// 1. Main copy overwritten with garbage (power cut mid-write): the backup is used. No crash.
	TestTrue(TEXT("Damaged the main copy"), FFileHelper::SaveArrayToFile(Garbage, *MainFile));
	TestEqual(TEXT("Falls back to the backup copy"), Slots.Make()->GetSettings().Difficulty, EETMDifficulty::Veteran);

	// 2. Main copy cut short: caught by the length and checksum, backup used.
	TArray<uint8> Truncated;
	FFileHelper::LoadFileToArray(Truncated, *BackupFile);
	Truncated.SetNum(Truncated.Num() / 2);
	FFileHelper::SaveArrayToFile(Truncated, *MainFile);
	TestEqual(TEXT("Truncated main copy: backup used"), Slots.Make()->GetSettings().Difficulty, EETMDifficulty::Veteran);

	// 3. Both copies garbage, one with a raw engine save header (the case that crashed the engine): defaults.
	TArray<uint8> FakeEngineSave = Garbage;
	FakeEngineSave[0] = 'G'; FakeEngineSave[1] = 'V'; FakeEngineSave[2] = 'A'; FakeEngineSave[3] = 'S';
	FFileHelper::SaveArrayToFile(FakeEngineSave, *MainFile);
	FFileHelper::SaveArrayToFile(Garbage, *BackupFile);
	TestEqual(TEXT("Both copies damaged: defaults, and no crash"), Slots.Make()->GetSettings().Difficulty, EETMDifficulty::Standard);

	// 4. A well-formed save from another version: ignored rather than misread.
	UETMProgressSaveGame* Old = NewObject<UETMProgressSaveGame>();
	Old->Version = 99;
	Old->bHasCheckpoint = true;
	Old->Saved.Checkpoint.bValid = true;
	Store->WriteSlot(Old, Slots.Prefix + TEXT("_Progress"));
	TestFalse(TEXT("An other-version checkpoint is ignored"), Slots.Make()->HasSavedCheckpoint());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMSaveBossCheckpointTest, "ETM.Save.Checkpoint.BossPresetAfterRestart", ETMSaveTestFlags)
bool FETMSaveBossCheckpointTest::RunTest(const FString&)
{
	FTestStore Slots(TEXT("ETMTest_BossCheckpoint"));

	// Session 1: reach the boss badly hurt (rig 40, objective 100); the checkpoint saves itself.
	{
		FTestWorld W;
		UETMSaveStore* Store = Slots.Make();
		Store->BindToMission(W.Mission);
		TestTrue(TEXT("Encounter starts"), W.Director->StartEncounter(9));
		TestTrue(TEXT("Start checkpoint saved automatically"), Slots.Make()->HasSavedCheckpoint());
		W.Rig->ApplyIntegrityDamage(260.f);
		W.Objective->Integrity->ApplyIntegrityDamage(400.f);
		W.Mission->RecordCheckpoint(TEXT("Boss"), 1, 435.f, /*bBoss*/ true, 9);
	}

	// Session 2, after a restart: the saved checkpoint is there with everything the bible asks for.
	UETMSaveStore* Store = Slots.Make();
	TestTrue(TEXT("Checkpoint survives restart"), Store->HasSavedCheckpoint());
	const FETMSavedCheckpoint& Saved = Store->GetSavedCheckpoint();
	TestEqual(TEXT("Phase"), Saved.Checkpoint.PhaseIndex, 1);
	TestEqual(TEXT("Encounter seed"), Saved.Checkpoint.EncounterSeed, 9);
	TestTrue(TEXT("Boss flag"), Saved.Checkpoint.bBossCheckpoint);
	TestEqual(TEXT("Health preset inputs: rig"), Saved.Checkpoint.RigIntegrity, 40.f);
	TestEqual(TEXT("Health preset inputs: objective"), Saved.Checkpoint.ObjectiveIntegrity, 100.f);
	TestEqual(TEXT("Difficulty"), Saved.Difficulty, EETMDifficulty::Standard);
	TestFalse(TEXT("Map name recorded (may be empty for a test world)"), Saved.MapName.Contains(TEXT("/")));

	{
		FTestWorld W;
		TestTrue(TEXT("Encounter starts"), W.Director->StartEncounter(Saved.Checkpoint.EncounterSeed));
		TestTrue(TEXT("Resume"), Store->ResumeInto(W.Mission));
		TestEqual(TEXT("Director jumps to the boss phase"), W.Director->GetPhaseIndex(), 1);
		TestEqual(TEXT("P-04: rig restored to 300, not the saved 40"), W.Rig->GetCurrent(), 300.f);
		TestEqual(TEXT("P-04 Standard: objective raised to 60%, not the saved 100"), W.Objective->Integrity->GetCurrent(), 300.f);
		TestEqual(TEXT("Resuming is not a retry"), W.Mission->GetRetryCount(), 0);
		TestEqual(TEXT("Mission running"), W.Mission->GetState(), EMissionState::Running);
	}

	// The same checkpoint saved on Assisted resumes with the 80% floor.
	FETMSettings Assisted;
	Assisted.Difficulty = EETMDifficulty::Assisted;
	Store->SetSettings(Assisted);
	FMissionCheckpoint Copy = Saved.Checkpoint;
	Store->SaveCheckpoint(Copy, Saved.MapName);
	{
		FTestWorld W;
		W.Director->StartEncounter(9);
		Slots.Make()->ResumeInto(W.Mission);
		TestEqual(TEXT("P-04 Assisted: objective raised to 80%"), W.Objective->Integrity->GetCurrent(), 400.f);
	}

	// A normal checkpoint resumes the saved values exactly.
	FMissionCheckpoint Midpoint = Saved.Checkpoint;
	Midpoint.Name = TEXT("Midpoint");
	Midpoint.bBossCheckpoint = false;
	Midpoint.RigIntegrity = 260.f;
	Midpoint.ObjectiveIntegrity = 410.f;
	Store->SaveCheckpoint(Midpoint, Saved.MapName);
	{
		FTestWorld W;
		W.Director->StartEncounter(9);
		Slots.Make()->ResumeInto(W.Mission);
		TestEqual(TEXT("Midpoint rig as saved"), W.Rig->GetCurrent(), 260.f);
		TestEqual(TEXT("Midpoint objective as saved"), W.Objective->Integrity->GetCurrent(), 410.f);
	}

	// Finishing the level clears it.
	Store->ClearCheckpoint();
	TestFalse(TEXT("Cleared checkpoint stays cleared after restart"), Slots.Make()->HasSavedCheckpoint());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMSaveResultsTest, "ETM.Save.Results.BestAndLast", ETMSaveTestFlags)
bool FETMSaveResultsTest::RunTest(const FString&)
{
	FTestStore Slots(TEXT("ETMTest_Results"));
	UETMSaveStore* Store = Slots.Make();
	const FName Level(TEXT("L1_Breach"));

	FETMMissionResult Failed;
	Failed.Level = Level;
	Failed.Score = 5000;
	TestFalse(TEXT("A failed run is never a best"), Store->RecordResult(Failed));

	FETMMissionResult Run;
	Run.Level = Level;
	Run.bCompleted = true;
	Run.DurationSeconds = 612.f;
	Run.ShotsFired = 400;
	Run.ShotsHit = 300;
	Run.WeakPointHits = 90;
	Run.DamagePrevented = 210.f;
	Run.Score = 1000;
	TestTrue(TEXT("First completion is a best"), Store->RecordResult(Run));
	Run.Score = 800;
	TestFalse(TEXT("A lower score is not"), Store->RecordResult(Run));
	Run.Score = 1200;
	TestTrue(TEXT("A higher score is"), Store->RecordResult(Run));

	const FETMLevelRecord Record = Slots.Make()->GetLevelRecord(Level);
	TestTrue(TEXT("Results survive restart"), Record.bHasBest);
	TestEqual(TEXT("Best score"), Record.Best.Score, 1200);
	TestEqual(TEXT("Last run kept too"), Record.Last.Score, 1200);
	TestEqual(TEXT("Completions counted"), Record.Completions, 3);
	TestEqual(TEXT("Weak-point hits kept (P-30 stats)"), Record.Best.WeakPointHits, 90);
	TestEqual(TEXT("Damage prevented kept (P-30)"), Record.Best.DamagePrevented, 210.f);
	TestEqual(TEXT("Accuracy"), Record.Best.GetAccuracy(), 0.75f);
	TestFalse(TEXT("Other levels untouched"), Slots.Make()->GetLevelRecord(TEXT("L2_LastSignal")).bHasBest);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMSaveApplyTest, "ETM.Save.Settings.AppliedToTheWorld", ETMSaveTestFlags)
bool FETMSaveApplyTest::RunTest(const FString&)
{
	FTestStore Slots(TEXT("ETMTest_Apply"));
	FTestWorld W(/*bWithRigStandIn*/ false);
	AXRSeatedRigPawn* Pawn = W.World->SpawnActor<AXRSeatedRigPawn>();

	UETMSaveStore* Store = Slots.Make();
	FETMSettings Settings;
	Settings.Difficulty = EETMDifficulty::Assisted;
	Settings.HapticStrength = 0.25f;
	Settings.bReducedFlash = true;
	Store->SetSettings(Settings);
	Store->ApplySettings(W.World);
	TestEqual(TEXT("Left haptics follow the setting"), Pawn->LeftFeedback->HapticStrength, 0.25f);
	TestEqual(TEXT("Right haptics follow the setting"), Pawn->RightFeedback->HapticStrength, 0.25f);
	TestTrue(TEXT("Reduced flash applied"), Pawn->LeftFeedback->bReducedFlash && Pawn->RightFeedback->bReducedFlash);
	TestEqual(TEXT("Assisted: boss preset floor 80%"), W.Mission->BossObjectiveFloor, 0.8f);
	TestTrue(TEXT("Assisted: reduced damage to the rig"), Pawn->RigIntegrity->IncomingDamageScale < 1.f);
	TestTrue(TEXT("Assisted: reduced damage to the objective"), W.Objective->Integrity->IncomingDamageScale < 1.f);

	Settings.Difficulty = EETMDifficulty::Standard;
	Settings.HapticStrength = 0.f;
	Store->SetSettings(Settings);
	Store->ApplySettings(W.World);
	TestEqual(TEXT("Haptics off"), Pawn->LeftFeedback->HapticStrength, 0.f);
	TestEqual(TEXT("Standard: 60% floor"), W.Mission->BossObjectiveFloor, 0.6f);
	TestEqual(TEXT("Standard: full rig damage"), Pawn->RigIntegrity->IncomingDamageScale, 1.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
