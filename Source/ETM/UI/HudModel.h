// HUD rules that do not need a world: which warnings to show, and when secondary text gives way.
// Spec: bible section 11 "HUD hierarchy" (limit simultaneous messages; critical attack warnings override
// score popups and secondary narrative text). Owned by T-0019. Tested in UI/Tests/HudPresenterTest.cpp.
#pragma once

#include "CoreMinimal.h"

namespace ETMHud
{
	/** One incoming attack warning, keyed by its source. */
	struct FWarning
	{
		const void* SourceKey = nullptr;
		float Remaining = 0.f;
		bool bTargetsRig = false;
	};

	/**
	 * The warnings to show, most urgent first: the soonest impact wins; on a tie, attacks on the rig win
	 * (the player can brace for those). At most MaxShown.
	 */
	inline TArray<FWarning> RankWarnings(TArray<FWarning> Warnings, int32 MaxShown)
	{
		Warnings.Sort([](const FWarning& A, const FWarning& B)
		{
			if (!FMath::IsNearlyEqual(A.Remaining, B.Remaining, 0.01f)) return A.Remaining < B.Remaining;
			return A.bTargetsRig && !B.bTargetsRig;
		});
		if (Warnings.Num() > MaxShown) Warnings.SetNum(FMath::Max(0, MaxShown));
		return Warnings;
	}

	/** A single secondary line (score popup, narrative): hidden whenever a critical warning is up. */
	struct FSecondaryLine
	{
		FText Text;
		float Remaining = 0.f;

		void Show(const FText& InText, float Seconds) { Text = InText; Remaining = Seconds; }
		/** Time keeps running while it is suppressed, so stale text never pops back long after. */
		void Advance(float Dt) { Remaining = FMath::Max(0.f, Remaining - Dt); }
		bool IsVisible(bool bCriticalWarningActive) const { return Remaining > 0.f && !bCriticalWarningActive && !Text.IsEmpty(); }
	};

	/**
	 * Is a world point outside the comfortable view cone? Used for the directional edge cue
	 * ("near the source, with directional edge cue if needed").
	 */
	inline bool IsOutsideView(const FVector& HeadLocation, const FVector& HeadForward, const FVector& Point, float HalfAngleDeg)
	{
		const FVector ToPoint = (Point - HeadLocation).GetSafeNormal();
		return FVector::DotProduct(ToPoint, HeadForward.GetSafeNormal()) < FMath::Cos(FMath::DegreesToRadians(HalfAngleDeg));
	}
}
