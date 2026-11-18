// UCannonFeedbackComponent: everything a shot looks, sounds and feels like. Cosmetic only.
// Spec: bible section 5 "Shot presentation" (7 parts), section 11 (no camera shake or recoil on the
// camera; haptic strength incl. off; reduced flash), section 12 "Weapon effects". Owned by T-0048 (for T-0015).
//
// Attach one to each UCannonComponent. It listens to the cannon's OnFired and plays, per shot:
//   1. barrel mechanism movement and 5. mount recoil  -> a kick on its own recoil pivot (never the camera)
//   2. amber-white muzzle discharge                   -> a short-lived flash mesh at the muzzle
//   3. thick luminous tracer                          -> a pooled cylinder that races from muzzle to end
//   4. impact flash (debris hook)                     -> a pooled flash at the impact, plus OnImpact for VFX
//   6. short haptic pulse                             -> on this cannon's hand, scaled by HapticStrength
//   7. report                                         -> FireSound at the muzzle
// Placeholder geometry, the engine's unlit emissive material and an engine tone stand in for art and
// audio (T-0034, T-0036). Hit resolution stays in UCannonComponent; nothing here affects gameplay.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Weapons/CannonComponent.h"
#include "CannonFeedbackComponent.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class USoundBase;
class UStaticMesh;
class UStaticMeshComponent;

/** For VFX (sparks, debris) once Niagara assets exist. bWeakPoint lets them look different on weak points. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnCannonImpactFeedback, FVector, Location, bool, bWeakPoint, bool, bBlocked);

/** The last haptic request, kept for tests and debugging. */
USTRUCT(BlueprintType)
struct FCannonHapticPulse
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly, Category = "Haptics") float Amplitude = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Haptics") float Frequency = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Haptics") float Duration = 0.f;
	UPROPERTY(BlueprintReadOnly, Category = "Haptics") bool bLeftHand = false;
};

