#include "Encounters/EncounterDirector.h"
#include "Enemies/Base/EnemyBase.h"
#include "Enemies/Base/EnemyPoolSubsystem.h"
#include "Enemies/Base/AttackTokenSubsystem.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Combat/IntegrityComponent.h"
#include "ETM.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

AEncounterDirector::AEncounterDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AEncounterDirector::BeginPlay()
{
	Super::BeginPlay();
	if (bStartOnBeginPlay)
	{
		StartEncounter(EncounterSeed);
	}
}

void AEncounterDirector::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMissionStateSubsystem* Mission = GetWorld() ? GetWorld()->GetSubsystem<UMissionStateSubsystem>() : nullptr)
	{
		Mission->OnMissionFailed.RemoveDynamic(this, &AEncounterDirector::HandleMissionFailed);
		Mission->OnRetryStarted.RemoveDynamic(this, &AEncounterDirector::HandleRetry);
	}
	Super::EndPlay(EndPlayReason);
}

void AEncounterDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AdvanceDirector(DeltaSeconds);
}

// ---------------------------------------------------------------------------------------------
// Setup
// ---------------------------------------------------------------------------------------------

AActor* AEncounterDirector::ResolveRig() const
{
	return RigActor ? RigActor.Get() : UGameplayStatics::GetPlayerPawn(this, 0);
}

FString AEncounterDirector::ValidateEntrances() const
{
	const AActor* Rig = ResolveRig();
	const float CosLimit = FMath::Cos(FMath::DegreesToRadians(MaxEntranceAngle));

	for (int32 P = 0; P < Phases.Num(); ++P)
	{
		for (int32 G = 0; G < Phases[P].Groups.Num(); ++G)
		{
			const FEncounterSpawnGroup& Group = Phases[P].Groups[G];
			const FString Where = FString::Printf(TEXT("phase %d '%s' group %d"), P, *Phases[P].Name.ToString(), G);
			if (!Group.EnemyClass)
			{
				return Where + TEXT(": no enemy class");
			}
			if (!Entrances.IsValidIndex(Group.EntranceIndex) || !Entrances[Group.EntranceIndex])
			{
				return Where + FString::Printf(TEXT(": entrance %d does not exist"), Group.EntranceIndex);
			}
			if (Rig)
			{
				// "Never spawn a damaging enemy immediately behind the player" (bible section 6).
				FVector ToEntrance = Entrances[Group.EntranceIndex]->GetActorLocation() - Rig->GetActorLocation();
				ToEntrance.Z = 0.f;
				FVector Forward = Rig->GetActorForwardVector();
				Forward.Z = 0.f;
				if (!ToEntrance.IsNearlyZero() && !Forward.IsNearlyZero()
					&& FVector::DotProduct(ToEntrance.GetSafeNormal(), Forward.GetSafeNormal()) < CosLimit)
				{
					return Where + FString::Printf(TEXT(": entrance %d is behind the player"), Group.EntranceIndex);
				}
			}
		}
	}
	return FString();
}

void AEncounterDirector::PrewarmPool() const
{
	UEnemyPoolSubsystem* Pool = GetWorld() ? GetWorld()->GetSubsystem<UEnemyPoolSubsystem>() : nullptr;
	if (!Pool) return;

	// Per class: enough actors for a full cap of live enemies plus a full cap still playing their death
	// sequence (a dying enemy keeps its actor until it is parked), plus survivors carried over from an
	// externally completed phase. Bounded by twice the global cap, and never more than the class's total.
	TMap<UClass*, int32> Total;
	for (const FEncounterPhase& Phase : Phases)
	{
		for (const FEncounterSpawnGroup& Group : Phase.Groups)
		{
			if (Group.EnemyClass) Total.FindOrAdd(Group.EnemyClass) += Group.Count;
		}
	}
	for (const TPair<UClass*, int32>& Pair : Total)
	{
		Pool->Prewarm(Pair.Key, FMath::Min(Pair.Value, 2 * GlobalActiveCap));
	}
}

void AEncounterDirector::BindMission()
{
	if (bMissionBound) return;
	if (UMissionStateSubsystem* Mission = GetWorld() ? GetWorld()->GetSubsystem<UMissionStateSubsystem>() : nullptr)
	{
		Mission->OnMissionFailed.AddUniqueDynamic(this, &AEncounterDirector::HandleMissionFailed);
		Mission->OnRetryStarted.AddUniqueDynamic(this, &AEncounterDirector::HandleRetry);
		bMissionBound = true;
	}
}

