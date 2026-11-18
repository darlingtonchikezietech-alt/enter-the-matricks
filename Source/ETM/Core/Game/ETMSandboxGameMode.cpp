#include "Core/Game/ETMSandboxGameMode.h"
#include "Core/Flow/CampaignFlow.h"
#include "Core/Flow/CampaignFlowSubsystem.h"
#include "Core/Flow/ETMMenuPanel.h"
#include "Encounters/EncounterDirector.h"
#include "Enemies/Archetypes/NeedleDrone.h"
#include "Enemies/Archetypes/Shardling.h"
#include "Enemies/Archetypes/Siphon.h"
#include "Input/RigPlayerController.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Player/XRSeatedRigPawn.h"
#include "Save/ETMSaveStore.h"
#include "ETM.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "GameFramework/PlayerStart.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "ETMSandbox"

namespace
{
	const float LaneYaw[] = { -35.f, 0.f, 35.f };

	FEncounterSpawnGroup Group(TSubclassOf<AEnemyBase> Class, int32 Count, int32 Entrance, float Interval, float Delay)
	{
		FEncounterSpawnGroup G;
		G.EnemyClass = Class;
		G.Count = Count;
		G.EntranceIndex = Entrance;
		G.Interval = Interval;
		G.StartDelay = Delay;
		return G;
	}

	FEncounterPhase Phase(const TCHAR* Name, float Slot, int32 MaxActive, TArray<FEncounterSpawnGroup> Groups, bool bStartCheckpoint)
	{
		FEncounterPhase P;
		P.Name = Name;
		P.SlotSeconds = Slot;
		P.MaxActive = MaxActive;
		P.Groups = MoveTemp(Groups);
		P.bCompleteWhenCleared = true;
		P.bCheckpointOnStart = bStartCheckpoint;
		if (bStartCheckpoint) P.CheckpointName = TEXT("Start");
		return P;
	}

	FString ResultLines(const FETMMissionResult& R)
	{
		return FString::Printf(TEXT("Time %s    Accuracy %.0f%%\nWeak-point hits %d    Damage prevented %.0f\nRetries %d    Score %d"),
			*ETMFlow::FormatTime(R.DurationSeconds), R.GetAccuracy() * 100.f, R.WeakPointHits, R.DamagePrevented, R.Retries, R.Score);
	}
}

AETMSandboxGameMode::AETMSandboxGameMode()
{
	DefaultPawnClass = AXRSeatedRigPawn::StaticClass();
	PlayerControllerClass = ARigPlayerController::StaticClass();
	// Ticks only to apply the fade to the light, which must also work while the world is paused.
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
}

bool AETMSandboxGameMode::ShouldBuildSandbox() const
{
	const UWorld* World = GetWorld();
	if (!bBuildSandbox || !World) return false;
	if (!SandboxMaps.Contains(UGameplayStatics::GetCurrentLevelName(World, /*bRemovePrefixString*/ true))) return false;
	// A real arena already has an objective. (Checked without a one-pass loop: Clang rejects those on Android.)
	TActorIterator<AMissionObjective> FirstObjective(GetWorld());
	return !FirstObjective;
}

void AETMSandboxGameMode::SetFlow(UETMCampaignFlow* InFlow)
{
	if (Flow == InFlow) return;
	if (Flow) Flow->OnStageChanged.RemoveAll(this);
	Flow = InFlow;
	if (Flow) Flow->OnStageChanged.AddUniqueDynamic(this, &AETMSandboxGameMode::HandleStageChanged);
}

void AETMSandboxGameMode::StartPlay()
{
	Super::StartPlay();
	SetFlow(UETMCampaignFlowSubsystem::FindFlow(this));
	// Local players have been spawned by now; make sure each rig counts for the mission.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		RegisterRig(It->Get());
	}
	if (ShouldBuildSandbox())
	{
		BuildSandbox();
	}
	else if (HasArena())
	{
		SetupArena();
	}
	// A transition was waiting for this world: start the fade-in.
	if (Flow) Flow->NotifyWorldReady(GetWorld());
}

void AETMSandboxGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	SetFlow(nullptr); // the flow outlives this world
	Super::EndPlay(Reason);
}

void AETMSandboxGameMode::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);
	if (bBuilt && NewPlayer) PlaceRigOnRange(NewPlayer->GetPawn());
	RegisterRig(NewPlayer);
}

void AETMSandboxGameMode::RegisterRig(AController* Controller)
{
	AXRSeatedRigPawn* Rig = Controller ? Cast<AXRSeatedRigPawn>(Controller->GetPawn()) : nullptr;
	UMissionStateSubsystem* Mission = GetWorld() ? GetWorld()->GetSubsystem<UMissionStateSubsystem>() : nullptr;
	if (!Rig || !Mission) return;
	Mission->RegisterRig(Rig->RigIntegrity, Rig->Defense);
	if (Director && !Director->RigActor) Director->RigActor = Rig;
}

APawn* AETMSandboxGameMode::FindRigPawn() const
{
	if (const APlayerController* Player = GetWorld()->GetFirstPlayerController())
	{
		if (APawn* Pawn = Player->GetPawn()) return Pawn;
	}
	TActorIterator<AXRSeatedRigPawn> First(GetWorld());
	return First ? *First : nullptr;
}

void AETMSandboxGameMode::PlaceRigOnRange(APawn* Rig) const
{
	if (!Rig) return;
	if (!Rig->GetActorLocation().Equals(RigMount.GetLocation(), 1.f) || !Rig->GetActorRotation().Equals(RigMount.Rotator(), 0.1f))
	{
		UE_LOG(LogETM, Display, TEXT("Sandbox: rig moved from %s to its mount at %s."), *Rig->GetActorLocation().ToCompactString(), *RigMount.GetLocation().ToCompactString());
	}
	Rig->SetActorLocationAndRotation(RigMount.GetLocation(), RigMount.Rotator(), false, nullptr, ETeleportType::TeleportPhysics);
}

float AETMSandboxGameMode::GetCurrentLightIntensity() const
{
	return LightIntensity * (1.f - (Flow ? Flow->GetFadeAlpha() : 0.f));
}

void AETMSandboxGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (Sun) Sun->GetLightComponent()->SetIntensity(GetCurrentLightIntensity());
}

// ---------------------------------------------------------------------------------------------
// The range
// ---------------------------------------------------------------------------------------------

template <typename T>
T* AETMSandboxGameMode::SpawnSandboxActor(const FVector& Location, const FRotator& Rotation)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	T* Actor = GetWorld()->SpawnActor<T>(T::StaticClass(), Location, Rotation, Params);
	if (Actor) SandboxActors.Add(Actor);
	return Actor;
}

AActor* AETMSandboxGameMode::SpawnMesh(const TCHAR* Shape, const FVector& Location, const FVector& SizeCm, const FRotator& Rotation)
{
	AStaticMeshActor* Actor = SpawnSandboxActor<AStaticMeshActor>(Location, Rotation);
	if (!Actor) return nullptr;
	UStaticMeshComponent* Mesh = Actor->GetStaticMeshComponent();
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("/Engine/BasicShapes/%s.%s"), Shape, Shape)));
	Mesh->SetWorldScale3D(SizeCm / 100.f);
	Mesh->SetCastShadow(false); // no real-time shadows in the Quest baseline (bible section 14)
	return Actor;
}

