// UIntegrityComponent: health pool for the rig (300) or a mission objective (500).
// Spec: bible section 6 "Health model" and "Checkpoints", P-04, P-05. Owned by T-0011.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IntegrityComponent.generated.h"

class UDefenseComponent;
class UDamageType;
class AController;

/** Any change to the pool. Delta is negative for damage, positive for repair. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnIntegrityChanged, float, Current, float, Max, float, Delta);
/** A hit landed. Applied is the integrity lost; Prevented is what the brace or difficulty scaling removed. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnIntegrityDamaged, float, Applied, float, Prevented, AActor*, DamageCauser);
/** Integrity reached zero: mission failure. Fires once until the pool is restored with ResetTo. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIntegrityDepleted);

UCLASS(ClassGroup = (ETM), meta = (BlueprintSpawnableComponent))
class ETM_API UIntegrityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIntegrityComponent();

	/**
	 * Applies damage and returns the integrity actually lost.
	 * For the rig, damage first passes through the brace. Does nothing once depleted.
	 */
	UFUNCTION(BlueprintCallable, Category = "ETM|Integrity") float ApplyIntegrityDamage(float Amount, AActor* DamageCauser = nullptr);
	/** Adds integrity, capped at the maximum (bible section 6). Does not revive a depleted pool. Returns the amount added. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Integrity") float Repair(float Amount);
	/** Sets the pool directly (level start, checkpoint restore). Clears the depleted state if Value is above zero. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Integrity") void ResetTo(float Value);
	/** Fills the pool to its maximum. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Integrity") void ResetToMax() { ResetTo(MaxIntegrity); }
	/** P-04 boss checkpoint preset: restores to the saved value, but never below MinFraction of the maximum. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Integrity") void RestoreCheckpoint(float SavedValue, float MinFraction);

	UFUNCTION(BlueprintPure, Category = "ETM|Integrity") float GetCurrent() const { return Current; }
	UFUNCTION(BlueprintPure, Category = "ETM|Integrity") float GetMax() const { return MaxIntegrity; }
	UFUNCTION(BlueprintPure, Category = "ETM|Integrity") float GetNormalized() const { return MaxIntegrity > 0.f ? Current / MaxIntegrity : 0.f; }
	UFUNCTION(BlueprintPure, Category = "ETM|Integrity") bool IsDepleted() const { return bDepleted; }

	/** Overrides the brace used for rig damage. By default the owner's UDefenseComponent is found at BeginPlay. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Integrity") void SetDefenseComponent(UDefenseComponent* InDefense) { Defense = InDefense; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Integrity", meta = (ClampMin = "1")) float MaxIntegrity = 300.f;
	/** True for RIG-6 (brace applies); false for objectives (brace never protects them). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Integrity") bool bIsRig = true;
	/** Multiplier on incoming damage before the brace. 1 on Standard; Assisted lowers it (bible section 6 "Difficulty"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Integrity", meta = (ClampMin = "0")) float IncomingDamageScale = 1.f;
	/** Route the owning actor's engine damage (ApplyDamage, ApplyPointDamage) into this pool. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Integrity") bool bBindToOwnerDamage = true;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Integrity") FOnIntegrityChanged OnIntegrityChanged;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Integrity") FOnIntegrityDamaged OnDamaged;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Integrity") FOnIntegrityDepleted OnDepleted;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION() void HandleOwnerDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser);

	UPROPERTY(Transient) TObjectPtr<UDefenseComponent> Defense;
	float Current = 0.f;
	bool bDepleted = false;
};
