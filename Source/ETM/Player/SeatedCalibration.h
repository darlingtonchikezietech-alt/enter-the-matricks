// Seated calibration maths for RIG-6. Pure functions, no world access, so they can be
// covered by automation tests (Player/Tests/SeatedCalibrationTest.cpp).
// Spec: bible section 4 (seated calibration, controller-to-cannon offsets), P-11. Owned by T-0007.
//
// Frames used here:
//   tracking space : what the headset reports (head pose relative to the XR origin component)
//   rig space      : the pawn root; +X is rig forward, and the seat design eye point is fixed in it
// The XR origin component's relative transform maps tracking space into rig space. Calibration
// only ever changes that transform: yaw about the vertical axis plus a translation. The camera
// itself is never written to.
#pragma once

#include "CoreMinimal.h"

namespace ETMSeated
{
	/** Limits for the per-hand controller-to-cannon offset (degrees). Roll is always zero. */
	constexpr float AimOffsetPitchMin = -75.f;
	constexpr float AimOffsetPitchMax = 15.f;
	constexpr float AimOffsetYawLimit = 30.f;

	/** Below this horizontal length the head is looking almost straight up or down and its yaw is not meaningful. */
	constexpr float MinHorizontalForward = 0.2f;

	/**
	 * Yaw (degrees) of the head's forward direction projected on the horizontal plane.
	 * Returns false if the head is looking too close to vertical to tell.
	 */
	inline bool GetHeadYaw(const FQuat& HeadRotation, float& OutYawDeg)
	{
		const FVector Forward = HeadRotation.GetForwardVector();
		const FVector2D Flat(Forward.X, Forward.Y);
		if (Flat.SizeSquared() < FMath::Square(MinHorizontalForward))
		{
			return false;
		}
		OutYawDeg = FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X));
		return true;
	}

	/** Where the head currently is in rig space. */
	inline FVector HeadLocationInRig(const FTransform& OriginInRig, const FVector& HeadLocationInTracking)
	{
		return OriginInRig.TransformPositionNoScale(HeadLocationInTracking);
	}

	/**
	 * Builds the XR origin transform (in rig space) that puts the head at TargetHeadInRig,
	 * facing rig forward. If the head yaw cannot be read, the current origin yaw is kept.
	 */
	inline FTransform SolveOrigin(const FTransform& HeadInTracking, const FVector& TargetHeadInRig, const FTransform& CurrentOriginInRig)
	{
		float HeadYaw = 0.f;
		const float OriginYaw = GetHeadYaw(HeadInTracking.GetRotation(), HeadYaw)
			? -HeadYaw
			: CurrentOriginInRig.Rotator().Yaw;
		const FQuat OriginRot = FRotator(0.f, OriginYaw, 0.f).Quaternion();
		const FVector OriginLoc = TargetHeadInRig - OriginRot.RotateVector(HeadInTracking.GetLocation());
		return FTransform(OriginRot, OriginLoc);
	}

	/** Full seated recenter: head goes to the seat design eye point, facing rig forward. */
	inline FTransform SolveSeatRecenter(const FTransform& HeadInTracking, const FVector& DesignEyeInRig, const FTransform& CurrentOriginInRig)
	{
		return SolveOrigin(HeadInTracking, DesignEyeInRig, CurrentOriginInRig);
	}

	/** P-11 Forward re-anchor: the head stays where it is in the rig; only the facing is re-aligned. */
	inline FTransform SolveForwardReAnchor(const FTransform& HeadInTracking, const FTransform& CurrentOriginInRig)
	{
		return SolveOrigin(HeadInTracking, HeadLocationInRig(CurrentOriginInRig, HeadInTracking.GetLocation()), CurrentOriginInRig);
	}

	/** Clamps a controller-to-cannon offset to the supported range. */
	inline FRotator ClampAimOffset(const FRotator& Offset)
	{
		return FRotator(
			FMath::Clamp(FRotator::NormalizeAxis(Offset.Pitch), AimOffsetPitchMin, AimOffsetPitchMax),
			FMath::Clamp(FRotator::NormalizeAxis(Offset.Yaw), -AimOffsetYawLimit, AimOffsetYawLimit),
			0.f);
	}
}
