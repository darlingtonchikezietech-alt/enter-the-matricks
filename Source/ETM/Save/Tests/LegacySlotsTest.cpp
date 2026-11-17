// T-0052: the calibration (ETM_Calibration) and control-scheme (ETM_Input) slots use the checked save format,
// so a damaged file falls back to the backup or to defaults instead of crashing the engine's loader.
// These slots have fixed names, so the test copies any real files aside and puts them back afterwards.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Save.OtherSlots; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Player/XRSeatedRigPawn.h"
#include "Input/RigPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace
{
	const EAutomationTestFlags ETMSlotTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	FString SlotFile(const FString& Slot) { return FPaths::ProjectSavedDir() / TEXT("SaveGames") / (Slot + TEXT(".sav")); }

	/** Moves a slot's real files (main and backup) out of the way for the test and restores them after. */
	struct FSlotGuard
	{
		TArray<FString> Files;
		explicit FSlotGuard(const FString& Slot)
		{
			Files = { SlotFile(Slot), SlotFile(Slot + TEXT("_bak")) };
			for (const FString& File : Files)
			{
				if (IFileManager::Get().FileExists(*File)) IFileManager::Get().Move(*(File + TEXT(".testguard")), *File);
			}
		}
		~FSlotGuard()
		{
			for (const FString& File : Files)
			{
				IFileManager::Get().Delete(*File);
				if (IFileManager::Get().FileExists(*(File + TEXT(".testguard")))) IFileManager::Get().Move(*File, *(File + TEXT(".testguard")));
			}
		}
	};

	struct FTestWorld
	{
		UWorld* World = nullptr;
		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ETMSlotTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true);
		}
		~FTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->SetBegunPlay(false);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
	};

	bool HasCheckedHeader(const FString& File)
	{
		TArray<uint8> Bytes;
		return FFileHelper::LoadFileToArray(Bytes, *File) && Bytes.Num() > 12
			&& Bytes[0] == 'E' && Bytes[1] == 'T' && Bytes[2] == 'M' && Bytes[3] == 'S';
	}

	void Damage(const FString& File, bool bFakeEngineHeader)
	{
		TArray<uint8> Garbage;
		for (int32 Index = 0; Index < 2048; ++Index) Garbage.Add(static_cast<uint8>((Index * 53 + 7) & 0xFF));
		if (bFakeEngineHeader) { Garbage[0] = 'G'; Garbage[1] = 'V'; Garbage[2] = 'A'; Garbage[3] = 'S'; }
		FFileHelper::SaveArrayToFile(Garbage, *File);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMCalibrationSlotTest, "ETM.Save.OtherSlots.CalibrationChecked", ETMSlotTestFlags)
bool FETMCalibrationSlotTest::RunTest(const FString&)
{
	FSlotGuard Guard(TEXT("ETM_Calibration"));
	FTestWorld W;

	AXRSeatedRigPawn* First = W.World->SpawnActor<AXRSeatedRigPawn>();
	First->SetAimOffset(true, FRotator(-42.f, 6.f, 0.f));
	TestTrue(TEXT("Calibration saves"), First->SaveCalibration());
	TestTrue(TEXT("Main copy is in the checked format"), HasCheckedHeader(SlotFile(TEXT("ETM_Calibration"))));
	TestTrue(TEXT("Backup copy written"), HasCheckedHeader(SlotFile(TEXT("ETM_Calibration_bak"))));

	AXRSeatedRigPawn* AfterRestart = W.World->SpawnActor<AXRSeatedRigPawn>(); // loads at BeginPlay
	TestTrue(TEXT("Offsets survive a restart"), AfterRestart->GetAimOffset(true).Equals(FRotator(-42.f, 6.f, 0.f), 0.01f));

	Damage(SlotFile(TEXT("ETM_Calibration")), false);
	AXRSeatedRigPawn* FromBackup = W.World->SpawnActor<AXRSeatedRigPawn>();
	TestTrue(TEXT("Damaged main copy: offsets come from the backup"), FromBackup->GetAimOffset(true).Equals(FRotator(-42.f, 6.f, 0.f), 0.01f));

	Damage(SlotFile(TEXT("ETM_Calibration")), true);
	Damage(SlotFile(TEXT("ETM_Calibration_bak")), true);
	AXRSeatedRigPawn* Defaults = W.World->SpawnActor<AXRSeatedRigPawn>();
	TestFalse(TEXT("Both copies damaged: load reports nothing usable"), Defaults->LoadCalibration());
	TestTrue(TEXT("... and the pawn runs on defaults, no crash"), Defaults->GetAimOffset(true).Equals(FRotator(-30.f, 0.f, 0.f), 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMInputSlotTest, "ETM.Save.OtherSlots.ControlSchemeChecked", ETMSlotTestFlags)
bool FETMInputSlotTest::RunTest(const FString&)
{
	FSlotGuard Guard(TEXT("ETM_Input"));
	FTestWorld W;

	ARigPlayerController* First = W.World->SpawnActor<ARigPlayerController>();
	TestEqual(TEXT("Default scheme"), First->GetControlScheme(), ERigControlScheme::GripVent);
	First->SetControlScheme(ERigControlScheme::ButtonVent, /*bSave*/ true);
	TestTrue(TEXT("Main copy is in the checked format"), HasCheckedHeader(SlotFile(TEXT("ETM_Input"))));

	ARigPlayerController* AfterRestart = W.World->SpawnActor<ARigPlayerController>(); // loads at BeginPlay
	TestEqual(TEXT("Scheme survives a restart"), AfterRestart->GetControlScheme(), ERigControlScheme::ButtonVent);

	Damage(SlotFile(TEXT("ETM_Input")), true);
	ARigPlayerController* FromBackup = W.World->SpawnActor<ARigPlayerController>();
	TestEqual(TEXT("Damaged main copy: scheme from the backup"), FromBackup->GetControlScheme(), ERigControlScheme::ButtonVent);

	Damage(SlotFile(TEXT("ETM_Input_bak")), true);
	ARigPlayerController* Defaults = W.World->SpawnActor<ARigPlayerController>();
	TestEqual(TEXT("Both copies damaged: default scheme, no crash"), Defaults->GetControlScheme(), ERigControlScheme::GripVent);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
