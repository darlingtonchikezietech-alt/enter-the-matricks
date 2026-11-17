// AEncounterDirector: runs a level's authored phases.
// Spec: bible section 6 "Encounter director", section 8 (P-13 early transition), section 14 (caps).
// Owned by T-0017.
//
// What it guarantees:
//   - spawns only from authored entrances, and refuses to start if one is behind the player;
//   - never exceeds the phase cap, the global cap of 8, or 4 adds in a boss phase;
//   - holds spawns back while the budget is full, at most one spawn per group per tick;
//   - sets the attack-token limit (max 2 simultaneous attacks) for each phase;
//   - stops reinforcements once a phase's groups are spent;
//   - moves on early only after 60% of the slot and all required dialogue (P-13), and lets an
//     unfinished phase run long rather than cutting it off;
//   - never removes an enemy itself: anything still alive at a phase change carries over;
//   - records checkpoints and repairs through the mission state (T-0018) and rebuilds the
//     checkpoint's phase on retry with the same seed.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Math/RandomStream.h"
#include "Encounters/EncounterTypes.h"
#include "Objectives/MissionTypes.h"
#include "EncounterDirector.generated.h"

class AEnemyBase;

UENUM(BlueprintType)
enum class EEncounterState : uint8
{
	Idle,
	Running,
	/** The mission failed; nothing spawns until a retry. */
	Suspended,
	Complete,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEncounterPhaseStarted, int32, PhaseIndex, FName, PhaseName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEncounterPhaseCompleted, int32, PhaseIndex, FName, PhaseName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnEncounterComplete);
/** Lets level scripting configure a fresh enemy (targets, hover points) right after it appears. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEncounterEnemySpawned, AEnemyBase*, Enemy, int32, PhaseIndex, int32, GroupIndex);

UCLASS()
class ETM_API AEncounterDirector : public AActor
{
	GENERATED_BODY()

public:
	AEncounterDirector();
	virtual void Tick(float DeltaSeconds) override;

	/**
	 * Validates the data, prewarms the enemy pool, starts the mission and the first phase.
	 * Returns false, and starts nothing, if an entrance is missing or behind the player.
	 */
	UFUNCTION(BlueprintCallable, Category = "ETM|Encounter") bool StartEncounter(int32 Seed = 0);
	/** Ends the current phase now. For phases that are not cleared by killing (boss death). */
	UFUNCTION(BlueprintCallable, Category = "ETM|Encounter") void CompletePhase();
	/** Required dialogue for the current phase is playing. A finished phase waits for it (P-13). */
	UFUNCTION(BlueprintCallable, Category = "ETM|Encounter") void SetDialoguePending(bool bPending) { bDialoguePending = bPending; }

	/** Returns the error for the first invalid entrance, or an empty string if all are usable. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Encounter") FString ValidateEntrances() const;

	UFUNCTION(BlueprintPure, Category = "ETM|Encounter") EEncounterState GetState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "ETM|Encounter") int32 GetPhaseIndex() const { return PhaseIndex; }
	UFUNCTION(BlueprintPure, Category = "ETM|Encounter") float GetPhaseElapsed() const { return PhaseElapsed; }
	/** Seconds of play in this level, excluding time spent failed. Retries rewind it to the checkpoint. */
	UFUNCTION(BlueprintPure, Category = "ETM|Encounter") float GetMissionTime() const { return MissionTime; }
	/** Enemies this director spawned that are still threats (alive and not dying). */
	UFUNCTION(BlueprintPure, Category = "ETM|Encounter") int32 GetLiveCount() const;
	/** The cap in force for the current phase. */
	UFUNCTION(BlueprintPure, Category = "ETM|Encounter") int32 GetActiveCap() const;
	/** Every group in the current phase has spawned its full count. */
	UFUNCTION(BlueprintPure, Category = "ETM|Encounter") bool AreGroupsSpent() const;
	UFUNCTION(BlueprintPure, Category = "ETM|Encounter") int32 GetSpawnedCount(int32 GroupIndex) const { return SpawnedPerGroup.IsValidIndex(GroupIndex) ? SpawnedPerGroup[GroupIndex] : 0; }
	/** Spawns refused because a budget was full, over the whole encounter. For tuning. */
	UFUNCTION(BlueprintPure, Category = "ETM|Encounter") int32 GetHeldBackCount() const { return HeldBackCount; }
	/** The enemies counted by GetLiveCount, for tests and debug views. */
	TArray<AEnemyBase*> GetLiveEnemies() const;

	/** Advances spawning and phase logic. Tick calls this; it is public so tests can step time directly. */
	void AdvanceDirector(float DeltaSeconds);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Encounter") TArray<FEncounterPhase> Phases;
	/** Spawn points placed in the level (any actor; its transform is used). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Encounter") TArray<TObjectPtr<AActor>> Entrances;
	/** The player's rig. Used for repairs and the behind-the-player check. If unset, the first player pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Encounter") TObjectPtr<AActor> RigActor;

	/** Normal active-enemy cap (bible section 14). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Encounter", meta = (ClampMin = "1")) int32 GlobalActiveCap = 8;
	/** Adds allowed alongside a boss (bible section 14). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Encounter", meta = (ClampMin = "0")) int32 BossAddCap = 4;
	/** P-13: a finished phase may end once this fraction of its slot has passed. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Encounter", meta = (ClampMin = "0", ClampMax = "1")) float EarlyTransitionFraction = 0.6f;
	/** An entrance further than this from the rig's forward direction counts as behind the player. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Encounter", meta = (ClampMin = "0", ClampMax = "180")) float MaxEntranceAngle = 110.f;
	/** Random offset around an entrance, so a group does not stack on one point. Seeded: retries repeat it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Encounter", meta = (ClampMin = "0")) float EntranceScatter = 100.f;
	/** Call StartMission on the mission state when the encounter starts. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Encounter") bool bStartsMission = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Encounter") bool bStartOnBeginPlay = false;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Encounter") FOnEncounterPhaseStarted OnPhaseStarted;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Encounter") FOnEncounterPhaseCompleted OnPhaseCompleted;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Encounter") FOnEncounterComplete OnEncounterComplete;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Encounter") FOnEncounterEnemySpawned OnEnemySpawned;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	void BeginPhase(int32 Index, bool bFromRetry);
	void TrySpawn();
	bool ShouldTransition() const;
	void PruneLive();
	AActor* ResolveRig() const;
	void PrewarmPool() const;
	void BindMission();

	UFUNCTION() void HandleMissionFailed(EMissionFailReason Reason);
	UFUNCTION() void HandleRetry(const FMissionCheckpoint& Checkpoint);

	TArray<TWeakObjectPtr<AEnemyBase>> Live;
	TArray<int32> SpawnedPerGroup;
	FRandomStream Random;
	EEncounterState State = EEncounterState::Idle;
	int32 PhaseIndex = INDEX_NONE;
	int32 EncounterSeed = 0;
	float PhaseElapsed = 0.f;
	float MissionTime = 0.f;
	int32 HeldBackCount = 0;
	bool bDialoguePending = false;
	bool bPhaseCompletedExternally = false;
	bool bMissionBound = false;
};
