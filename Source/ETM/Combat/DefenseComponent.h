// UDefenseComponent: the RIG-6 defensive brace. Spec: bible section 5 "Defensive brace". Owned by T-0011.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DefenseComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBraceStateChanged, bool, bActive);

UCLASS(ClassGroup = (ETM), meta = (BlueprintSpawnableComponent))
class ETM_API UDefenseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDefenseComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Returns true if the brace activated. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Brace") bool TryActivateBrace();
	/** Applies brace reduction to rig damage. The objective is never protected (bible section 5). */
	UFUNCTION(BlueprintCallable, Category = "ETM|Brace") float ModifyIncomingRigDamage(float Damage);

	UFUNCTION(BlueprintPure, Category = "ETM|Brace") bool IsBraceActive() const { return ActiveRemaining > 0.f; }
	UFUNCTION(BlueprintPure, Category = "ETM|Brace") bool IsBraceReady() const { return ActiveRemaining <= 0.f && CooldownRemaining <= 0.f; }
	UFUNCTION(BlueprintPure, Category = "ETM|Brace") float GetCooldownRemaining() const { return CooldownRemaining; }
	/** Total damage absorbed this mission; feeds the "damage prevented" score (P-30). */
	UFUNCTION(BlueprintPure, Category = "ETM|Brace") float GetDamageAbsorbed() const { return DamageAbsorbed; }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Brace") float Duration = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Brace") float Cooldown = 12.0f; // starts when the field ends
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Brace", meta = (ClampMin = "0", ClampMax = "1")) float DamageReduction = 0.75f;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Brace") FOnBraceStateChanged OnBraceStateChanged;

private:
	float ActiveRemaining = 0.f;
	float CooldownRemaining = 0.f;
	float DamageAbsorbed = 0.f;
};
