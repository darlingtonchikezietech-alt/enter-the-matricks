// UDefenseComponent: the RIG-6 defensive brace. Spec: bible section 5 "Defensive brace". Owned by T-0011.
// Timing and damage maths live in Combat/BraceModel.h; this component adds events and ticking.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Combat/BraceModel.h"
#include "DefenseComponent.generated.h"

/** bActive is true when the field goes up and false when it drops. Drive the activation cue from this. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBraceStateChanged, bool, bActive);
/** The cooldown has finished and the brace can be used again (HUD readiness indicator). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnBraceReady);
/** The field reduced a hit. Absorbed is the damage removed; Passed is what reached the rig. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnBraceAbsorbedDamage, float, Absorbed, float, Passed);

UCLASS(ClassGroup = (ETM), meta = (BlueprintSpawnableComponent))
class ETM_API UDefenseComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDefenseComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Returns true if the brace activated. Fails while the field is up or cooling down. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Brace") bool TryActivateBrace();
	/** Applies brace reduction to rig damage and returns what gets through. The objective is never protected (bible section 5). */
	UFUNCTION(BlueprintCallable, Category = "ETM|Brace") float ModifyIncomingRigDamage(float Damage);
	/** Drops the field and clears the cooldown, for checkpoint retry. Keeps the absorbed total unless bClearAbsorbed is set. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Brace") void ResetBrace(bool bClearAbsorbed = false);

	UFUNCTION(BlueprintPure, Category = "ETM|Brace") bool IsBraceActive() const { return ETM::IsBraceActive(State); }
	UFUNCTION(BlueprintPure, Category = "ETM|Brace") bool IsBraceReady() const { return ETM::IsBraceReady(State); }
	UFUNCTION(BlueprintPure, Category = "ETM|Brace") float GetActiveRemaining() const { return State.ActiveRemaining; }
	UFUNCTION(BlueprintPure, Category = "ETM|Brace") float GetCooldownRemaining() const { return State.CooldownRemaining; }
	/** 0 while the field is up or just dropped, rising to 1 when the brace is ready again. */
	UFUNCTION(BlueprintPure, Category = "ETM|Brace") float GetReadiness() const;
	/** Total damage absorbed this mission; feeds the "damage prevented" score (P-30). */
	UFUNCTION(BlueprintPure, Category = "ETM|Brace") float GetDamageAbsorbed() const { return State.DamageAbsorbed; }

	/** Advances the brace timers. TickComponent calls this; it is public so tests can step time without a world. */
	void AdvanceBrace(float DeltaTime);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Brace", meta = (ClampMin = "0")) float Duration = 2.0f;
	/** Starts when the field ends. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Brace", meta = (ClampMin = "0")) float Cooldown = 12.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Brace", meta = (ClampMin = "0", ClampMax = "1")) float DamageReduction = 0.75f;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Brace") FOnBraceStateChanged OnBraceStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Brace") FOnBraceReady OnBraceReady;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Brace") FOnBraceAbsorbedDamage OnBraceAbsorbedDamage;

private:
	ETM::FBraceTuning MakeTuning() const;

	ETM::FBraceState State;
};
