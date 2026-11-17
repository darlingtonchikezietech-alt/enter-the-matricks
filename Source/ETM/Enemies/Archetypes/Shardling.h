// AShardling: small four-legged ground cutter. Targets infrastructure only; never damages the rig.
// Spec: bible section 7 (80 HP; scrapes forward, then raises cutting limbs for 1 s; 20 objective
// damage on arrival, then destroys itself), P-09. Owned by T-0014.
//
// Movement is a straight kinematic approach to the objective: no navigation mesh. Spawn points,
// lanes and attack tokens are the encounter director's (T-0017).
#pragma once

#include "CoreMinimal.h"
#include "Enemies/Base/EnemyBase.h"
#include "Shardling.generated.h"

class AMissionObjective;
class UAudioComponent;
class USoundBase;
class UStaticMeshComponent;

UCLASS()
class ETM_API AShardling : public AEnemyBase
{
	GENERATED_BODY()

public:
	AShardling();
	virtual void Tick(float DeltaSeconds) override;

	/** The objective to cut. If unset, the mission's active objective is used. Typed so it can never be the rig. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Shardling") void SetTarget(AMissionObjective* InTarget) { Target = InTarget; }
	UFUNCTION(BlueprintPure, Category = "ETM|Shardling") AMissionObjective* GetTarget() const { return Target.Get(); }
	/** 0 while scraping forward, rising to 1 as the cutting limbs lift during the warning. Drives the limb pose. */
	UFUNCTION(BlueprintPure, Category = "ETM|Shardling") float GetTelegraphProgress() const;

	/** One simulation step without the world's tick: state timers, then behaviour. For tests and tools. */
	void SimulateStep(float DeltaSeconds);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Shardling", meta = (ClampMin = "0")) float ObjectiveDamage = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Shardling", meta = (ClampMin = "0")) float LimbRaiseSeconds = 1.f;
	/** Prototype value; tune in the M1 playtest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Shardling", meta = (ClampMin = "0")) float MoveSpeed = 250.f;
	/** Horizontal distance from the objective at which it stops and raises its limbs. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Shardling", meta = (ClampMin = "0")) float AttackRange = 150.f;
	/** Limb angle at the top of the warning, in degrees. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Shardling") float LimbRaisedPitch = 70.f;
	/** Plays during the warning (sound cue alongside the limb shape). Assign a sound asset; none ships yet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Shardling") TObjectPtr<USoundBase> TelegraphSound;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Shardling") TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Shardling") TObjectPtr<USceneComponent> LeftLimbPivot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Shardling") TObjectPtr<USceneComponent> RightLimbPivot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Shardling") TObjectPtr<UAudioComponent> TelegraphAudio;

protected:
	virtual void HandleStateChanged(EEnemyState NewState, EEnemyState OldState) override;
	virtual void ResolveAttack(AActor* AttackTarget, float Damage) override;

private:
	void UpdateBehavior(float DeltaSeconds);
	void SetLimbPose(float Progress);
	AMissionObjective* ResolveTarget();

	UPROPERTY(Transient) TWeakObjectPtr<AMissionObjective> Target;
};
