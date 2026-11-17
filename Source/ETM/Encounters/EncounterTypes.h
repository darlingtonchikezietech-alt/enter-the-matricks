// Authored encounter data: phases and the spawn groups inside them.
// Spec: bible section 6 "Encounter director", section 8 (timing, P-13), sections 9-10 (schedules).
// Owned by T-0017. The full Level 1 and Level 2 schedules are authored in T-0026 and T-0029.
#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"
#include "EncounterTypes.generated.h"

class AEnemyBase;

/** A run of identical enemies from one entrance: "8 Shardlings in small groups from the left duct". */
USTRUCT(BlueprintType)
struct FEncounterSpawnGroup
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn") TSubclassOf<AEnemyBase> EnemyClass;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0")) int32 Count = 1;
	/** Index into the director's Entrances. Entrances must be readable: never behind the player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0")) int32 EntranceIndex = 0;
	/** Seconds into the phase before the first of this group may appear. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0")) float StartDelay = 0.f;
	/** Seconds between members of the group. Spawns held back by the active cap come later, never in a burst. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spawn", meta = (ClampMin = "0")) float Interval = 2.f;
};

/** One storyboard row. */
USTRUCT(BlueprintType)
struct FEncounterPhase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase") FName Name;
	/** Length of the phase's storyboard slot. Used for the P-13 early-transition rule, not as a hard limit. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase", meta = (ClampMin = "0")) float SlotSeconds = 60.f;
	/** "Maximum N active" from the storyboard. 0 means only the director's global cap applies. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase", meta = (ClampMin = "0")) int32 MaxActive = 0;
	/** Boss phase: adds are capped at the director's BossAddCap (bible section 14: boss plus at most 4 adds). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase") bool bBossPhase = false;
	/**
	 * True: the phase is complete once every group has spawned and every enemy it spawned is gone.
	 * False: something else ends it by calling AEncounterDirector::CompletePhase (a boss's death).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase") bool bCompleteWhenCleared = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase") TArray<FEncounterSpawnGroup> Groups;

	/** Attack-token limit while this phase runs (bible section 14: at most 2). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase", meta = (ClampMin = "0")) int32 MaxConcurrentAttacks = 2;
	/** Rig repair applied when the phase starts, before its checkpoint (P-05: 60 at each level's midpoint). Never on a retry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase", meta = (ClampMin = "0")) float RigRepairOnStart = 0.f;
	/** Record a mission checkpoint when the phase starts (midpoint, boss start). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase") bool bCheckpointOnStart = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase") FName CheckpointName;
	/** The checkpoint restores the P-04 boss preset on retry. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Phase") bool bBossCheckpoint = false;
};
