// CannonHeatModel.h
// Pure C++ (no Unreal types) so it can be unit-tested outside the engine and kept
// in lock-step with enter-the-matricks-tracker/tools/balance_sim.py.
// Spec: PRODUCT_BIBLE.md section 5, patches P-01 and P-34.
#pragma once

#include <algorithm>

namespace ETM
{
	struct FHeatTuning
	{
		float FireRate = 10.0f;             // shots per second (P-34)
		float HeatCapacity = 100.0f;
		float HeatPerShot = 1.0f;           // P-34: about 100 shots from cold to overheat
		float PassiveCooling = 18.0f;       // per second
		float PassiveCoolingDelay = 0.4f;   // seconds without firing before passive cooling starts
		float VentCooling = 40.0f;          // per second; REPLACES passive (not additive)
		float OverheatUnlockThreshold = 35.0f;
		bool  bAllowVentWhileOverheated = false; // P-01: must stay false
	};

	struct FHeatState
	{
		float Heat = 0.0f;
		bool  bOverheated = false;
		float TimeSinceLastShot = 1000.0f;
		float FireCooldown = 0.0f;
	};

	/**
	 * Advance the heat model by Dt seconds.
	 * Returns true if a shot was fired during this step.
	 * Order of operations matches balance_sim.py exactly:
	 * timers -> cooling -> unlock check -> fire attempt -> overheat check.
	 */
	inline bool StepHeat(FHeatState& S, const FHeatTuning& T, float Dt, bool bTriggerHeld, bool bVentHeld)
	{
		// The cooldown may go below zero by at most one step, and that remainder is carried into the
		// next shot's cooldown (T-0043). Without the carry, frame-stepped fire slips up to one sub-step
		// per shot, which at 10 shots/s costs several percent of the fire rate.
		S.FireCooldown = std::max(-Dt, S.FireCooldown - Dt);
		S.TimeSinceLastShot += Dt;

		bool bVenting = bVentHeld && (!S.bOverheated || T.bAllowVentWhileOverheated);

		if (bVenting)
		{
			S.Heat = std::max(0.0f, S.Heat - T.VentCooling * Dt);
		}
		else if (S.TimeSinceLastShot >= T.PassiveCoolingDelay)
		{
			S.Heat = std::max(0.0f, S.Heat - T.PassiveCooling * Dt);
		}

		if (S.bOverheated && S.Heat <= T.OverheatUnlockThreshold)
		{
			S.bOverheated = false;
		}

		// Venting blocks firing on that cannon (bible section 5).
		if (bTriggerHeld && !bVenting && !S.bOverheated && S.FireCooldown <= 0.0f)
		{
			S.Heat += T.HeatPerShot;
			S.TimeSinceLastShot = 0.0f;
			S.FireCooldown += 1.0f / T.FireRate;
			if (S.Heat >= T.HeatCapacity) // the shot that crosses the cap still fires (P-01)
			{
				S.Heat = T.HeatCapacity;
				S.bOverheated = true;
			}
			return true;
		}
		return false;
	}
}
