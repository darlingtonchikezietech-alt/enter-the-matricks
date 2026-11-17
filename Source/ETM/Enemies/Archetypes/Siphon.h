// ASiphon: cable-bearing parasite that latches onto infrastructure and drains it.
// Spec: bible section 7 (200 HP; visible tether and objective-drain warning; destroy it to stop sustained
// objective damage; drain 5 objective integrity per second). Owned by T-0022.
//
// Crawls to the objective, shows its tether while latching (the warning), then drains until destroyed.
#pragma once

#include "CoreMinimal.h"
#include "Enemies/Base/EnemyBase.h"
#include "Siphon.generated.h"

class AMissionObjective;
class UAudioComponent;
class USoundBase;
class UStaticMeshComponent;

UCLASS()
class ETM_API ASiphon : public AEnemyBase
{
	GENERATED_BODY()

public:
	ASiphon();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "ETM|Siphon") void SetTarget(AMissionObjective* InTarget) { Target = InTarget; }
	UFUNCTION(BlueprintPure, Category = "ETM|Siphon") bool IsDraining() const { return bDraining; }
	UFUNCTION(BlueprintPure, Category = "ETM|Siphon") bool IsTetherVisible() const;
	/** Total integrity this Siphon has drained since it appeared. */
	UFUNCTION(BlueprintPure, Category = "ETM|Siphon") float GetDrained() const { return Drained; }

	void SimulateStep(float DeltaSeconds);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Siphon") float DrainPerSecond = 5.f;
	/** Prototype values. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Siphon|Prototype") float LatchWarningSeconds = 1.5f;
	/** Drain is applied in pulses rather than every frame, so damage events stay readable. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Siphon|Prototype") float DrainPulseSeconds = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Siphon|Prototype") float MoveSpeed = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Siphon|Prototype") float LatchRange = 180.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Siphon") TObjectPtr<USoundBase> DrainSound;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Siphon") TObjectPtr<UStaticMeshComponent> Body;
	/** Cable from the Siphon to the objective, shown from the latch warning until it dies: the shape cue. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Siphon") TObjectPtr<UStaticMeshComponent> Tether;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Siphon") TObjectPtr<UAudioComponent> DrainAudio;

protected:
	virtual void HandleStateChanged(EEnemyState NewState, EEnemyState OldState) override;
	virtual void ResolveAttack(AActor* AttackTarget, float Damage) override;

private:
	void UpdateBehavior(float DeltaSeconds);
	void UpdateTether();

	UPROPERTY(Transient) TWeakObjectPtr<AMissionObjective> Target;
	float PulseTimer = 0.f;
	float Drained = 0.f;
	bool bDraining = false;
};