void AETMSandboxGameMode::BuildSandbox()
{
	if (bBuilt || !GetWorld()) return;
	bBuilt = true;
	const EETMFlowStage Stage = Flow ? Flow->GetStageToBuild() : EETMFlowStage::Practice;
	UE_LOG(LogETM, Display, TEXT("Building the sandbox range for flow stage %d (T-0053, T-0031)."), static_cast<int32>(Stage));
	RigMount = FTransform(FVector(0.f, 0.f, RigBaseHeight));
	PlaceRigOnRange(FindRigPawn());
	BuildRange();

	switch (Stage)
	{
	case EETMFlowStage::Level:
	{
		BuildObjective();
		StartEncounter(MakeLevelPhases(Flow->GetLevelIndex()));
		if (Director) Director->OnEncounterComplete.AddUniqueDynamic(this, &AETMSandboxGameMode::HandleEncounterComplete);
		Flow->BeginLevel(GetWorld(), Cast<AXRSeatedRigPawn>(FindRigPawn()));
		break;
	}
	case EETMFlowStage::Title:
		ShowPanel(EETMFlowStage::Title);
		break;
	default:
	{
		// The open-ended practice range: Shardlings from the left and centre, drones from the right.
		BuildObjective();
		FEncounterPhase Practice = Phase(TEXT("Practice"), 3600.f, 6, {
			Group(AShardling::StaticClass(), 300, 0, 3.f, 2.f),
			Group(AShardling::StaticClass(), 300, 1, 4.5f, 4.f),
			Group(ANeedleDrone::StaticClass(), 150, 2, 6.f, 6.f),
			Group(ANeedleDrone::StaticClass(), 150, 0, 8.f, 10.f),
		}, false);
		Practice.bCompleteWhenCleared = false;
		StartEncounter({ Practice });
		if (Flow) ShowPanel(EETMFlowStage::Practice);
		break;
	}
	}
}

bool AETMSandboxGameMode::HasArena() const
{
	if (!GetWorld()) return false;
	TActorIterator<AMissionObjective> FirstObjective(GetWorld());
	TActorIterator<AEncounterDirector> FirstDirector(GetWorld());
	return FirstObjective && FirstDirector;
}

void AETMSandboxGameMode::SetupArena()
{
	if (bBuilt || !GetWorld() || !HasArena()) return;
	bBuilt = true;
	bArena = true;
	const EETMFlowStage Stage = Flow ? Flow->GetStageToBuild() : EETMFlowStage::Practice;
	UE_LOG(LogETM, Display, TEXT("Playing arena %s for flow stage %d (T-0020)."), *UGameplayStatics::GetCurrentLevelName(GetWorld()), static_cast<int32>(Stage));

	TActorIterator<AMissionObjective> FirstObjective(GetWorld());
	TActorIterator<AEncounterDirector> FirstDirector(GetWorld());
	Objective = *FirstObjective;
	Director = *FirstDirector;

	// The map's first directional light does the fades, from the intensity the map gave it.
	TActorIterator<ADirectionalLight> FirstLight(GetWorld());
	if (FirstLight)
	{
		Sun = *FirstLight;
		LightIntensity = Sun->GetLightComponent()->Intensity;
		Sun->GetLightComponent()->SetIntensity(GetCurrentLightIntensity());
	}

	// The rig sits exactly on the map's player start (its base, not a capsule centre).
	TActorIterator<APlayerStart> FirstStart(GetWorld());
	RigMount = FirstStart ? FTransform(FRotator(0.f, FirstStart->GetActorRotation().Yaw, 0.f), FirstStart->GetActorLocation())
		: FTransform(FVector(0.f, 0.f, RigBaseHeight));
	PlaceRigOnRange(FindRigPawn());
	Director->RigActor = FindRigPawn();

	if (Stage == EETMFlowStage::Title)
	{
		ShowPanel(EETMFlowStage::Title);
		return;
	}
	// The map's own schedule, or the stand-in waves until it has one (T-0026, T-0029).
	const int32 LevelIndex = Flow ? Flow->GetLevelIndex() : 0;
	StartEncounter(Director->Phases.Num() > 0 ? Director->Phases : MakeLevelPhases(LevelIndex));
	if (Stage == EETMFlowStage::Level && Flow)
	{
		Director->OnEncounterComplete.AddUniqueDynamic(this, &AETMSandboxGameMode::HandleEncounterComplete);
		Flow->BeginLevel(GetWorld(), Cast<AXRSeatedRigPawn>(FindRigPawn()));
	}
}