bool AEncounterDirector::StartEncounter(int32 Seed)
{
	if (Phases.Num() == 0)
	{
		UE_LOG(LogETM, Error, TEXT("%s: no phases to run."), *GetName());
		return false;
	}
	const FString Problem = ValidateEntrances();
	if (!Problem.IsEmpty())
	{
		UE_LOG(LogETM, Error, TEXT("%s: encounter not started: %s."), *GetName(), *Problem);
		return false;
	}

	EncounterSeed = Seed;
	MissionTime = 0.f;
	HeldBackCount = 0;
	Live.Reset();
	PrewarmPool();
	BindMission();

	if (bStartsMission)
	{
		if (UMissionStateSubsystem* Mission = GetWorld()->GetSubsystem<UMissionStateSubsystem>())
		{
			Mission->StartMission(Seed);
		}
	}

	State = EEncounterState::Running;
	SetActorTickEnabled(true);
	BeginPhase(0, /*bFromRetry*/ false);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Phases
// ---------------------------------------------------------------------------------------------

void AEncounterDirector::BeginPhase(int32 Index, bool bFromRetry)
{
	PhaseIndex = Index;
	PhaseElapsed = 0.f;
	bDialoguePending = false;
	bPhaseCompletedExternally = false;

	const FEncounterPhase& Phase = Phases[Index];
	SpawnedPerGroup.Init(0, Phase.Groups.Num());
	// Seeded per phase, so a retry from this phase places enemies exactly as before.
	Random.Initialize(EncounterSeed + 7919 * (Index + 1));

	if (UAttackTokenSubsystem* Tokens = GetWorld() ? GetWorld()->GetSubsystem<UAttackTokenSubsystem>() : nullptr)
	{
		Tokens->SetMaxConcurrentAttacks(Phase.MaxConcurrentAttacks);
	}

	if (!bFromRetry)
	{
		// Repair first, then the checkpoint, so a retry restores the repaired value (P-05).
		if (Phase.RigRepairOnStart > 0.f)
		{
			if (AActor* Rig = ResolveRig())
			{
				if (UIntegrityComponent* RigIntegrity = Rig->FindComponentByClass<UIntegrityComponent>())
				{
					RigIntegrity->Repair(Phase.RigRepairOnStart);
				}
			}
		}
		if (Phase.bCheckpointOnStart)
		{
			if (UMissionStateSubsystem* Mission = GetWorld()->GetSubsystem<UMissionStateSubsystem>())
			{
				Mission->RecordCheckpoint(Phase.CheckpointName.IsNone() ? Phase.Name : Phase.CheckpointName,
					Index, MissionTime, Phase.bBossCheckpoint, EncounterSeed);
			}
		}
	}

	OnPhaseStarted.Broadcast(Index, Phase.Name);
}

void AEncounterDirector::CompletePhase()
{
	if (State == EEncounterState::Running) bPhaseCompletedExternally = true;
}

bool AEncounterDirector::AreGroupsSpent() const
{
	if (!Phases.IsValidIndex(PhaseIndex)) return true;
	const FEncounterPhase& Phase = Phases[PhaseIndex];
	for (int32 G = 0; G < Phase.Groups.Num(); ++G)
	{
		if (SpawnedPerGroup[G] < Phase.Groups[G].Count) return false;
	}
	return true;
}

bool AEncounterDirector::ShouldTransition() const
{
	const FEncounterPhase& Phase = Phases[PhaseIndex];
	const bool bDone = Phase.bCompleteWhenCleared
		? (AreGroupsSpent() && GetLiveCount() == 0)
		: bPhaseCompletedExternally;
	// P-13: finished phases may move on early, but only after 60% of their slot and all required dialogue.
	return bDone && PhaseElapsed >= Phase.SlotSeconds * EarlyTransitionFraction && !bDialoguePending;
}

void AEncounterDirector::AdvanceDirector(float DeltaSeconds)
{
	if (State != EEncounterState::Running) return;

	PhaseElapsed += DeltaSeconds;
	MissionTime += DeltaSeconds;
	PruneLive();
	TrySpawn();

	if (ShouldTransition())
	{
		const FName Finished = Phases[PhaseIndex].Name;
		OnPhaseCompleted.Broadcast(PhaseIndex, Finished);
		if (PhaseIndex + 1 < Phases.Num())
		{
			BeginPhase(PhaseIndex + 1, /*bFromRetry*/ false);
		}
		else
		{
			State = EEncounterState::Complete;
			SetActorTickEnabled(false);
			OnEncounterComplete.Broadcast();
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Spawning
// ---------------------------------------------------------------------------------------------

int32 AEncounterDirector::GetActiveCap() const
{
	if (!Phases.IsValidIndex(PhaseIndex)) return GlobalActiveCap;
	const FEncounterPhase& Phase = Phases[PhaseIndex];
	int32 Cap = Phase.MaxActive > 0 ? FMath::Min(Phase.MaxActive, GlobalActiveCap) : GlobalActiveCap;
	if (Phase.bBossPhase) Cap = FMath::Min(Cap, BossAddCap);
	return Cap;
}

void AEncounterDirector::PruneLive()
{
	Live.RemoveAll([](const TWeakObjectPtr<AEnemyBase>& Enemy) { return !Enemy.IsValid() || !Enemy->IsThreat(); });
}

int32 AEncounterDirector::GetLiveCount() const
{
	int32 Count = 0;
	for (const TWeakObjectPtr<AEnemyBase>& Enemy : Live)
	{
		if (Enemy.IsValid() && Enemy->IsThreat()) ++Count;
	}
	return Count;
}

TArray<AEnemyBase*> AEncounterDirector::GetLiveEnemies() const
{
	TArray<AEnemyBase*> Result;
	for (const TWeakObjectPtr<AEnemyBase>& Enemy : Live)
	{
		if (Enemy.IsValid() && Enemy->IsThreat()) Result.Add(Enemy.Get());
	}
	return Result;
}

void AEncounterDirector::TrySpawn()
{
	UEnemyPoolSubsystem* Pool = GetWorld() ? GetWorld()->GetSubsystem<UEnemyPoolSubsystem>() : nullptr;
	if (!Pool) return;

	const FEncounterPhase& Phase = Phases[PhaseIndex];
	const int32 Cap = GetActiveCap();

	// At most one spawn per group per tick: enemies held back by the cap trickle in rather than appear in a clump.
	for (int32 G = 0; G < Phase.Groups.Num(); ++G)
	{
		const FEncounterSpawnGroup& Group = Phase.Groups[G];
		int32& Spawned = SpawnedPerGroup[G];
		if (Spawned >= Group.Count) continue;
		if (PhaseElapsed < Group.StartDelay + Spawned * Group.Interval) continue;

		if (GetLiveCount() >= Cap)
		{
			++HeldBackCount; // the threat budget is full: suppress this spawn for now (bible section 6)
			continue;
		}

		const AActor* Entrance = Entrances[Group.EntranceIndex];
		FVector Location = Entrance->GetActorLocation();
		if (EntranceScatter > 0.f)
		{
			const FVector2D Offset = FVector2D(Random.FRandRange(-1.f, 1.f), Random.FRandRange(-1.f, 1.f)) * EntranceScatter;
			Location += FVector(Offset.X, Offset.Y, 0.f);
		}

		AEnemyBase* Enemy = Pool->Acquire(Group.EnemyClass, FTransform(Entrance->GetActorRotation(), Location));
		if (!Enemy)
		{
			++HeldBackCount; // the pool refused (growth disabled and none parked)
			continue;
		}
		++Spawned;
		Live.Add(Enemy);
		OnEnemySpawned.Broadcast(Enemy, PhaseIndex, G);
	}
}

// ---------------------------------------------------------------------------------------------
// Mission failure and retry
// ---------------------------------------------------------------------------------------------

void AEncounterDirector::HandleMissionFailed(EMissionFailReason Reason)
{
	if (State == EEncounterState::Running) State = EEncounterState::Suspended;
}

void AEncounterDirector::HandleRetry(const FMissionCheckpoint& Checkpoint)
{
	if (State == EEncounterState::Idle || !Phases.IsValidIndex(Checkpoint.PhaseIndex)) return;

	// The mission state has already parked every enemy and restored health.
	Live.Reset();
	EncounterSeed = Checkpoint.EncounterSeed;
	MissionTime = Checkpoint.MissionTime;
	State = EEncounterState::Running;
	SetActorTickEnabled(true);
	BeginPhase(Checkpoint.PhaseIndex, /*bFromRetry*/ true); // no second repair, no new checkpoint
}
