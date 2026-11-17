// UMissionStateSubsystem: mission failure and checkpoint retry for one level.
// Spec: bible section 6 "Health model" (failure when rig or objective reaches zero; recovery
// retries the most recent checkpoint), "Checkpoints", P-04. Owned by T-0018.
//
// Flow: StartMission at level start -> RecordCheckpoint at midpoint and boss start (the encounter
// director, T-0017, calls it) -> on failure OnMissionFailed fires -> the failure screen (T-0031)
// calls RetryFromLastCheckpoint -> OnRetryStarted tells the director which phase to restart.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Objectives/MissionTypes.h"
#include "MissionStateSubsystem.generated.h"

class AMissionObjective;
class UIntegrityComponent;
class UDefenseComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMissionFailed, EMissionFailReason, Reason);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCheckpointRecorded, const FMissionCheckpoint&, Checkpoint);
/** Health has been restored and enemies parked. The director restarts Checkpoint.PhaseIndex with Checkpoint.EncounterSeed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRetryStarted, const FMissionCheckpoint&, Checkpoint);

UCLASS()
class ETM_API UMissionStateSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** The rig's health and brace. The rig pawn or the game mode registers them; failure is watched from here. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Mission") void RegisterRig(UIntegrityComponent* RigIntegrity, UDefenseComponent* RigDefense);
	/** The objective now being defended. Replaces any previous one. Objectives call this themselves at BeginPlay. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Mission") void RegisterObjective(AMissionObjective* Objective);

	/**
	 * Level start (and entering Level 2): fills rig and objective, clears earlier checkpoints,
	 * and records the start checkpoint.
	 */
	UFUNCTION(BlueprintCallable, Category = "ETM|Mission") void StartMission(int32 EncounterSeed = 0);
	/** Saves a restore point from the current health. Ignored unless the mission is running. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Mission") bool RecordCheckpoint(FName Name, int32 PhaseIndex, float MissionTime, bool bBossCheckpoint, int32 EncounterSeed = 0);
	/**
	 * Restores the most recent checkpoint after a failure: parks every pooled enemy, resets the brace,
	 * restores health (boss checkpoints use the P-04 preset), and fires OnRetryStarted. Returns false if
	 * the mission has not failed.
	 */
	UFUNCTION(BlueprintCallable, Category = "ETM|Mission") bool RetryFromLastCheckpoint();
	/**
	 * Continues from a checkpoint saved before the app closed (T-0023). Start the encounter first, then call
	 * this: it restores as a retry does (boss checkpoints use the P-04 preset) without counting as a retry.
	 */
	UFUNCTION(BlueprintCallable, Category = "ETM|Mission") bool ResumeFromCheckpoint(const FMissionCheckpoint& Checkpoint);

	UFUNCTION(BlueprintPure, Category = "ETM|Mission") EMissionState GetState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "ETM|Mission") EMissionFailReason GetFailReason() const { return FailReason; }
	UFUNCTION(BlueprintPure, Category = "ETM|Mission") FMissionCheckpoint GetLastCheckpoint() const { return LastCheckpoint; }
	UFUNCTION(BlueprintPure, Category = "ETM|Mission") int32 GetRetryCount() const { return RetryCount; }
	UFUNCTION(BlueprintPure, Category = "ETM|Mission") AMissionObjective* GetObjective() const { return Objective.Get(); }

	/** P-04 boss preset floor for the objective: 0.6 on Standard. The difficulty setting (T-0037) sets 0.8 for Assisted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Mission", meta = (ClampMin = "0", ClampMax = "1")) float BossObjectiveFloor = 0.6f;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Mission") FOnMissionFailed OnMissionFailed;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Mission") FOnCheckpointRecorded OnCheckpointRecorded;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Mission") FOnRetryStarted OnRetryStarted;

private:
	UFUNCTION() void HandleRigDepleted();
	UFUNCTION() void HandleObjectiveDepleted();
	void Fail(EMissionFailReason Reason);
	void RestoreLastCheckpoint(bool bCountAsRetry);
	UIntegrityComponent* GetObjectiveIntegrity() const;

	UPROPERTY(Transient) TWeakObjectPtr<UIntegrityComponent> Rig;
	UPROPERTY(Transient) TWeakObjectPtr<UDefenseComponent> Defense;
	UPROPERTY(Transient) TWeakObjectPtr<AMissionObjective> Objective;

	FMissionCheckpoint LastCheckpoint;
	EMissionState State = EMissionState::Idle;
	EMissionFailReason FailReason = EMissionFailReason::None;
	int32 RetryCount = 0;
};
