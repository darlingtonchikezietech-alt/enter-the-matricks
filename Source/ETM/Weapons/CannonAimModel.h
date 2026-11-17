// Aim envelope and servo maths for the HCP-40 cannon. Pure functions, no world access,
// so they can be covered by automation tests (Weapons/Tests/CannonComponentTest.cpp).
// Spec: bible section 4 "Reach and aiming". Owned by T-0009.
#pragma once

#include "CoreMinimal.h"

namespace ETMCannon
{
	struct FAimLimits
	{
		float YawLimitDeg = 65.f;   // +/- from mount forward
		float PitchMinDeg = -30.f;
		float PitchMaxDeg = 45.f;
	};

	/**
	 * Turns a direction in mount space into the barrel rotation the mechanism can actually reach.
	 * bOutClamped is true when the requested direction lies outside the envelope.
	 */
	inline FRotator ClampAim(const FVector& DirectionInMount, const FAimLimits& Limits, bool& bOutClamped)
	{
		const FRotator Wanted = DirectionInMount.Rotation();
		const float WantedYaw = FRotator::NormalizeAxis(Wanted.Yaw);
		const float WantedPitch = FRotator::NormalizeAxis(Wanted.Pitch);

		const float Yaw = FMath::Clamp(WantedYaw, -Limits.YawLimitDeg, Limits.YawLimitDeg);
		const float Pitch = FMath::Clamp(WantedPitch, Limits.PitchMinDeg, Limits.PitchMaxDeg);

		bOutClamped = !FMath::IsNearlyEqual(Yaw, WantedYaw, 0.01f) || !FMath::IsNearlyEqual(Pitch, WantedPitch, 0.01f);
		return FRotator(Pitch, Yaw, 0.f);
	}

	/** Moves the barrel toward the target at a constant angular speed, without overshooting. */
	inline FRotator StepServo(const FRotator& Current, const FRotator& Target, float DeltaTime, float SpeedDegPerSec)
	{
		return FMath::RInterpConstantTo(Current, Target, DeltaTime, SpeedDegPerSec);
	}
}
