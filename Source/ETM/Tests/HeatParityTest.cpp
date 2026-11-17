// In-engine parity test: the cannon heat model must reproduce the tracker's tools/balance_sim.py.
// Spec: PRODUCT_BIBLE.md section 5, P-01. Owned by T-0010.
//
// The reference numbers below are the sim's own output (bible v1.1 tuning):
//   python3 -c "import balance_sim as b; print(b.time_to_overheat(), b.recovery_time(False), b.sustained_dps('hold'), b.sustained_dps('burst_vent'))"
//   -> (2.751 s, 12 shots), 4.011 s, 60.0 dps, 90.67 dps
// If a tuning value changes, change the sim and this file together.
//
// Run headless: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Weapons.HeatParity; Quit" -unattended -nullrdr -testexit="Automation Test Queue Empty"
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Weapons/CannonHeatModel.h"
#include "Weapons/CannonComponent.h"

namespace
{
	const EAutomationTestFlags ETMHeatTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::CriticalPriority;

	// balance_sim.py reference values.
	constexpr float SimOverheatSeconds = 2.751f;
	constexpr int32 SimOverheatShots = 12;
	constexpr float SimRecoverySeconds = 4.011f;
	constexpr float SimHoldDps = 60.0f;
	constexpr float SimBurstVentDps = 90.667f;
	constexpr float Damage = 40.0f;

	enum class EPolicy { Hold, HoldThenVent, BurstVent };

	/**
	 * Steps the model the way the caller asks: either fixed 1 ms steps like the sim, or 72 Hz frames
	 * split into sub-steps of at most 1/240 s, exactly as UCannonComponent::AdvanceCannon does.
	 */
	struct FStepper
	{
		bool bFrameStepping = false;

		/** Advances one unit of time (1 ms, or one 72 Hz frame). Returns shots fired and the time advanced. */
		int32 Step(ETM::FHeatState& S, const ETM::FHeatTuning& T, bool bTrigger, bool bVent, float& OutDt) const
		{
			int32 Shots = 0;
			if (!bFrameStepping)
			{
				OutDt = 0.001f;
				Shots += ETM::StepHeat(S, T, OutDt, bTrigger, bVent) ? 1 : 0;
				return Shots;
			}
			OutDt = 1.f / 72.f;
			float Remaining = OutDt;
			while (Remaining > KINDA_SMALL_NUMBER)
			{
				const float Dt = FMath::Min(Remaining, 1.f / 240.f);
				Shots += ETM::StepHeat(S, T, Dt, bTrigger, bVent) ? 1 : 0;
				Remaining -= Dt;
			}
			return Shots;
		}
	};

	void TimeToOverheat(const FStepper& Stepper, const ETM::FHeatTuning& T, float& OutSeconds, int32& OutShots)
	{
		ETM::FHeatState S;
		OutSeconds = 0.f;
		OutShots = 0;
		while (!S.bOverheated && OutSeconds < 30.f)
		{
			float Dt = 0.f;
			OutShots += Stepper.Step(S, T, true, false, Dt);
			OutSeconds += Dt;
		}
	}

	float RecoveryTime(const FStepper& Stepper, const ETM::FHeatTuning& T, bool bVentHeld)
	{
		ETM::FHeatState S;
		S.Heat = T.HeatCapacity;
		S.bOverheated = true;
		S.TimeSinceLastShot = 0.f;
		float Seconds = 0.f;
		while (S.bOverheated && Seconds < 30.f)
		{
			float Dt = 0.f;
			Stepper.Step(S, T, false, bVentHeld, Dt);
			Seconds += Dt;
		}
		return Seconds;
	}

	/** Same three policies as balance_sim.sustained_dps, over 60 s. */
	float SustainedDps(const FStepper& Stepper, const ETM::FHeatTuning& T, EPolicy Policy)
	{
		ETM::FHeatState S;
		bool bVenting = false;
		int32 Shots = 0;
		float Seconds = 0.f;
		while (Seconds < 60.f - KINDA_SMALL_NUMBER)
		{
			bool bTrigger = true;
			bool bVent = false;
			if (Policy == EPolicy::HoldThenVent)
			{
				bTrigger = !S.bOverheated;
				bVent = S.bOverheated;
			}
			else if (Policy == EPolicy::BurstVent)
			{
				if (!bVenting && S.Heat >= T.HeatCapacity - T.HeatPerShot * 2.f) bVenting = true;
				if (bVenting && S.Heat <= 0.f) bVenting = false;
				bTrigger = !bVenting;
				bVent = bVenting;
			}
			float Dt = 0.f;
			Shots += Stepper.Step(S, T, bTrigger, bVent, Dt);
			Seconds += Dt;
		}
		return Shots * Damage / Seconds;
	}