void AETMSandboxGameMode::BuildRange()
{
	// Light: one movable directional light, from above and behind the rig so everything facing the player
	// (enemies, panels, the objective) is lit. No shadows.
	Sun = SpawnSandboxActor<ADirectionalLight>(FVector(0.f, 0.f, 1000.f), FRotator(-50.f, 20.f, 0.f));
	if (Sun)
	{
		ULightComponent* Light = Sun->GetLightComponent();
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetCastShadows(false);
		Light->SetIntensity(GetCurrentLightIntensity()); // a world loaded mid-transition starts black
	}

	// Floor: 60 m square, its top at z = 0.
	SpawnMesh(TEXT("Cube"), FVector(2000.f, 0.f, -5.f), FVector(6000.f, 6000.f, 10.f));
	// The rig's mount: a block under the seat, kept behind the cannon mounts so low shots clear it.
	if (RigBaseHeight > 1.f) SpawnMesh(TEXT("Cube"), FVector(-30.f, 0.f, RigBaseHeight * 0.5f), FVector(120.f, 160.f, RigBaseHeight));

	// Distance posts down three lanes: left duct, centre door, right conveyor (bible section 9 layout).
	for (const float Yaw : LaneYaw)
	{
		for (const float Metres : { 5.f, 15.f, 30.f, 45.f })
		{
			const FVector At = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(Metres * 100.f, 0.f, 0.f));
			SpawnMesh(TEXT("Cylinder"), At + FVector(0.f, 0.f, 50.f + Metres), FVector(20.f, 20.f, 100.f + Metres * 2.f));
		}
	}

	// Entrances 18 m out on each lane, facing the rig (brought in after the owner's headset test, T-0055).
	Director = SpawnSandboxActor<AEncounterDirector>(FVector::ZeroVector);
	if (!Director) return;
	for (const float Yaw : LaneYaw)
	{
		const FVector At = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(1800.f, 0.f, 0.f));
		Director->Entrances.Add(SpawnSandboxActor<ATargetPoint>(At, FRotator(0.f, Yaw + 180.f, 0.f)));
	}
	Director->EntranceScatter = 150.f;
	Director->RigActor = FindRigPawn();
}

void AETMSandboxGameMode::BuildObjective()
{
	// Objective: in front of and below the rig, centre lane (P-22). Its bulk is low (90 cm) so it never hides
	// the centre lane from the mounted rig (owner headset test, T-0031).
	Objective = SpawnSandboxActor<AMissionObjective>(FVector(350.f, 0.f, 0.f));
	if (Objective) SpawnMesh(TEXT("Cube"), FVector(350.f, 0.f, 45.f), FVector(160.f, 220.f, 90.f)); // the equipment's bulk
}

void AETMSandboxGameMode::StartEncounter(const TArray<FEncounterPhase>& Phases)
{
	if (!Director) return;
	Director->Phases = Phases;
	if (UMissionStateSubsystem* Mission = GetWorld()->GetSubsystem<UMissionStateSubsystem>())
	{
		Mission->OnMissionFailed.AddUniqueDynamic(this, &AETMSandboxGameMode::HandleMissionFailed);
	}
	if (!Director->StartEncounter(EncounterSeed))
	{
		UE_LOG(LogETM, Error, TEXT("Sandbox encounter did not start."));
	}
}

