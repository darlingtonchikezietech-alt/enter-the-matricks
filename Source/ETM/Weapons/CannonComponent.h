// UCannonComponent: one HCP-40 pulse cannon. Two instances live on the rig.
// Spec: PRODUCT_BIBLE.md sections 4 (aiming envelope) and 5 (weapon, heat). Owned by task T-0009.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Weapons/CannonHeatModel.h"
#include "CannonComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCannonFired, FVector, MuzzleLocation, FVector, ImpactLocation, bool, bHit);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnCannonHeatChanged, float, HeatNormalized, bool, bOverheated);

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

	UFUNCTION(BlueprintPure, Category = "ETM|Cannon") float GetHeatNormalized() const { return Heat.Heat / FMath::Max(1.0f, Tuning.HeatCapacity); }
	UFUNCTION(BlueprintPure, Category = "ETM|Cannon") bool IsOverheated() const { return Heat.bOverheated; }
	UFUNCTION(BlueprintPure, Category = "ETM|Cannon") bool IsVenting() const { return bVentHeld && !Heat.bOverheated; }
	/** The barrel's actual (clamped) direction. HUD must show this, not the controller ray (bible section 4). */
	UFUNCTION(BlueprintPure, Category = "ETM|Cannon") FVector GetBarrelDirection() const { return GetForwardVector(); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Cannon") FCannonTuning Tuning;
	/** Offset from this component's origin to the muzzle, in local space. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Cannon") FVector MuzzleOffset = FVector(120.f, 0.f, 0.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Cannon") TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Cannon") FOnCannonFired OnFired;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Cannon") FOnCannonHeatChanged OnHeatChanged;

protected:
	virtual void BeginPlay() override;

private:
	void UpdateServo(float DeltaTime);
	void FireShot();
	float ResolveMultiplier(const FHitResult& Hit) const;

	ETM::FHeatState Heat;
	ETM::FHeatTuning HeatTuning;
	FVector DesiredAimWorld = FVector::ForwardVector;
	bool bTriggerHeld = false;
	bool bVentHeld = false;
	bool bWasOverheated = false;
};
