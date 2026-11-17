#include "Objectives/MissionStateSubsystem.h"
#include "Objectives/MissionObjective.h"
#include "Combat/IntegrityComponent.h"
#include "Combat/DefenseComponent.h"
#include "Enemies/Base/EnemyBase.h"
#include "Enemies/Base/EnemyPoolSubsystem.h"
#include "EngineUtils.h"
#include "ETM.h"
#include "Engine/World.h"

void UMissionStateSubsystem::RegisterRig(UIntegrityComponent* RigIntegrity, UDefenseComponent* RigDefense)
{
	if (UIntegrityComponent* Old = Rig.Get())
	{
		Old->OnDepleted.RemoveDynamic(this, &UMissionStateSubsystem::HandleRigDepleted);
	}
	Rig = RigIntegrity;
	Defense = RigDefense;
	if (RigIntegrity)
	{
		RigIntegrity->OnDepleted.AddUniqueDynamic(this, &UMissionStateSubsystem::HandleRigDepleted);
	}
}

void UMissionStateSubsystem::RegisterObjective(AMissionObjective* InObjective)
{
	if (UIntegrityComponent* Old = GetObjectiveIntegrity())
	{
		Old->OnDepleted.RemoveDynamic(this, &UMissionStateSubsystem::HandleObjectiveDepleted);
	}
	Objective = InObjective;
	if (UIntegrityComponent* New = GetObjectiveIntegrity())
	{
		New->OnDepleted.AddUniqueDynamic(this, &UMissionStateSubsystem::HandleObjectiveDepleted);
	}
}

UIntegrityComponent* UMissionStateSubsystem::GetObjectiveIntegrity() const
{
	const AMissionObjective* Current = Objective.Get();
	return Current ? Current->Integrity.Get() : nullptr;
}

void UMissionStateSubsystem::StartMission(int32 EncounterSeed)
{
	if (UIntegrityComponent* RigIntegrity = Rig.Get()) RigIntegrity->ResetToMax();
	if (UIntegrityComponent* ObjectiveIntegrity = GetObjectiveIntegrity()) ObjectiveIntegrity->ResetToMax();
	if (UDefenseComponent* Brace = Defense.Get()) Brace->ResetBrace(/*bClearAbsorbed*/ true);

	State = EMissionState::Running;
	FailReason = EMissionFailReason::None;
	RetryCount = 0;
	LastCheckpoint = FMissionCheckpoint();
	RecordCheckpoint(TEXT("Start"), 0, 0.f, false, EncounterSeed);
}

bool UMissionStateSubsystem::RecordCheckpoint(FName Name, int32 PhaseIndex, float MissionTime, bool bBossCheckpoint, int32 EncounterSeed)
{
	if (State != EMissionState::Running) return false;

	const UIntegrityComponent* RigIntegrity = Rig.Get();
	const UIntegrityComponent* ObjectiveIntegrity = GetObjectiveIntegrity();

	FMissionCheckpoint Checkpoint;
	Checkpoint.Name = Name;
	Checkpoint.PhaseIndex = PhaseIndex;
	Checkpoint.MissionTime = MissionTime;
	Checkpoint.EncounterSeed = EncounterSeed;
	Checkpoint.bBossCheckpoint = bBossCheckpoint;
	Checkpoint.RigIntegrity = RigIntegrity ? RigIntegrity->GetCurrent() : 0.f;
	Checkpoint.ObjectiveIntegrity = ObjectiveIntegrity ? ObjectiveIntegrity->GetCurrent() : 0.f;
	Checkpoint.bValid = true;

	LastCheckpoint = Checkpoint;
	OnCheckpointRecorded.Broadcast(LastCheckpoint);
	return true;
}

void UMissionStateSubsystem::HandleRigDepleted()
{
	Fail(EMissionFailReason::RigDestroyed);
}

void UMissionStateSubsystem::HandleObjectiveDepleted()
{
	Fail(EMissionFailReason::ObjectiveDestroyed);
}

void UMissionStateSubsystem::Fail(EMissionFailReason Reason)
{
	if (State != EMissionState::Running) return; // first failure wins; a second pool emptying in the same frame is ignored
	State = EMissionState::Failed;
	FailReason = Reason;
	UE_LOG(LogETM, Display, TEXT("Mission failed: %s. Last checkpoint '%s'."),
		Reason == EMissionFailReason::RigDestroyed ? TEXT("rig destroyed") : TEXT("objective destroyed"),
		*LastCheckpoint.Name.ToString());
	OnMissionFailed.Broadcast(Reason);
}

bool UMissionStateSubsystem::RetryFromLastCheckpoint()
{
	if (State != EMissionState::Failed) return false;
	RestoreLastCheckpoint(/*bCountAsRetry*/ true);
	return true;
}

bool UMissionStateSubsystem::ResumeFromCheckpoint(const FMissionCheckpoint& Checkpoint)
{
	// After an app restart: the level has just loaded and the encounter has started; jump to the saved point
	// exactly as a retry would, including the P-04 boss preset, but it is not a retry for the stats.
	if (!Checkpoint.bValid) return false;
	LastCheckpoint = Checkpoint;
	RestoreLastCheckpoint(/*bCountAsRetry*/ false);
	return true;
}

void UMissionStateSubsystem::RestoreLastCheckpoint(bool bCountAsRetry)
{

	// Every enemy is parked with no death sequence; the director repopulates the phase.
	// Enemies placed in the level or spawned outside the pool are parked too, not only pooled ones.
	if (UWorld* World = GetWorld())
	{
		if (UEnemyPoolSubsystem* Pool = World->GetSubsystem<UEnemyPoolSubsystem>())
		{
			Pool->DeactivateAll();
		}
		for (TActorIterator<AEnemyBase> It(World); It; ++It)
		{
			It->Deactivate();
		}
	}

	if (!LastCheckpoint.bValid)
	{
		UE_LOG(LogETM, Warning, TEXT("Retry with no checkpoint recorded; restarting from full health."));
		LastCheckpoint.Name = TEXT("Start");
		LastCheckpoint.bValid = true;
		LastCheckpoint.RigIntegrity = Rig.IsValid() ? Rig->GetMax() : 0.f;
		LastCheckpoint.ObjectiveIntegrity = GetObjectiveIntegrity() ? GetObjectiveIntegrity()->GetMax() : 0.f;
	}

	UIntegrityComponent* RigIntegrity = Rig.Get();
	UIntegrityComponent* ObjectiveIntegrity = GetObjectiveIntegrity();
	if (LastCheckpoint.bBossCheckpoint)
	{
		// P-04 preset: rig full; objective at the saved value but never below the floor; boss at full health (director).
		if (RigIntegrity) RigIntegrity->ResetToMax();
		if (ObjectiveIntegrity) ObjectiveIntegrity->RestoreCheckpoint(LastCheckpoint.ObjectiveIntegrity, BossObjectiveFloor);
	}
	else
	{
		if (RigIntegrity) RigIntegrity->ResetTo(LastCheckpoint.RigIntegrity);
		if (ObjectiveIntegrity) ObjectiveIntegrity->ResetTo(LastCheckpoint.ObjectiveIntegrity);
	}
	if (UDefenseComponent* Brace = Defense.Get()) Brace->ResetBrace();

	State = EMissionState::Running;
	FailReason = EMissionFailReason::None;
	if (bCountAsRetry) ++RetryCount;
	OnRetryStarted.Broadcast(LastCheckpoint);
}
