// AMissionObjective: the infrastructure the player protects (power-transfer equipment, railway junction).
// Spec: bible section 6 "Health model" (objective integrity 500, damage from designated attackers),
// section 11 (objective marker). Owned by T-0018.
//
// Damage arrives through the engine's ApplyDamage and is accepted only from designated attackers:
// actors tagged ETM.InfrastructureAttacker or of a class listed in DesignatedAttackerClasses.
// Cannon fire, stray rig damage and anonymous damage are all refused.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Templates/SubclassOf.h"
#include "MissionObjective.generated.h"

class UIntegrityComponent;

/** First hit after a quiet period: the HUD marker starts its under-attack cue (T-0019). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnObjectiveUnderAttack, AMissionObjective*, Objective, AActor*, Attacker);

UCLASS()
class ETM_API AMissionObjective : public AActor
{
	GENERATED_BODY()

public:
	AMissionObjective();

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	/** True if this actor may damage the objective. */
	UFUNCTION(BlueprintPure, Category = "ETM|Objective") bool IsDesignatedAttacker(const AActor* Candidate) const;

	/** Where the world marker sits (bible section 11). */
	UFUNCTION(BlueprintPure, Category = "ETM|Objective") FVector GetMarkerLocation() const;
	/** True for UnderAttackHoldSeconds after accepted damage. The HUD polls this for the marker state. */
	UFUNCTION(BlueprintPure, Category = "ETM|Objective") bool IsUnderAttack() const;
	/** Location of the most recent accepted attacker, for the directional warning. Zero if none yet. */
	UFUNCTION(BlueprintPure, Category = "ETM|Objective") FVector GetLastAttackerLocation() const { return LastAttackerLocation; }
	UFUNCTION(BlueprintPure, Category = "ETM|Objective") float GetIntegrityNormalized() const;
	/** Damage attempts refused because the source was not a designated attacker. For debugging and tests. */
	UFUNCTION(BlueprintPure, Category = "ETM|Objective") int32 GetRejectedDamageCount() const { return RejectedDamageCount; }

	/** Starting objective integrity (bible section 6). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Objective", meta = (ClampMin = "1")) float MaxIntegrity = 500.f;
	/** Classes allowed to damage this objective in addition to tagged actors. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Objective") TArray<TSubclassOf<AActor>> DesignatedAttackerClasses;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Objective", meta = (ClampMin = "0")) float UnderAttackHoldSeconds = 1.5f;
	/** Register with the mission state as the active objective at BeginPlay. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Objective") bool bRegisterOnBeginPlay = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Objective") TObjectPtr<USceneComponent> SceneRoot;
	/** Move this in the level to place the marker above the equipment. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Objective") TObjectPtr<USceneComponent> MarkerAnchor;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Objective") TObjectPtr<UIntegrityComponent> Integrity;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Objective") FOnObjectiveUnderAttack OnUnderAttack;

protected:
	virtual void BeginPlay() override;

private:
	double LastDamageTime = -1.0e9;
	FVector LastAttackerLocation = FVector::ZeroVector;
	int32 RejectedDamageCount = 0;
};
