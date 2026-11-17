// ALanceWalker: tall, narrow artillery unit. Walks a short way in, plants its legs and charges a lance.
// Spec: bible section 7 (320 HP; plants legs and charges a bright lance for 2 s; its 160 HP charge
// chamber is a weak point, 2 shots, and breaking it interrupts the attack; strike 35 to its designated
// target), P-02, P-09. Owned by T-0022.
#pragma once

#include "CoreMinimal.h"
#include "Enemies/Base/EnemyBase.h"
#include "LanceWalker.generated.h"

class UAudioComponent;
class USoundBase;
class UStaticMeshComponent;

UCLASS()
class ETM_API ALanceWalker : public AEnemyBase
{
	GENERATED_BODY()

public:
	ALanceWalker();
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** The designated target: the rig (default) or the objective. Targeting the objective adds the infrastructure tag. */
	UFUNCTION(BlueprintCallable, Category = "ETM|LanceWalker") void SetTarget(AActor* InTarget);
	UFUNCTION(BlueprintPure, Category = "ETM|LanceWalker") AActor* GetTarget() const { return Target.Get(); }
	UFUNCTION(BlueprintPure, Category = "ETM|LanceWalker") bool IsPlanted() const { return bPlanted; }
	UFUNCTION(BlueprintPure, Category = "ETM|LanceWalker") bool IsChamberExposed() const { return bChamberExposed; }
	/** 0 at the start of the charge, 1 at the strike. The lance grows and brightens with it (shape cue). */
	UFUNCTION(BlueprintPure, Category = "ETM|LanceWalker") float GetTelegraphProgress() const;

	void SimulateStep(float DeltaSeconds);

	static const FName ChamberPart;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|LanceWalker") float StrikeDamage = 35.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|LanceWalker") float ChargeSeconds = 2.f;
	/** Prototype values. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|LanceWalker|Prototype") float StrikeCooldown = 4.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|LanceWalker|Prototype") float MoveSpeed = 150.f;
	/** Walks this far from its entrance toward the target, then plants. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|LanceWalker|Prototype") float AdvanceDistance = 600.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|LanceWalker|Prototype") float PlantSeconds = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|LanceWalker") TObjectPtr<USoundBase> ChargeSound;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|LanceWalker") TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|LanceWalker") TObjectPtr<UStaticMeshComponent> Chamber;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|LanceWalker") TObjectPtr<UStaticMeshComponent> Lance;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|LanceWalker") TObjectPtr<UAudioComponent> TelegraphAudio;

protected:
	virtual void HandleStateChanged(EEnemyState NewState, EEnemyState OldState) override;
	virtual void ResolveAttack(AActor* AttackTarget, float Damage) override;

private:
	void UpdateBehavior(float DeltaSeconds);
	void SetChamberExposed(bool bExposed);
	AActor* ResolveTarget();

	UPROPERTY(Transient) TWeakObjectPtr<AActor> Target;
	FVector PlantPoint = FVector::ZeroVector;
	float Cooldown = 0.f;
	float PlantTimer = 0.f;
	bool bPlanted = false;
	bool bPlantPointSet = false;
	bool bChamberExposed = false;
};
