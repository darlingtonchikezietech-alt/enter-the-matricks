// UCannonComponent: one HCP-40 pulse cannon. Two instances live on the rig.
// Spec: PRODUCT_BIBLE.md sections 4 (aiming envelope) and 5 (weapon, heat). Owned by task T-0009.
//
// Each instance owns its own aim, cadence and heat; nothing is shared between the two cannons.
// Heat maths: Weapons/CannonHeatModel.h. Aim maths: Weapons/CannonAimModel.h.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Weapons/CannonHeatModel.h"
#include "CannonComponent.generated.h"

/** Everything listeners need about one shot: VFX and audio (T-0015), HUD (T-0019), accuracy and weak-point stats. */
USTRUCT(BlueprintType)
struct FCannonShot
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Shot") FVector MuzzleLocation = FVector::ZeroVector;
	/** Impact point, or the end of the 60 m trace on a miss. */
	UPROPERTY(BlueprintReadOnly, Category = "Shot") FVector EndLocation = FVector::ZeroVector;
	/** The shot struck something other than the rig itself. */
	UPROPERTY(BlueprintReadOnly, Category = "Shot") bool bHit = false;
	/** The shot was stopped by the rig frame or by geometry between the barrel root and the muzzle. Deals no damage. */
	UPROPERTY(BlueprintReadOnly, Category = "Shot") bool bBlocked = false;
	UPROPERTY(BlueprintReadOnly, Category = "Shot") bool bWeakPoint = false;
	UPROPERTY(BlueprintReadOnly, Category = "Shot") float Multiplier = 0.f;
	/** Damage sent to the hit actor (base damage x multiplier). Zero on a miss, a block, or an immune surface. */
	UPROPERTY(BlueprintReadOnly, Category = "Shot") float Damage = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Shot") TObjectPtr<AActor> HitActor = nullptr;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCannonFired, const FCannonShot&, Shot);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCannonHeatChanged, float, HeatNormalized, bool, bOverheated);
/** bOverheated is true when the cannon locks and false when heat has fallen to the unlock threshold. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCannonOverheatChanged, bool, bOverheated);
/** bVenting is true while the vent is actually open. It stays false during an overheat lock (P-01). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCannonVentingChanged, bool, bVenting);

USTRUCT(BlueprintType)
struct FCannonTuning
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire") float FireRate = 4.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire") float Damage = 40.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire") float WeakPointMultiplier = 2.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire") float ArmorMultiplier = 0.25f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Fire") float MaxRangeCm = 6000.0f; // P-18: 60 m, no falloff

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float HeatCapacity = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float HeatPerShot = 9.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float PassiveCooling = 18.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float PassiveCoolingDelay = 0.4f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float VentCooling = 40.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Heat") float OverheatUnlockThreshold = 35.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim") float YawLimitDeg = 65.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim") float PitchMinDeg = -30.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim") float PitchMaxDeg = 45.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Aim") float ServoSpeedDegPerSec = 360.0f; // prototype; tune in M1

	ETM::FHeatTuning ToHeatTuning() const
	{
		ETM::FHeatTuning T;
		T.FireRate = FireRate; T.HeatCapacity = HeatCapacity; T.HeatPerShot = HeatPerShot;
		T.PassiveCooling = PassiveCooling; T.PassiveCoolingDelay = PassiveCoolingDelay;
		T.VentCooling = VentCooling; T.OverheatUnlockThreshold = OverheatUnlockThreshold;
		return T;
	}
};

/** Component tags used on hit components to pick the damage multiplier (P-02). */
namespace ETMTags
{
	static const FName WeakPoint(TEXT("ETM.WeakPoint"));
	static const FName Armor(TEXT("ETM.Armor"));
	static const FName Immune(TEXT("ETM.Immune"));
}

