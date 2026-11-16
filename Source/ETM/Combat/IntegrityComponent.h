// UIntegrityComponent: health pool for the rig (300) or a mission objective (500).
// Spec: bible section 6 "Health model", P-05. Owned by T-0011.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "IntegrityComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnIntegrityChanged, float, Current, float, Max, float, Delta);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnIntegrityDepleted);

UCLASS(ClassGroup = (ETM), meta = (BlueprintSpawnableComponent))
class ETM_API UIntegrityComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UIntegrityComponent();

	/** If this is the rig, damage first passes through the owner's UDefenseComponent (brace). */
	UFUNCTION(BlueprintCallable, Category = "ETM|Integrity") float ApplyIntegrityDamage(float Amount);
	UFUNCTION(BlueprintCallable, Category = "ETM|Integrity") void Repair(float Amount);
	UFUNCTION(BlueprintCallable, Category = "ETM|Integrity") void ResetTo(float Value);

	UFUNCTION(BlueprintPure, Category = "ETM|Integrity") float GetCurrent() const { return Current; }
	UFUNCTION(BlueprintPure, Category = "ETM|Integrity") float GetMax() const { return MaxIntegrity; }
	UFUNCTION(BlueprintPure, Category = "ETM|Integrity") bool IsDepleted() const { return Current <= 0.f; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Integrity") float MaxIntegrity = 300.f;
	/** True for RIG-6 (brace applies); false for objectives (brace never protects them). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Integrity") bool bIsRig = true;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Integrity") FOnIntegrityChanged OnIntegrityChanged;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Integrity") FOnIntegrityDepleted OnDepleted;

protected:
	virtual void BeginPlay() override;

private:
	float Current = 0.f;
};
