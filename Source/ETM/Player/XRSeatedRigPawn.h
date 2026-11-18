// AXRSeatedRigPawn: RIG-6. XR origin, tracked camera, two tracked controllers, two cannons.
// Spec: bible section 4 (first-person rules, calibration), P-11. Owned by T-0007.
// Input binding (Enhanced Input, P-12 grip threshold) is T-0012; this class exposes
// BlueprintCallable entry points that the input layer and the pause menu call.
//
// Camera rule: the headset drives the camera. Nothing in this class writes to the camera.
// Calibration moves only VROrigin (the tracking-space anchor), and only when asked.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "XRSeatedRigPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UCannonComponent;
class UCannonFeedbackComponent;
class UHUDPresenterComponent;
class UDefenseComponent;
class UIntegrityComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRigCalibrationChanged);

UCLASS()
class ETM_API AXRSeatedRigPawn : public APawn
{
	GENERATED_BODY()

public:
	AXRSeatedRigPawn();
	virtual void Tick(float DeltaSeconds) override;

	UFUNCTION(BlueprintCallable, Category = "ETM|Rig") void SetTrigger(bool bLeft, bool bHeld);
	UFUNCTION(BlueprintCallable, Category = "ETM|Rig") void SetVent(bool bLeft, bool bHeld);
	UFUNCTION(BlueprintCallable, Category = "ETM|Rig") bool ActivateBrace();

	/**
	 * Seated recenter (pause menu): places the player's head at the seat design eye point, facing rig forward.
	 * Returns false if no head pose was available.
	 */
	UFUNCTION(BlueprintCallable, Category = "ETM|Calibration") bool Recenter();
	/**
	 * P-11 Forward re-anchor (pause menu): the player's current facing becomes rig forward.
	 * The head does not move within the rig. Returns false if no head pose was available.
	 */
	UFUNCTION(BlueprintCallable, Category = "ETM|Calibration") bool ForwardReAnchor();

	/** Sets one hand's controller-to-cannon offset. The value is clamped; roll is ignored. Not saved until SaveCalibration. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Calibration") void SetAimOffset(bool bLeft, FRotator Offset);
	UFUNCTION(BlueprintPure, Category = "ETM|Calibration") FRotator GetAimOffset(bool bLeft) const { return bLeft ? LeftAimOffset : RightAimOffset; }
	/** Restores both offsets to the class defaults. Not saved until SaveCalibration. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Calibration") void ResetAimOffsets();
	/** Writes the offsets to the calibration save slot. Returns false if the write failed. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Calibration") bool SaveCalibration() const;
	/** Reads the offsets from the calibration save slot. Returns false if there is no usable save. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Calibration") bool LoadCalibration();

	/** True once the seat has been calibrated at least once this session. */
	UFUNCTION(BlueprintPure, Category = "ETM|Calibration") bool IsSeatCalibrated() const { return bSeatCalibrated; }

	/** Fired after a recenter, a re-anchor, or an offset change, so UI and previews can refresh. */
	UPROPERTY(BlueprintAssignable, Category = "ETM|Calibration") FOnRigCalibrationChanged OnCalibrationChanged;

	/** Where the seated player's eyes belong, in rig space (cm). Tune with the T-0008 blockout. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Calibration") FVector DesignEyeLocation = FVector(0.f, 0.f, 110.f); // 10 cm lower after the owner's headset test (T-0055)
	/** Calibrate the seat automatically on the first frame with a tracked headset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Calibration") bool bAutoCalibrateOnStart = true;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<USceneComponent> VROrigin;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<UCameraComponent> Camera;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<UMotionControllerComponent> LeftController;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<UMotionControllerComponent> RightController;
	/** Fixed frame mount. Never moves the camera: recoil animates cannons, not the view. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<USceneComponent> RigFrame;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<USceneComponent> LeftMount;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<USceneComponent> RightMount;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<UCannonComponent> LeftCannon;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<UCannonComponent> RightCannon;
	/** Shot presentation for each cannon: recoil, flash, tracer, impact, haptics, report. Cosmetic only. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<UCannonFeedbackComponent> LeftFeedback;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<UCannonFeedbackComponent> RightFeedback;
	/** The diegetic HUD on the frame, housings and in the world (bible section 11). */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<UHUDPresenterComponent> HUD;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<UDefenseComponent> Defense;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<UIntegrityComponent> RigIntegrity;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * Head pose in tracking space (relative to VROrigin). Returns false when no headset pose is available.
	 * Virtual so tests and non-VR previews can supply a pose.
	 */
	virtual bool GetHeadPoseInTracking(FTransform& OutHead) const;

	/** Per-hand controller-to-cannon rotation offset (bible section 4). Change through SetAimOffset. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Calibration") FRotator LeftAimOffset = FRotator(-30.f, 0.f, 0.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Calibration") FRotator RightAimOffset = FRotator(-30.f, 0.f, 0.f);

private:
	void UpdateCannonAim(const UMotionControllerComponent* Hand, UCannonComponent* Cannon, const FRotator& Offset) const;
	void HandleSystemRecenter();

	bool bSeatCalibrated = false;
	FDelegateHandle SystemRecenterHandle;
};