TArray<FEncounterPhase> AETMSandboxGameMode::MakeLevelPhases(int32 LevelIndex)
{
	// Stand-ins until the real arenas and their storyboards (T-0020, T-0027), using the archetypes each level
	// introduces first. Entrances: 0 left duct/trench, 1 centre, 2 right conveyor/balcony.
	// Owner headset test (T-0056): spawns were too slow, so gaps are 1.5-2.5 s and slots short (the P-13 60%
	// rule holds a cleared phase until 60% of its slot); drones come from the left as well as the right. The
	// real arenas keep the bible's layout (flyers on the right) unless the owner changes it.
	if (LevelIndex == 0)
	{
		return {
			Phase(TEXT("Breach: duct"), 25.f, 5, { Group(AShardling::StaticClass(), 9, 0, 1.5f, 1.f) }, true),
			Phase(TEXT("Breach: conveyor"), 30.f, 7, {
				Group(ANeedleDrone::StaticClass(), 5, 2, 2.f, 1.f),
				Group(ANeedleDrone::StaticClass(), 3, 0, 2.5f, 3.f),
				Group(AShardling::StaticClass(), 6, 1, 1.8f, 2.f) }, false),
		};
	}
	return {
		Phase(TEXT("Signal: trench"), 25.f, 6, {
			Group(AShardling::StaticClass(), 7, 0, 1.5f, 1.f),
			Group(ASiphon::StaticClass(), 2, 1, 2.5f, 2.f) }, true),
		Phase(TEXT("Signal: balcony"), 30.f, 7, {
			Group(ANeedleDrone::StaticClass(), 5, 2, 1.8f, 1.f),
			Group(ANeedleDrone::StaticClass(), 4, 0, 2.2f, 2.f),
			Group(AShardling::StaticClass(), 6, 1, 1.8f, 2.f) }, false),
	};
}

// ---------------------------------------------------------------------------------------------
// Panels and flow events
// ---------------------------------------------------------------------------------------------

void AETMSandboxGameMode::ShowPanel(EETMFlowStage Stage)
{
	if (Panel)
	{
		Panel->Destroy();
		Panel = nullptr;
	}

	FText Title;
	FString Body;
	TArray<FETMMenuButton> Buttons;
	FVector At = PanelLocation;
	const auto Button = [&Buttons](const TCHAR* Id, const FText& Label)
	{
		FETMMenuButton& B = Buttons.AddDefaulted_GetRef();
		B.Id = FName(Id);
		B.Label = Label;
	};

	switch (Stage)
	{
	case EETMFlowStage::Title:
		Title = LOCTEXT("Title", "ENTER THE MATRICKS");
		Body = TEXT("RIG-6 online. Haven Nine is evacuating.\nAim a cannon at a button and fire.");
		Button(TEXT("Start"), LOCTEXT("Start", "START CAMPAIGN"));
		Button(TEXT("Practice"), LOCTEXT("Practice", "PRACTICE RANGE"));
		break;
	case EETMFlowStage::LevelResults:
	{
		const int32 Index = Flow ? Flow->GetLevelIndex() : 0;
		const FETMMissionResult R = Flow ? Flow->GetLevelResult(Index) : FETMMissionResult();
		Title = FText::FromString(FString::Printf(TEXT("LEVEL %d COMPLETE: %s"), Index + 1, ETMFlow::LevelTitle(Index)));
		Body = TEXT("Power reaches the evacuation railway.\n") + ResultLines(R);
		if (Flow && Flow->Store)
		{
			const FETMLevelRecord Record = Flow->Store->GetLevelRecord(R.Level);
			if (Record.bHasBest) Body += FString::Printf(TEXT("\nBest %d"), Record.Best.Score);
		}
		Button(TEXT("Continue"), LOCTEXT("Continue", "CONTINUE"));
		break;
	}
	case EETMFlowStage::Ending:
		Title = LOCTEXT("EndingTitle", "THE LAST SIGNAL");
		Body = TEXT("MARA: \"Last carriage is clear. You bought us a tomorrow.\"\n\nThe shutters close. The machines falter.\nFar above, a distant signal answers.\nThe invasion continues.");
		Button(TEXT("Skip"), LOCTEXT("Skip", "SKIP"));
		break;
	case EETMFlowStage::CampaignResults:
	{
		Title = LOCTEXT("CampaignTitle", "HAVEN NINE EVACUATED");
		for (int32 i = 0; i < ETMFlow::LevelCount; ++i)
		{
			const FETMMissionResult R = Flow ? Flow->GetLevelResult(i) : FETMMissionResult();
			Body += FString::Printf(TEXT("L%d %s: %s, %.0f%%, score %d\n"), i + 1, ETMFlow::LevelTitle(i),
				*ETMFlow::FormatTime(R.DurationSeconds), R.GetAccuracy() * 100.f, R.Score);
		}
		Body += FString::Printf(TEXT("Campaign score %d"), Flow ? Flow->GetCampaignScore() : 0);
		Button(TEXT("Title"), LOCTEXT("ReturnToTitle", "RETURN TO TITLE"));
		break;
	}
	case EETMFlowStage::Practice:
		// Small and high on the left, out of the lanes, so practice fire does not hit it by accident.
		Title = LOCTEXT("PracticeTitle", "PRACTICE RANGE");
		Button(TEXT("Title"), LOCTEXT("Back", "TITLE"));
		At = FVector(220.f, -170.f, 175.f);
		break;
	default:
		return;
	}

	// In the rig's frame, rotated to face it (the panel's +X points back at the player).
	FTransform RigFrame = FTransform::Identity;
	if (const APawn* Rig = FindRigPawn()) RigFrame = FTransform(FRotator(0.f, Rig->GetActorRotation().Yaw, 0.f), Rig->GetActorLocation());
	Panel = SpawnSandboxActor<AETMMenuPanel>(RigFrame.TransformPosition(At), RigFrame.Rotator() + FRotator(0.f, 180.f, 0.f));
	if (!Panel) return;
	Panel->Setup(Title, Body, Buttons);
	Panel->OnButtonPressed.AddUniqueDynamic(this, &AETMSandboxGameMode::HandlePanelButton);
}

