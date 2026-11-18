#include "Core/Game/ETMSandboxGameMode.h"
#include "Encounters/EncounterDirector.h"
#include "Enemies/Archetypes/NeedleDrone.h"
#include "Enemies/Archetypes/Shardling.h"
#include "Input/RigPlayerController.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Player/XRSeatedRigPawn.h"
#include "ETM.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

AETMSandboxGameMode::AETMSandboxGameMode()
{
	DefaultPawnClass = AXRSeatedRigPawn::StaticClass();
	PlayerControllerClass = ARigPlayerController::StaticClass();
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

void AETMSandboxGameMode::StartPlay()
{
	Super::StartPlay();
	// Local players have been spawned by now; make sure each rig counts for the mission.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		RegisterRig(It->Get());
	}
	if (ShouldBuildSandbox())
	{
		BuildSandbox();
	}
}

void AETMSandboxGameMode::RestartPlayer(AController* NewPlayer)
{
	Super::RestartPlayer(NewPlayer);
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
	UE_LOG(LogETM, Display, TEXT("Building the sandbox range (T-0053)."));

	// Light: one movable directional light from above and in front, no shadows.
	if (ADirectionalLight* Sun = SpawnSandboxActor<ADirectionalLight>(FVector(0.f, 0.f, 1000.f), FRotator(-50.f, 160.f, 0.f)))
	{
		ULightComponent* Light = Sun->GetLightComponent();
		Light->SetMobility(EComponentMobility::Movable);
		Light->SetCastShadows(false);
		Light->SetIntensity(6.f);
	}

	// Floor: 60 m square, its top at z = 0 where the rig sits.
	SpawnMesh(TEXT("Cube"), FVector(2000.f, 0.f, -5.f), FVector(6000.f, 6000.f, 10.f));

	// Distance posts down three lanes: left duct, centre door, right conveyor (bible section 9 layout).
	const float LaneYaw[] = { -35.f, 0.f, 35.f };
	for (const float Yaw : LaneYaw)
	{
		for (const float Metres : { 5.f, 15.f, 30.f, 45.f })
		{
			const FVector At = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(Metres * 100.f, 0.f, 0.f));
			SpawnMesh(TEXT("Cylinder"), At + FVector(0.f, 0.f, 50.f + Metres), FVector(20.f, 20.f, 100.f + Metres * 2.f));
		}
	}

	// Objective: in front of and below the rig, centre lane (P-22).
	Objective = SpawnSandboxActor<AMissionObjective>(FVector(450.f, 0.f, 0.f));
	if (Objective) SpawnMesh(TEXT("Cube"), FVector(450.f, 0.f, 60.f), FVector(160.f, 220.f, 120.f)); // the equipment's bulk

	// Entrances 25 m out on each lane, facing the rig.
	Director = SpawnSandboxActor<AEncounterDirector>(FVector::ZeroVector);
	if (!Director) return;
	for (const float Yaw : LaneYaw)
	{
		const FVector At = FRotator(0.f, Yaw, 0.f).RotateVector(FVector(2500.f, 0.f, 0.f));
		Director->Entrances.Add(SpawnSandboxActor<ATargetPoint>(At, FRotator(0.f, Yaw + 180.f, 0.f)));
	}

	// One long practice phase: Shardlings from the left and centre, drones from the right.
	FEncounterPhase Practice;
	Practice.Name = TEXT("Practice");
	Practice.SlotSeconds = 3600.f;
	Practice.MaxActive = 5;
	const auto Group = [](TSubclassOf<AEnemyBase> Class, int32 Count, int32 Entrance, float Interval, float Delay)
	{
		FEncounterSpawnGroup G;
		G.EnemyClass = Class;
		G.Count = Count;
		G.EntranceIndex = Entrance;
		G.Interval = Interval;
		G.StartDelay = Delay;
		return G;
	};
	Practice.Groups = {
		Group(AShardling::StaticClass(), 300, 0, 6.f, 4.f),
		Group(AShardling::StaticClass(), 300, 1, 9.f, 8.f),
		Group(ANeedleDrone::StaticClass(), 150, 2, 12.f, 15.f),
	};
	Director->Phases = { Practice };
	Director->EntranceScatter = 150.f;
	if (const APlayerController* Player = GetWorld()->GetFirstPlayerController())
	{
		Director->RigActor = Player->GetPawn();
	}

	if (UMissionStateSubsystem* Mission = GetWorld()->GetSubsystem<UMissionStateSubsystem>())
	{
		Mission->OnMissionFailed.AddUniqueDynamic(this, &AETMSandboxGameMode::HandleMissionFailed);
	}
	if (!Director->StartEncounter(EncounterSeed))
	{
		UE_LOG(LogETM, Error, TEXT("Sandbox encounter did not start."));
	}
}

void AETMSandboxGameMode::HandleMissionFailed(EMissionFailReason Reason)
{
	// No failure screen yet (T-0031): retry by itself after a short beat.
	GetWorldTimerManager().SetTimer(RetryTimer, this, &AETMSandboxGameMode::DoRetry, FMath::Max(0.01f, AutoRetrySeconds), false);
}

void AETMSandboxGameMode::DoRetry()
{
	if (UMissionStateSubsystem* Mission = GetWorld() ? GetWorld()->GetSubsystem<UMissionStateSubsystem>() : nullptr)
	{
		Mission->RetryFromLastCheckpoint();
	}
}
