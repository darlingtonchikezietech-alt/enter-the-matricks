// AXRSeatedRigPawn: RIG-6. XR origin, tracked camera, two tracked controllers, two cannons.
// Spec: bible section 4 (first-person rules, calibration), P-11. Owned by T-0007.
// Input binding (Enhanced Input, P-12 grip threshold) is T-0012; this class exposes
// BlueprintCallable entry points that the input layer calls.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "XRSeatedRigPawn.generated.h"

class UCameraComponent;
class UMotionControllerComponent;
class UCannonComponent;
class UDefenseComponent;
class UIntegrityComponent;

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
	/** Pause-menu recenter: resets HMD orientation/position to the seated origin. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Rig") void Recenter();
	/** P-11 Forward re-anchor: yaw the XR origin so the player's current facing becomes rig forward. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Rig") void ForwardReAnchor();

	/** Per-hand controller-to-cannon rotation offset, adjustable in calibration (bible section 4). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Calibration") FRotator LeftAimOffset = FRotator(-30.f, 0.f, 0.f);
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Calibration") FRotator RightAimOffset = FRotator(-30.f, 0.f, 0.f);

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
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<UDefenseComponent> Defense;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Rig") TObjectPtr<UIntegrityComponent> RigIntegrity;
};