void AETMSandboxGameMode::HandlePanelButton(FName ButtonId)
{
	if (!Flow) return;
	if (ButtonId == TEXT("Start")) Flow->StartCampaign();
	else if (ButtonId == TEXT("Practice")) Flow->StartPractice();
	else if (ButtonId == TEXT("Continue")) Flow->Continue();
	else if (ButtonId == TEXT("Skip")) Flow->SkipEnding();
	else if (ButtonId == TEXT("Title")) Flow->ReturnToTitle();
}

void AETMSandboxGameMode::HandleStageChanged(EETMFlowStage NewStage, int32 LevelIndex)
{
	// Stages shown in place, in the world that just finished a level.
	if (NewStage == EETMFlowStage::LevelResults || NewStage == EETMFlowStage::Ending || NewStage == EETMFlowStage::CampaignResults)
	{
		ShowPanel(NewStage);
	}
}

void AETMSandboxGameMode::HandleEncounterComplete()
{
	if (Flow) Flow->NotifyLevelComplete(GetWorld());
}

void AETMSandboxGameMode::HandleMissionFailed(EMissionFailReason Reason)
{
	// No failure screen yet: retry by itself after a short beat, while a level or the practice range is on.
	if (Flow && Flow->GetStage() != EETMFlowStage::Level && Flow->GetStage() != EETMFlowStage::Practice) return;
	GetWorldTimerManager().SetTimer(RetryTimer, this, &AETMSandboxGameMode::DoRetry, FMath::Max(0.01f, AutoRetrySeconds), false);
}

void AETMSandboxGameMode::DoRetry()
{
	if (UMissionStateSubsystem* Mission = GetWorld() ? GetWorld()->GetSubsystem<UMissionStateSubsystem>() : nullptr)
	{
		Mission->RetryFromLastCheckpoint();
	}
}

#undef LOCTEXT_NAMESPACE
