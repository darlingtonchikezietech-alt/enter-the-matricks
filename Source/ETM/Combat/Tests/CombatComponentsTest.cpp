// Automation tests for the brace model, UDefenseComponent and UIntegrityComponent.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Combat; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Combat/BraceModel.h"
#include "Combat/DefenseComponent.h"
#include "Combat/IntegrityComponent.h"
#include "Combat/Tests/CombatTestListener.h"

namespace
{
	const EAutomationTestFlags ETMCombatTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;

	/** Steps a component in 72 Hz frames for the given time. */
	void Advance(UDefenseComponent* Defense, float Seconds)
	{
		for (float T = 0.f; T < Seconds - KINDA_SMALL_NUMBER; T += Frame72)
		{
			Defense->AdvanceBrace(FMath::Min(Frame72, Seconds - T));
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMBraceModelTimingTest, "ETM.Combat.Brace.ModelTiming", ETMCombatTestFlags)
bool FETMBraceModelTimingTest::RunTest(const FString&)
{
	const ETM::FBraceTuning T;
	TestEqual(TEXT("Spec duration"), T.Duration, 2.0f);
	TestEqual(TEXT("Spec cooldown"), T.Cooldown, 12.0f);
	TestEqual(TEXT("Spec reduction"), T.DamageReduction, 0.75f);

	// The full cycle (2 s field + 12 s cooldown) must take 14 s at any frame rate.
	for (const float Rate : { 30.f, 72.f, 90.f, 120.f })
	{
		ETM::FBraceState S;
		TestTrue(TEXT("Activates when ready"), ETM::TryActivateBrace(S, T));
		TestFalse(TEXT("Cannot re-activate while up"), ETM::TryActivateBrace(S, T));

		const float Dt = 1.f / Rate;
		float Time = 0.f, EndedAt = -1.f, ReadyAt = -1.f;
		while (ReadyAt < 0.f && Time < 30.f)
		{
			const ETM::FBraceStepResult R = ETM::StepBrace(S, T, Dt);
			Time += Dt;
			if (R.bEnded && EndedAt < 0.f)
			{
				EndedAt = Time;
				TestFalse(TEXT("Cannot activate during cooldown"), ETM::TryActivateBrace(S, T));
			}
			if (R.bBecameReady) ReadyAt = Time;
		}
		TestTrue(FString::Printf(TEXT("Field lasts 2 s at %.0f Hz (got %.3f)"), Rate, EndedAt), FMath::IsNearlyEqual(EndedAt, 2.f, Dt + 0.001f));
		TestTrue(FString::Printf(TEXT("Ready 14 s after activation at %.0f Hz (got %.3f)"), Rate, ReadyAt), FMath::IsNearlyEqual(ReadyAt, 14.f, Dt + 0.001f));
		TestTrue(TEXT("Activates again once ready"), ETM::TryActivateBrace(S, T));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMBraceModelDamageTest, "ETM.Combat.Brace.ModelDamage", ETMCombatTestFlags)
bool FETMBraceModelDamageTest::RunTest(const FString&)
{
	const ETM::FBraceTuning T;
	ETM::FBraceState S;

	TestEqual(TEXT("No reduction while down"), ETM::ModifyRigDamage(S, T, 40.f), 40.f);
	TestEqual(TEXT("Nothing absorbed while down"), S.DamageAbsorbed, 0.f);

	ETM::TryActivateBrace(S, T);
	TestEqual(TEXT("75% reduction while up"), ETM::ModifyRigDamage(S, T, 40.f), 10.f);
	TestEqual(TEXT("Absorbed part is tracked"), S.DamageAbsorbed, 30.f);
	TestEqual(TEXT("Negative damage is ignored"), ETM::ModifyRigDamage(S, T, -5.f), 0.f);
	TestEqual(TEXT("Absorbed total unchanged by bad input"), S.DamageAbsorbed, 30.f);

	ETM::StepBrace(S, T, 2.5f);
	TestEqual(TEXT("No reduction during cooldown"), ETM::ModifyRigDamage(S, T, 40.f), 40.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMDefenseComponentEventsTest, "ETM.Combat.Brace.ComponentEvents", ETMCombatTestFlags)
bool FETMDefenseComponentEventsTest::RunTest(const FString&)
{
	UDefenseComponent* Defense = NewObject<UDefenseComponent>();
	UETMCombatTestListener* Listener = NewObject<UETMCombatTestListener>();
	Defense->OnBraceStateChanged.AddDynamic(Listener, &UETMCombatTestListener::HandleBraceStateChanged);
	Defense->OnBraceReady.AddDynamic(Listener, &UETMCombatTestListener::HandleBraceReady);
	Defense->OnBraceAbsorbedDamage.AddDynamic(Listener, &UETMCombatTestListener::HandleBraceAbsorbed);

	TestTrue(TEXT("Ready at start"), Defense->IsBraceReady());
	TestTrue(TEXT("Activates"), Defense->TryActivateBrace());
	TestFalse(TEXT("Second press is rejected"), Defense->TryActivateBrace());
	TestEqual(TEXT("One activation event"), Listener->BraceActivated, 1);
	TestEqual(TEXT("Readiness is 0 while up"), Defense->GetReadiness(), 0.f);

	TestEqual(TEXT("Hit is reduced"), Defense->ModifyIncomingRigDamage(80.f), 20.f);
	TestEqual(TEXT("Absorb event fired"), Listener->BraceAbsorbedEvents, 1);
	TestEqual(TEXT("Absorb event amount"), Listener->LastAbsorbed, 60.f);

	Advance(Defense, 2.05f);
	TestFalse(TEXT("Field has dropped"), Defense->IsBraceActive());
	TestEqual(TEXT("One end event"), Listener->BraceEnded, 1);
	TestEqual(TEXT("Not ready yet"), Listener->BraceReady, 0);
	TestFalse(TEXT("Press during cooldown is rejected"), Defense->TryActivateBrace());

	Advance(Defense, 6.f);
	TestTrue(TEXT("Readiness is about half way"), FMath::IsNearlyEqual(Defense->GetReadiness(), 0.5f, 0.02f));

	Advance(Defense, 6.f);
	TestTrue(TEXT("Ready again"), Defense->IsBraceReady());
	TestEqual(TEXT("One ready event"), Listener->BraceReady, 1);
	TestEqual(TEXT("Absorbed total survives the cycle"), Defense->GetDamageAbsorbed(), 60.f);

	// Checkpoint retry while the field is up.
	Defense->TryActivateBrace();
	Defense->ResetBrace(true);
	TestTrue(TEXT("Reset leaves the brace ready"), Defense->IsBraceReady());
	TestEqual(TEXT("Reset ends the field"), Listener->BraceEnded, 2);
	TestEqual(TEXT("Reset can clear the absorbed total"), Defense->GetDamageAbsorbed(), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMIntegrityRigTest, "ETM.Combat.Integrity.RigWithBrace", ETMCombatTestFlags)
bool FETMIntegrityRigTest::RunTest(const FString&)
{
	UDefenseComponent* Defense = NewObject<UDefenseComponent>();
	UIntegrityComponent* Rig = NewObject<UIntegrityComponent>();
	UETMCombatTestListener* Listener = NewObject<UETMCombatTestListener>();
	Rig->OnIntegrityChanged.AddDynamic(Listener, &UETMCombatTestListener::HandleIntegrityChanged);
	Rig->OnDamaged.AddDynamic(Listener, &UETMCombatTestListener::HandleDamaged);
	Rig->OnDepleted.AddDynamic(Listener, &UETMCombatTestListener::HandleDepleted);
	Rig->SetDefenseComponent(Defense);
	Rig->ResetToMax();
	TestEqual(TEXT("Rig starts at 300"), Rig->GetCurrent(), 300.f);

	TestEqual(TEXT("Unbraced hit lands in full"), Rig->ApplyIntegrityDamage(40.f), 40.f);
	TestEqual(TEXT("Damage event: applied"), Listener->LastApplied, 40.f);
	TestEqual(TEXT("Damage event: prevented"), Listener->LastPrevented, 0.f);
	TestEqual(TEXT("Change event delta is negative"), Listener->LastDelta, -40.f);

	Defense->TryActivateBrace();
	TestEqual(TEXT("Braced hit is cut by 75%"), Rig->ApplyIntegrityDamage(40.f), 10.f);
	TestEqual(TEXT("Damage event reports what the brace prevented"), Listener->LastPrevented, 30.f);
	TestEqual(TEXT("Rig at 250"), Rig->GetCurrent(), 250.f);

	// Midpoint repair: +60, capped at maximum (P-05).
	TestEqual(TEXT("Repair adds up to the cap"), Rig->Repair(60.f), 50.f);
	TestEqual(TEXT("Rig is capped at 300"), Rig->GetCurrent(), 300.f);

	Defense->ResetBrace();
	TestEqual(TEXT("Overkill only removes what is left"), Rig->ApplyIntegrityDamage(1000.f), 300.f);
	TestTrue(TEXT("Rig is depleted"), Rig->IsDepleted());
	TestEqual(TEXT("Failure event fired once"), Listener->Depleted, 1);

	const int32 DamagedBefore = Listener->Damaged;
	TestEqual(TEXT("No damage after failure"), Rig->ApplyIntegrityDamage(10.f), 0.f);
	TestEqual(TEXT("No repair after failure"), Rig->Repair(60.f), 0.f);
	TestEqual(TEXT("No events after failure"), Listener->Damaged, DamagedBefore);
	TestEqual(TEXT("Failure event still fired once"), Listener->Depleted, 1);

	// Checkpoint retry restores the pool and re-arms the failure event.
	Rig->ResetToMax();
	TestFalse(TEXT("Restored rig is alive"), Rig->IsDepleted());
	Rig->ApplyIntegrityDamage(300.f);
	TestEqual(TEXT("Failure fires again after a restore"), Listener->Depleted, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMIntegrityObjectiveTest, "ETM.Combat.Integrity.ObjectiveAndPresets", ETMCombatTestFlags)
bool FETMIntegrityObjectiveTest::RunTest(const FString&)
{
	UDefenseComponent* Defense = NewObject<UDefenseComponent>();
	UIntegrityComponent* Objective = NewObject<UIntegrityComponent>();
	Objective->MaxIntegrity = 500.f;
	Objective->bIsRig = false;
	Objective->SetDefenseComponent(Defense); // even with a brace wired in, the objective must not use it
	Objective->ResetToMax();

	Defense->TryActivateBrace();
	TestEqual(TEXT("Brace never protects the objective"), Objective->ApplyIntegrityDamage(100.f), 100.f);
	TestEqual(TEXT("Brace absorbed nothing"), Defense->GetDamageAbsorbed(), 0.f);
	TestEqual(TEXT("Objective at 400"), Objective->GetCurrent(), 400.f);
	TestEqual(TEXT("Normalized value"), Objective->GetNormalized(), 0.8f);

	// P-04 boss checkpoint presets: Standard keeps at least 60%, Assisted at least 80%.
	Objective->RestoreCheckpoint(120.f, 0.6f);
	TestEqual(TEXT("Low saved value is raised to 60%"), Objective->GetCurrent(), 300.f);
	Objective->RestoreCheckpoint(450.f, 0.6f);
	TestEqual(TEXT("High saved value is kept"), Objective->GetCurrent(), 450.f);
	Objective->RestoreCheckpoint(120.f, 0.8f);
	TestEqual(TEXT("Assisted floor is 80%"), Objective->GetCurrent(), 400.f);

	// Assisted difficulty: reduced incoming damage.
	Objective->IncomingDamageScale = 0.5f;
	TestEqual(TEXT("Damage scale applies"), Objective->ApplyIntegrityDamage(100.f), 50.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
