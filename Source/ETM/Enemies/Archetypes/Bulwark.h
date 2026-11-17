// ABulwark: broad shield carrier with a narrow central body. Walks to the objective and slams it.
// Spec: bible section 7 (480 HP; shield x0.1; two 80 HP side nodes, weak points x2; breaking either node
// drops the shield and opens the body; no ranged attack; slams the objective for 30 every 4 s), P-02, P-09.
// Owned by T-0022.
#pragma once

#include "CoreMinimal.h"
#include "Enemies/Base/EnemyBase.h"
#include "Bulwark.generated.h"

class AMissionObjective;
class UAudioComponent;
class USoundBase;
class UStaticMeshComponent;

UCLASS()
class ETM_API ABulwark : public AEnemyBase
{
	GENERATED_BODY()

public:
	ABulwark();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "ETM|Bulwark") void SetTarget(AMissionObjective* InTarget) { Target = InTarget; }
	UFUNCTION(BlueprintPure, Category = "ETM|Bulwark") bool IsShieldUp() const { return !bShieldDropped; }
	/** 0 at the start of the slam wind-up, 1 at impact. Drives the shield-lift pose (shape cue). */
	UFUNCTION(BlueprintPure, Category = "ETM|Bulwark") float GetTelegraphProgress() const;

	void SimulateStep(float DeltaSeconds);

	static const FName ShieldPart;
	static const FName LeftNodePart;
	static const FName RightNodePart;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Bulwark") float SlamDamage = 30.f;
	/** Slam cycle length while in contact (bible: every 4 s): wind-up plus recovery. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Bulwark") float SlamCycleSeconds = 4.f;
	/** Prototype values. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Bulwark|Prototype") float SlamWindUpSeconds = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Bulwark|Prototype") float MoveSpeed = 120.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Bulwark|Prototype") float ContactRange = 200.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Bulwark") TObjectPtr<USoundBase> SlamWindUpSound;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Bulwark") TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Bulwark") TObjectPtr<USceneComponent> ShieldPivot;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Bulwark") TObjectPtr<UStaticMeshComponent> Shield;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Bulwark") TObjectPtr<UStaticMeshComponent> LeftNode;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Bulwark") TObjectPtr<UStaticMeshComponent> RightNode;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Bulwark") TObjectPtr<UAudioComponent> TelegraphAudio;

protected:
	virtual void HandleStateChanged(EEnemyState NewState, EEnemyState OldState) override;
	virtual void HandlePartBroken(const FEnemyPart& Part) override;
	virtual void ResolveAttack(AActor* AttackTarget, float Damage) override;

private:
	void UpdateBehavior(float DeltaSeconds);
	void SetShieldUp(bool bUp);

	UPROPERTY(Transient) TWeakObjectPtr<AMissionObjective> Target;
	float SlamCooldown = 0.f;
	bool bShieldDropped = false;
};