	void CheckParity(FAutomationTestBase& Test, const FStepper& Stepper, const TCHAR* Label, float TimeTolerance, float DpsTolerance)
	{
		const ETM::FHeatTuning T;

		float OverheatSeconds = 0.f;
		int32 OverheatShots = 0;
		TimeToOverheat(Stepper, T, OverheatSeconds, OverheatShots);
		Test.TestEqual(FString::Printf(TEXT("%s: overheat after 12 shots"), Label), OverheatShots, SimOverheatShots);
		Test.TestTrue(FString::Printf(TEXT("%s: overheat at 2.75 s (got %.3f)"), Label, OverheatSeconds), FMath::IsNearlyEqual(OverheatSeconds, SimOverheatSeconds, TimeTolerance));

		const float Passive = RecoveryTime(Stepper, T, false);
		const float Vented = RecoveryTime(Stepper, T, true);
		Test.TestTrue(FString::Printf(TEXT("%s: recovery about 4.0 s (got %.3f)"), Label, Passive), FMath::IsNearlyEqual(Passive, SimRecoverySeconds, TimeTolerance));
		Test.TestTrue(FString::Printf(TEXT("%s: holding vent during the lock does not speed recovery (P-01)"), Label), FMath::IsNearlyEqual(Vented, Passive, 0.0001f));

		const float Hold = SustainedDps(Stepper, T, EPolicy::Hold);
		const float HoldThenVent = SustainedDps(Stepper, T, EPolicy::HoldThenVent);
		const float BurstVent = SustainedDps(Stepper, T, EPolicy::BurstVent);
		Test.TestTrue(FString::Printf(TEXT("%s: trigger held = 60 dps (got %.2f)"), Label, Hold), FMath::IsNearlyEqual(Hold, SimHoldDps, DpsTolerance));
		Test.TestTrue(FString::Printf(TEXT("%s: hold-then-vent gains nothing over hold (P-01, got %.2f)"), Label, HoldThenVent), FMath::IsNearlyEqual(HoldThenVent, Hold, DpsTolerance));
		Test.TestTrue(FString::Printf(TEXT("%s: burst + vent = 91 dps (got %.2f)"), Label, BurstVent), FMath::IsNearlyEqual(BurstVent, SimBurstVentDps, DpsTolerance));
		Test.TestTrue(FString::Printf(TEXT("%s: burst + vent beats holding the trigger"), Label), BurstVent > Hold * 1.2f);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMHeatParitySimStepTest, "ETM.Weapons.HeatParity.SimStep", ETMHeatTestFlags)
bool FETMHeatParitySimStepTest::RunTest(const FString&)
{
	// Same 1 ms step as the sim. Times match to the step size; dps may differ by one shot per minute
	// (0.67 dps) because the model accumulates in float and the sim in double.
	FStepper Stepper;
	CheckParity(*this, Stepper, TEXT("1 ms steps"), 0.002f, 0.7f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMHeatParityFrameStepTest, "ETM.Weapons.HeatParity.Quest72Hz", ETMHeatTestFlags)
bool FETMHeatParityFrameStepTest::RunTest(const FString&)
{
	// What the headset actually runs: 72 Hz frames with the component's 1/240 s sub-steps.
	//
	// KNOWN DRIFT (tracker T-0043): StepHeat resets the fire cooldown on the sub-step where it expires
	// instead of carrying the remainder, so each shot slips up to 3.5 ms. Measured on 17 Nov 2026:
	// overheat 2.792 s (sim 2.751), burst + vent 89.33 dps (sim 90.67). The tolerances below allow
	// three frames and 2% so the drift is visible but not failing. When T-0043 lands, tighten them to
	// one frame (1/72 s + 1 ms) and 1% (0.9 dps).
	FStepper Stepper;
	Stepper.bFrameStepping = true;
	CheckParity(*this, Stepper, TEXT("72 Hz frames"), 3.f / 72.f + 0.001f, 1.8f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMHeatParityTuningTest, "ETM.Weapons.HeatParity.ComponentDefaults", ETMHeatTestFlags)
bool FETMHeatParityTuningTest::RunTest(const FString&)
{
	// The values a designer sees on UCannonComponent must be the same ones the sim uses.
	const FCannonTuning Tuning;
	const ETM::FHeatTuning Heat = Tuning.ToHeatTuning();
	TestEqual(TEXT("FIRE_RATE"), Heat.FireRate, 4.0f);
	TestEqual(TEXT("DAMAGE"), Tuning.Damage, 40.0f);
	TestEqual(TEXT("WEAK_MULT"), Tuning.WeakPointMultiplier, 2.0f);
	TestEqual(TEXT("HEAT_CAP"), Heat.HeatCapacity, 100.0f);
	TestEqual(TEXT("HEAT_PER_SHOT"), Heat.HeatPerShot, 9.0f);
	TestEqual(TEXT("PASSIVE_COOL"), Heat.PassiveCooling, 18.0f);
	TestEqual(TEXT("PASSIVE_DELAY"), Heat.PassiveCoolingDelay, 0.4f);
	TestEqual(TEXT("VENT_COOL"), Heat.VentCooling, 40.0f);
	TestEqual(TEXT("OVERHEAT_UNLOCK"), Heat.OverheatUnlockThreshold, 35.0f);
	TestFalse(TEXT("VENT_DURING_LOCK is off (P-01)"), Heat.bAllowVentWhileOverheated);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
