// ACradleBomber: suspended circular carrier holding an explosive pod above its target.
// Spec: bible section 7 (160 HP; pod glows and a descending tone sounds for 2 s; destroy the 80 HP pod,
// a weak point and 1 shot, or the carrier before release; impact 40 to its designated target), P-09.
// Owned by T-0022.
//
// Flies to a hover point above its target, warns for 2 s, releases (the hit lands at once), then
// re-arms with a new pod after a cooldown. Breaking the pod detonates it in the cradle and destroys
// the carrier.
#pragma once

#include "CoreMinimal.h"
#include "Enemies/Base/EnemyBase.h"
#include "CradleBomber.generated.h"

class UAudioComponent;
class USoundBase;
class UStaticMeshComponent;

UCLASS()
class ETM_API ACradleBomber : public AEnemyBase
{
	GENERATED_BODY()

public:
	ACradleBomber();
	virtual void Tick(float DeltaSeconds) override;

	/** The designated target: the rig (default) or the objective. Targeting the objective adds the infrastructure tag. */
	UFUNCTION(BlueprintCallable, Category = "ETM|CradleBomber") void SetTarget(AActor* InTarget);
	UFUNCTION(BlueprintPure, Category = "ETM|CradleBomber") AActor* GetTarget() const { return Target.Get(); }
	UFUNCTION(BlueprintPure, Category = "ETM|CradleBomber") bool HasPod() const;
	/** 0 at the start of the warning, 1 at release. The pod swells as it glows (shape cue). */
	UFUNCTION(BlueprintPure, Category = "ETM|CradleBomber") float GetTelegraphProgress() const;
	/** Descending tone (bible): pitch falls from 1.5 to 0.75 across the warning. */
	UFUNCTION(BlueprintPure, Category = "ETM|CradleBomber") float GetTelegraphTonePitch() const { return 1.5f - 0.75f * GetTelegraphProgress(); }

	void SimulateStep(float DeltaSeconds);

	static const FName PodPart;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|CradleBomber") float ImpactDamage = 40.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|CradleBomber") float WarningSeconds = 2.f;
	/** Prototype values. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|CradleBomber|Prototype") float RearmSeconds = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|CradleBomber|Prototype") float HoverHeight = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|CradleBomber|Prototype") float MoveSpeed = 300.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|CradleBomber") TObjectPtr<USoundBase> WarningSound;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|CradleBomber") TObjectPtr<UStaticMeshComponent> Carrier;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|CradleBomber") TObjectPtr<UStaticMeshComponent> Pod;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|CradleBomber") TObjectPtr<UAudioComponent> TelegraphAudio;

protected:
	virtual void HandleStateChanged(EEnemyState NewState, EEnemyState OldState) override;
	virtual void ResolveAttack(AActor* AttackTarget, float Damage) override;

private:
	void UpdateBehavior(float DeltaSeconds);
	void SetPodPresent(bool bPresent);
	AActor* ResolveTarget();

	UPROPERTY(Transient) TWeakObjectPtr<AActor> Target;
	float RearmTimer = 0.f;
	bool bPodPresent = true;
};
