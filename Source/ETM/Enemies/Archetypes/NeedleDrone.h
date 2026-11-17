// ANeedleDrone: floating diamond with two lateral fins that bursts the rig.
// Spec: bible section 7 (120 HP; aiming line and rising tone for 1.5 s; burst 15 to the rig;
// shoot it or brace before the burst). Owned by T-0014.
//
// Flies straight to its hover point, faces the rig, then repeats: telegraph, burst, cooldown.
// The hover point and attack tokens come from the encounter director (T-0017).
#pragma once

#include "CoreMinimal.h"
#include "Enemies/Base/EnemyBase.h"
#include "NeedleDrone.generated.h"

class UAudioComponent;
class USoundBase;
class UStaticMeshComponent;

UCLASS()
class ETM_API ANeedleDrone : public AEnemyBase
{
	GENERATED_BODY()

public:
	ANeedleDrone();
	virtual void Tick(float DeltaSeconds) override;

	/** The rig to burst. If unset, the first local player's pawn is used. */
	UFUNCTION(BlueprintCallable, Category = "ETM|NeedleDrone") void SetTarget(AActor* InTarget) { Target = InTarget; }
	UFUNCTION(BlueprintPure, Category = "ETM|NeedleDrone") AActor* GetTarget() const { return Target.Get(); }
	/** Where to hold position. If never set, the drone rises HoverHeight above its spawn point. */
	UFUNCTION(BlueprintCallable, Category = "ETM|NeedleDrone") void SetHoverLocation(const FVector& InHover) { HoverLocation = InHover; bHoverAssigned = true; }
	UFUNCTION(BlueprintPure, Category = "ETM|NeedleDrone") FVector GetHoverLocation() const { return HoverLocation; }
	/** 0 at the start of the warning, 1 at the burst. Drives the aiming line and the rising tone. */
	UFUNCTION(BlueprintPure, Category = "ETM|NeedleDrone") float GetTelegraphProgress() const;
	/** Pitch the warning tone should play at right now: rises from 1 to 1 + TonePitchRise across the warning. */
	UFUNCTION(BlueprintPure, Category = "ETM|NeedleDrone") float GetTelegraphTonePitch() const { return 1.f + TonePitchRise * GetTelegraphProgress(); }
	UFUNCTION(BlueprintPure, Category = "ETM|NeedleDrone") bool IsAimLineVisible() const;

	/** One simulation step without the world's tick: state timers, then behaviour. For tests and tools. */
	void SimulateStep(float DeltaSeconds);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|NeedleDrone", meta = (ClampMin = "0")) float BurstDamage = 15.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|NeedleDrone", meta = (ClampMin = "0")) float TelegraphSeconds = 1.5f;
	/** Prototype values; tune in the M1 playtest. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|NeedleDrone", meta = (ClampMin = "0")) float AttackCooldown = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|NeedleDrone", meta = (ClampMin = "0")) float MoveSpeed = 400.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|NeedleDrone", meta = (ClampMin = "0")) float HoverHeight = 400.f;
	/** Within this distance of the hover point the drone counts as arrived and may attack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|NeedleDrone", meta = (ClampMin = "0")) float ArriveTolerance = 20.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|NeedleDrone", meta = (ClampMin = "0")) float TonePitchRise = 1.f;
	/** Plays during the warning, with its pitch rising (bible: "rising tone"). Assign a sound asset; none ships yet. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|NeedleDrone") TObjectPtr<USoundBase> TelegraphSound;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|NeedleDrone") TObjectPtr<UStaticMeshComponent> Body;
	/** Thin beam from the drone toward the rig, shown only during the warning: the shape cue. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|NeedleDrone") TObjectPtr<UStaticMeshComponent> AimLine;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|NeedleDrone") TObjectPtr<UAudioComponent> TelegraphAudio;

protected:
	virtual void HandleStateChanged(EEnemyState NewState, EEnemyState OldState) override;
	virtual void ResolveAttack(AActor* AttackTarget, float Damage) override;

private:
	void UpdateBehavior(float DeltaSeconds);
	void UpdateAimLine();
	AActor* ResolveTarget();

	UPROPERTY(Transient) TWeakObjectPtr<AActor> Target;
	FVector HoverLocation = FVector::ZeroVector;
	float CooldownRemaining = 0.f;
	bool bHoverAssigned = false;
};