UCLASS(ClassGroup = (ETM), meta = (BlueprintSpawnableComponent))
class ETM_API UCannonComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCannonComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** World-space direction the player's controller is pointing (after calibration offset). */
	UFUNCTION(BlueprintCallable, Category = "ETM|Cannon") void SetAimDirection(const FVector& WorldDirection);
	UFUNCTION(BlueprintCallable, Category = "ETM|Cannon") void SetTriggerHeld(bool bHeld) { bTriggerHeld = bHeld; }
	UFUNCTION(BlueprintCallable, Category = "ETM|Cannon") void SetVentHeld(bool bHeld) { bVentHeld = bHeld; }
	/** Clears heat, lock and cadence (level start, checkpoint retry). Input state is left alone. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Cannon") void ResetHeat();

	UFUNCTION(BlueprintPure, Category = "ETM|Cannon") float GetHeatNormalized() const { return Heat.Heat / FMath::Max(1.0f, Tuning.HeatCapacity); }
	UFUNCTION(BlueprintPure, Category = "ETM|Cannon") bool IsOverheated() const { return Heat.bOverheated; }
	/** True only while the vent is actually open: vent held and the cannon not locked (P-01). */
	UFUNCTION(BlueprintPure, Category = "ETM|Cannon") bool IsVenting() const { return bVentHeld && !Heat.bOverheated; }
	/** The barrel's actual (clamped) direction. HUD must show this, not the controller ray (bible section 4). */
	UFUNCTION(BlueprintPure, Category = "ETM|Cannon") FVector GetBarrelDirection() const { return GetForwardVector(); }
	/** True while the controller points outside the envelope and the barrel is held at a mechanical limit. */
	UFUNCTION(BlueprintPure, Category = "ETM|Cannon") bool IsAimClamped() const { return bAimClamped; }
	UFUNCTION(BlueprintPure, Category = "ETM|Cannon") FVector GetMuzzleLocation() const { return GetComponentTransform().TransformPosition(MuzzleOffset); }
	/**
	 * Where a shot fired now would land, using the same obstruction rules as a real shot. For the aim marker.
	 * Returns false if nothing is in range; OutPoint is then the end of the trace.
	 */
	UFUNCTION(BlueprintCallable, Category = "ETM|Cannon") bool GetBarrelImpactPoint(FVector& OutPoint) const;
	/** The most recent shot. Valid after the first OnFired. */
	UFUNCTION(BlueprintPure, Category = "ETM|Cannon") const FCannonShot& GetLastShot() const { return LastShot; }
	UFUNCTION(BlueprintPure, Category = "ETM|Cannon") int32 GetShotsFired() const { return ShotsFired; }

	/** P-02 multiplier for a hit component: immune 0, weak point x2, armor x0.25, anything else x1. */
	static float ResolveMultiplier(const UPrimitiveComponent* HitComponent, const FCannonTuning& InTuning);

	/** Advances servo, cadence and heat. TickComponent calls this; it is public so tests can step time directly. */
	void AdvanceCannon(float DeltaTime);

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Cannon") FCannonTuning Tuning;
	/** Offset from this component's origin (the barrel root) to the muzzle, in local space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Cannon") FVector MuzzleOffset = FVector(120.f, 0.f, 0.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Cannon") TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Cannon") FOnCannonFired OnFired;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Cannon") FOnCannonHeatChanged OnHeatChanged;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Cannon") FOnCannonOverheatChanged OnOverheatChanged;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Cannon") FOnCannonVentingChanged OnVentingChanged;

protected:
	virtual void OnRegister() override;

private:
	void UpdateServo(float DeltaTime);
	void FireShot();
	/** Runs the obstruction check and the muzzle trace. Fills everything in OutShot except damage. */
	void TraceShot(FCannonShot& OutShot, FHitResult& OutHit) const;

	ETM::FHeatState Heat;
	FCannonShot LastShot;
	FVector DesiredAimWorld = FVector::ForwardVector;
	int32 ShotsFired = 0;
	bool bTriggerHeld = false;
	bool bVentHeld = false;
	bool bAimClamped = false;
	bool bWasOverheated = false;
	bool bWasVenting = false;
};
