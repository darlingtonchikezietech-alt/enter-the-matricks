// Shared mission types: failure reasons and checkpoint records.
// Spec: bible section 6 "Health model" and "Checkpoints", P-04, P-05. Owned by T-0018.
#pragma once

#include "CoreMinimal.h"
#include "MissionTypes.generated.h"

UENUM(BlueprintType)
enum class EMissionState : uint8
{
	/** No mission started yet in this world. */
	Idle,
	Running,
	/** Rig or objective integrity reached zero. Waits for RetryFromLastCheckpoint. */
	Failed,
};

UENUM(BlueprintType)
enum class EMissionFailReason : uint8
{
	None,
	RigDestroyed,
	ObjectiveDestroyed,
};

/**
 * A restore point. Captures what the bible says a checkpoint stores: phase, health, and the encounter
 * seed. Difficulty lives with the settings, not here. Persisting checkpoints to disk is T-0023.
 */
USTRUCT(BlueprintType)
struct FMissionCheckpoint
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Checkpoint") FName Name;
	/** Index of the encounter phase to restart from. The encounter director (T-0017) owns its meaning. */
	UPROPERTY(BlueprintReadOnly, Category = "Checkpoint") int32 PhaseIndex = 0;
	/** Seconds into the level when the checkpoint was taken (Level 1 midpoint is 300, boss start 435). */
	UPROPERTY(BlueprintReadOnly, Category = "Checkpoint") float MissionTime = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Checkpoint") int32 EncounterSeed = 0;
	/** Boss checkpoints restore the P-04 preset instead of the saved health. */
	UPROPERTY(BlueprintReadOnly, Category = "Checkpoint") bool bBossCheckpoint = false;
	UPROPERTY(BlueprintReadOnly, Category = "Checkpoint") float RigIntegrity = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Checkpoint") float ObjectiveIntegrity = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Checkpoint") bool bValid = false;
};

namespace ETMObjectiveTags
{
	/** Actor tag that marks an enemy as an infrastructure attacker: the only actors that may damage an objective. */
	static const FName InfrastructureAttacker(TEXT("ETM.InfrastructureAttacker"));
}
