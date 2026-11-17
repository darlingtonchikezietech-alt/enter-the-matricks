// BraceModel.h
// Pure C++ (no Unreal types) model of the RIG-6 defensive brace, in the same style as
// Weapons/CannonHeatModel.h, so the timing can be tested without a world.
// Spec: PRODUCT_BIBLE.md section 5 "Defensive brace", P-30. Owned by T-0011.
#pragma once

#include <algorithm>

namespace ETM
{
	struct FBraceTuning
	{
		float Duration = 2.0f;         // seconds the field is up
		float Cooldown = 12.0f;        // seconds; starts when the field ends, not when it starts
		float DamageReduction = 0.75f; // fraction of rig damage removed while the field is up
	};

	struct FBraceState
	{
		float ActiveRemaining = 0.0f;
		float CooldownRemaining = 0.0f;
		float DamageAbsorbed = 0.0f;   // running total, feeds "damage prevented" (P-30)
	};

	struct FBraceStepResult
	{
		bool bEnded = false;       // the field dropped during this step
		bool bBecameReady = false; // the cooldown finished during this step
	};

	inline bool IsBraceActive(const FBraceState& S) { return S.ActiveRemaining > 0.0f; }
	inline bool IsBraceReady(const FBraceState& S) { return S.ActiveRemaining <= 0.0f && S.CooldownRemaining <= 0.0f; }

	/** Raises the field if it is ready. Returns true if it activated. */
	inline bool TryActivateBrace(FBraceState& S, const FBraceTuning& T)
	{
		if (!IsBraceReady(S) || T.Duration <= 0.0f) return false;
		S.ActiveRemaining = T.Duration;
		return true;
	}

	/**
	 * Advances the brace by Dt seconds. Time left over when the field drops is counted
	 * against the cooldown, so the total cycle length does not depend on frame rate.
	 */
	inline FBraceStepResult StepBrace(FBraceState& S, const FBraceTuning& T, float Dt)
	{
		FBraceStepResult R;
		if (Dt <= 0.0f) return R;

		if (S.ActiveRemaining > 0.0f)
		{
			S.ActiveRemaining -= Dt;
			if (S.ActiveRemaining > 0.0f) return R;

			const float Leftover = -S.ActiveRemaining;
			S.ActiveRemaining = 0.0f;
			S.CooldownRemaining = std::max(0.0f, T.Cooldown - Leftover);
			R.bEnded = true;
			R.bBecameReady = S.CooldownRemaining <= 0.0f;
			return R;
		}

		if (S.CooldownRemaining > 0.0f)
		{
			S.CooldownRemaining = std::max(0.0f, S.CooldownRemaining - Dt);
			R.bBecameReady = S.CooldownRemaining <= 0.0f;
		}
		return R;
	}

	/**
	 * Returns the rig damage that gets through. While the field is up the reduced part is
	 * added to DamageAbsorbed. The brace reduces damage only: it never cancels or reflects an attack.
	 */
	inline float ModifyRigDamage(FBraceState& S, const FBraceTuning& T, float Damage)
	{
		if (Damage <= 0.0f || !IsBraceActive(S)) return std::max(0.0f, Damage);
		const float Reduction = std::min(1.0f, std::max(0.0f, T.DamageReduction));
		const float Passed = Damage * (1.0f - Reduction);
		S.DamageAbsorbed += Damage - Passed;
		return Passed;
	}
}