UCLASS(ClassGroup = (ETM), meta = (BlueprintSpawnableComponent))
class ETM_API UCannonFeedbackComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UCannonFeedbackComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Plays the full presentation for one shot. Bound to the cannon's OnFired; public so tests can call it. */
	UFUNCTION() void HandleShot(const FCannonShot& Shot);
	UFUNCTION() void HandleOverheatChanged(bool bOverheated);

	/** Advances the animations. TickComponent calls this; it is public so tests can step time directly. */
	void AdvanceFeedback(float DeltaTime);

	UFUNCTION(BlueprintPure, Category = "ETM|Feedback") FVector GetRecoilOffset() const;
	UFUNCTION(BlueprintPure, Category = "ETM|Feedback") bool IsMuzzleFlashVisible() const;
	/** Muzzle flashes shown so far (rapid fire shows one every MuzzleFlashMinInterval, not one per shot). */
	UFUNCTION(BlueprintPure, Category = "ETM|Feedback") int32 GetMuzzleFlashesShown() const { return MuzzleFlashesShown; }
	/** Where the muzzle flash sits, in this component's frame. */
	UFUNCTION(BlueprintPure, Category = "ETM|Feedback") FVector GetMuzzleFlashOffset() const;
	UFUNCTION(BlueprintPure, Category = "ETM|Feedback") int32 GetActiveTracerCount() const;
	UFUNCTION(BlueprintPure, Category = "ETM|Feedback") int32 GetActiveImpactCount() const;
	UFUNCTION(BlueprintPure, Category = "ETM|Feedback") FCannonHapticPulse GetLastHaptic() const { return LastHaptic; }
	UFUNCTION(BlueprintPure, Category = "ETM|Feedback") int32 GetReportsPlayed() const { return ReportsPlayed; }
	/** Visual components created at BeginPlay. Flat during play means nothing is allocated per shot. */
	UFUNCTION(BlueprintPure, Category = "ETM|Feedback") int32 GetCreatedVisualCount() const { return CreatedVisualCount; }
	/** The visible part of tracer Index, in world space. Returns false if that tracer is idle. */
	bool GetTracerSegment(int32 Index, FVector& OutTail, FVector& OutHead) const;

	// ---- Settings (the accessibility settings, T-0037, write these) ----
	/** 0 turns haptics off (bible section 11). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Feedback", meta = (ClampMin = "0", ClampMax = "1")) float HapticStrength = 1.f;
	/** Smaller, shorter muzzle and impact flashes (bible section 11). Tracers stay: they carry aim information. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Feedback") bool bReducedFlash = false;
	/** Which controller gets this cannon's haptics. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Feedback") bool bLeftHand = false;

	// ---- Tuning (prototype values; tune in the M1 playtest) ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Recoil") float RecoilKickCm = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Recoil") float RecoilPitchDeg = 2.5f;
	/** Time for the barrel to settle back. At 10 shots/s (P-34) the kicks overlap into a steady shudder. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Recoil") float RecoilRecoverSeconds = 0.15f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Flash") float MuzzleFlashSeconds = 0.05f;
	/**
	 * At 10 shots/s a flash on every shot strobes beside the player's eyes (owner headset test, T-0056; bible
	 * section 11: essential weapon effects outside the central sightline). The first shot of a burst always
	 * flashes; after that at most one flash per this many seconds.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Flash", meta = (ClampMin = "0")) float MuzzleFlashMinInterval = 0.25f;
	/** How far past the muzzle the flash sits, away from the face. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Flash") float MuzzleFlashForwardCm = 40.f;
	/** Flash mesh scale; reduced flash uses half. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Flash") float MuzzleFlashScale = 0.35f;
	/** Brightness of the muzzle flash relative to the discharge colour (tracers and impacts use 4). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Flash") float MuzzleFlashBrightness = 1.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Flash") float ImpactFlashSeconds = 0.1f;
	/** Head reaches the end at half the lifetime; the tail follows. "Rapidly animated cosmetic tracer" (bible section 5). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Tracer") float TracerSeconds = 0.08f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Tracer") float TracerRadiusCm = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Tracer", meta = (ClampMin = "1", ClampMax = "8")) int32 TracerPoolSize = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Flash", meta = (ClampMin = "1", ClampMax = "8")) int32 ImpactPoolSize = 3;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback") FLinearColor DischargeColor = FLinearColor(1.f, 0.62f, 0.22f); // amber-white
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Haptics") float ShotHapticAmplitude = 0.8f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Haptics") float ShotHapticFrequency = 0.6f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Haptics") float ShotHapticSeconds = 0.06f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Haptics") float OverheatHapticSeconds = 0.25f;

	// ---- Assets (placeholders until T-0034/T-0036) ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Assets") TObjectPtr<UStaticMesh> BarrelMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Assets") TObjectPtr<UStaticMesh> FlashMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Assets") TObjectPtr<UStaticMesh> TracerMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Assets") TObjectPtr<UMaterialInterface> DischargeMaterial;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Assets") TObjectPtr<USoundBase> FireSound;
	/** The placeholder tone is high; play it low so it reads as a report, not a beep. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Assets") float FireSoundPitch = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Feedback|Assets") TObjectPtr<USoundBase> OverheatSound;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Feedback") FOnCannonImpactFeedback OnImpact;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FTracer
	{
		TObjectPtr<UStaticMeshComponent> Mesh = nullptr;
		FVector Start = FVector::ZeroVector;
		FVector End = FVector::ZeroVector;
		float Age = -1.f; // < 0: idle
	};
	struct FImpact
	{
		TObjectPtr<UStaticMeshComponent> Mesh = nullptr;
		float Age = -1.f;
		float Size = 1.f;
	};

	UStaticMeshComponent* MakeVisual(const TCHAR* Name, UStaticMesh* Mesh, USceneComponent* Parent, bool bWorldSpace);
	void PlayHaptic(float Amplitude, float Duration);
	void StopHaptic();
	void UpdateTracer(FTracer& Tracer) const;

	UPROPERTY(Transient) TObjectPtr<UCannonComponent> Cannon;
	UPROPERTY(Transient) TObjectPtr<USceneComponent> RecoilPivot;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Barrel;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> MuzzleFlash;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> DischargeInstance;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> MuzzleFlashInstance;
	/** Keeps the pooled meshes alive for the garbage collector. */
	UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> PooledMeshes;

	TArray<FTracer> Tracers;
	TArray<FImpact> Impacts;
	FCannonHapticPulse LastHaptic;
	float RecoilTime = -1.f;
	float FlashAge = -1.f;
	float FlashCooldown = 0.f;
	int32 MuzzleFlashesShown = 0;
	float HapticRemaining = 0.f;
	int32 NextTracer = 0;
	int32 NextImpact = 0;
	int32 ReportsPlayed = 0;
	int32 CreatedVisualCount = 0;
};
