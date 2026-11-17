// Automation tests for the Gatebreaker boss.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Bosses; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Bosses/Gatebreaker/Gatebreaker.h"
#include "Encounters/EncounterDirector.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Combat/IntegrityComponent.h"
#include "Combat/DefenseComponent.h"
#include "Weapons/CannonHeatModel.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const EAutomationTestFlags ETMBossTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;
	const float WeakPointShot = 80.f; // 40 base x2 (P-02)

	struct FTestWorld
	{
		UWorld* World = nullptr;
		AActor* RigActor = nullptr;
		UIntegrityComponent* Rig = nullptr;
		UDefenseComponent* Brace = nullptr;
		AGatebreaker* Boss = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ETMBossTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true); // no game mode, so mark it begun or spawned actors skip BeginPlay

			RigActor = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(RigActor);
			RigActor->SetRootComponent(Root);
			Root->RegisterComponent();
			Brace = NewObject<UDefenseComponent>(RigActor);
			Brace->RegisterComponent();
			Rig = NewObject<UIntegrityComponent>(RigActor);
			Rig->MaxIntegrity = 300.f;
			Rig->RegisterComponent();
			Rig->SetDefenseComponent(Brace);
			Rig->ResetToMax();

			Boss = World->SpawnActor<AGatebreaker>(FVector(3000.f, 0.f, 0.f), FRotator(0.f, 180.f, 0.f));
			Boss->Target = RigActor;
		}

		~FTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->SetBegunPlay(false);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		void Run(float Seconds) const
		{
			for (float T = 0.f; T < Seconds - KINDA_SMALL_NUMBER; T += Frame72) Boss->SimulateStep(Frame72);
		}

		/** Runs until the condition holds or the time runs out. Returns whether it held. */
		template <typename F>
		bool RunUntil(F&& Condition, float MaxSeconds) const
		{
			for (float T = 0.f; T < MaxSeconds; T += Frame72)
			{
				if (Condition()) return true;
				Boss->SimulateStep(Frame72);
			}
			return Condition();
		}

		/** A cannon hit on one of the boss's meshes, as the cannon delivers it (damage already multiplied). */
		float Shoot(UPrimitiveComponent* Mesh, float Damage) const
		{
			FHitResult Hit;
			Hit.Component = Mesh;
			Hit.ImpactPoint = Mesh->GetComponentLocation();
			return UGameplayStatics::ApplyPointDamage(Boss, Damage, FVector::ForwardVector, Hit, nullptr, RigActor, nullptr);
		}

		void BreakNodes() const
		{
			for (int32 Guard = 0; Guard < 40 && Boss->GetBossPhase() == EGatebreakerPhase::Anchor; ++Guard)
			{
				RunUntil([this] { return Boss->IsTargetExposed(AGatebreaker::NodeLeftPart) || Boss->IsTargetExposed(AGatebreaker::NodeRightPart); }, 10.f);
				UPrimitiveComponent* Open = Boss->IsTargetExposed(AGatebreaker::NodeLeftPart) ? Boss->NodeLeft.Get() : Boss->NodeRight.Get();
				for (int32 Shot = 0; Shot < 8; ++Shot) Shoot(Open, WeakPointShot);
			}
		}

		void Activate() const
		{
			Boss->Activate(Boss->GetActorTransform());
			Run(Boss->SpawnDuration + 0.05f);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMGatebreakerValuesTest, "ETM.Bosses.Gatebreaker.ValuesAndArmour", ETMBossTestFlags)
bool FETMGatebreakerValuesTest::RunTest(const FString&)
{
	FTestWorld W;
	W.Activate();
	TestEqual(TEXT("Shield nodes 600 HP"), W.Boss->GetPartHealth(AGatebreaker::NodeLeftPart), 600.f);
	TestEqual(TEXT("Charge chamber 240 HP"), W.Boss->GetPartHealth(AGatebreaker::ChamberPart), 240.f);
	TestEqual(TEXT("Core 3000 HP"), W.Boss->GetCoreHealth(), 3000.f);
	TestEqual(TEXT("Furnace blast 45"), W.Boss->FurnaceDamage, 45.f);
	TestEqual(TEXT("Furnace charge 2 s"), W.Boss->FurnaceChargeSeconds, 2.f);
	TestEqual(TEXT("Artillery 35"), W.Boss->ArtilleryDamage, 35.f);
	TestEqual(TEXT("Starts in phase 1"), W.Boss->GetBossPhase(), EGatebreakerPhase::Anchor);
	for (UStaticMeshComponent* Target : { W.Boss->NodeLeft.Get(), W.Boss->NodeRight.Get(), W.Boss->Chamber.Get(), W.Boss->Core.Get() })
	{
		TestTrue(TEXT("Every target is a weak point (P-02)"), Target->ComponentHasTag(TEXT("ETM.WeakPoint")));
	}
	TestTrue(TEXT("Hull is immune armour"), W.Boss->Hull->ComponentHasTag(TEXT("ETM.Immune")));

	TestEqual(TEXT("Hull hit deals nothing"), W.Shoot(W.Boss->Hull, 500.f), 0.f);
	TestEqual(TEXT("Closed core deals nothing"), W.Shoot(W.Boss->Core, 500.f), 0.f);
	UGameplayStatics::ApplyDamage(W.Boss, 500.f, nullptr, W.RigActor, nullptr);
	TestEqual(TEXT("Core untouched"), W.Boss->GetCoreHealth(), 3000.f);
	TestEqual(TEXT("Each refused hit is a deflection (feedback)"), W.Boss->GetDeflectedCount(), 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMGatebreakerAnchorTest, "ETM.Bosses.Gatebreaker.Phase1Anchor", ETMBossTestFlags)
bool FETMGatebreakerAnchorTest::RunTest(const FString&)
{
	FTestWorld W;
	W.Activate();

	TestTrue(TEXT("A node opens"), W.RunUntil([&W] { return W.Boss->IsTargetExposed(AGatebreaker::NodeLeftPart); }, 3.f));
	TestFalse(TEXT("Only one node at a time"), W.Boss->IsTargetExposed(AGatebreaker::NodeRightPart));
	TestEqual(TEXT("The closed node is immune"), W.Shoot(W.Boss->NodeRight, 80.f), 0.f);

	// Let the left window pass unused: the right one opens next, and the left one comes round again.
	TestTrue(TEXT("Windows alternate"), W.RunUntil([&W] { return W.Boss->IsTargetExposed(AGatebreaker::NodeRightPart); }, 10.f));
	TestTrue(TEXT("A missed node window comes back"), W.RunUntil([&W] { return W.Boss->IsTargetExposed(AGatebreaker::NodeLeftPart); }, 10.f));

	for (int32 Shot = 0; Shot < 8; ++Shot) W.Shoot(W.Boss->NodeLeft, WeakPointShot);
	TestTrue(TEXT("8 weak-point shots break a 600 HP node"), W.Boss->IsPartBroken(AGatebreaker::NodeLeftPart));
	TestEqual(TEXT("Still phase 1 with one node left"), W.Boss->GetBossPhase(), EGatebreakerPhase::Anchor);

	// Phase-1 furnace: chamber shown only while charging; if not interrupted it hits for 45 and the core stays shut.
	TestTrue(TEXT("Furnace starts charging"), W.RunUntil([&W] { return W.Boss->GetCurrentAttack() == EGatebreakerAttack::Furnace; }, 15.f));
	TestTrue(TEXT("Chamber is exposed during the charge"), W.Boss->IsTargetExposed(AGatebreaker::ChamberPart));
	const float RigBefore = W.Rig->GetCurrent();
	W.Run(2.1f);
	TestEqual(TEXT("Furnace hits the rig for 45"), RigBefore - W.Rig->GetCurrent(), 45.f);
	TestFalse(TEXT("Chamber closes after the blast"), W.Boss->IsTargetExposed(AGatebreaker::ChamberPart));
	TestFalse(TEXT("No core window in phase 1"), W.Boss->IsTargetExposed(AGatebreaker::CorePart));

	TestTrue(TEXT("Only the right node opens now"), W.RunUntil([&W] { return W.Boss->IsTargetExposed(AGatebreaker::NodeRightPart); }, 10.f));
	for (int32 Shot = 0; Shot < 8; ++Shot) W.Shoot(W.Boss->NodeRight, WeakPointShot);
	TestEqual(TEXT("Both nodes down: phase 2"), W.Boss->GetBossPhase(), EGatebreakerPhase::Breach);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMGatebreakerBreachTest, "ETM.Bosses.Gatebreaker.Phase2CoreWindows", ETMBossTestFlags)
bool FETMGatebreakerBreachTest::RunTest(const FString&)
{
	FTestWorld W;
	W.Activate();
	W.BreakNodes();
	TestEqual(TEXT("In phase 2"), W.Boss->GetBossPhase(), EGatebreakerPhase::Breach);

	// Furnace fires: 45 to the rig, core opens for 4 s.
	TestTrue(TEXT("Furnace charges"), W.RunUntil([&W] { return W.Boss->GetCurrentAttack() == EGatebreakerAttack::Furnace; }, 15.f));
	const float RigBefore = W.Rig->GetCurrent();
	TestTrue(TEXT("Core opens after the blast"), W.RunUntil([&W] { return W.Boss->IsTargetExposed(AGatebreaker::CorePart); }, 3.f));
	TestEqual(TEXT("The blast landed"), RigBefore - W.Rig->GetCurrent(), 45.f);
	TestTrue(FString::Printf(TEXT("Fired: 4 s window (got %.2f)"), W.Boss->GetCoreOpenRemaining()), FMath::IsNearlyEqual(W.Boss->GetCoreOpenRemaining(), 4.f, 0.05f));
	TestEqual(TEXT("Open core takes weak-point damage"), W.Shoot(W.Boss->Core, WeakPointShot), WeakPointShot);
	TestTrue(TEXT("Core closes after its window"), W.RunUntil([&W] { return !W.Boss->IsTargetExposed(AGatebreaker::CorePart); }, 4.5f));
	TestEqual(TEXT("Closed again: immune"), W.Shoot(W.Boss->Core, WeakPointShot), 0.f);

	// Furnace interrupted: break the 240 HP chamber with 3 weak-point shots; no blast, core opens for 6 s.
	TestTrue(TEXT("Next furnace charges"), W.RunUntil([&W] { return W.Boss->GetCurrentAttack() == EGatebreakerAttack::Furnace; }, 15.f));
	W.Run(0.5f);
	const float RigBeforeInterrupt = W.Rig->GetCurrent();
	for (int32 Shot = 0; Shot < 3; ++Shot) W.Shoot(W.Boss->Chamber, WeakPointShot);
	TestTrue(TEXT("Chamber broke"), W.Boss->IsPartBroken(AGatebreaker::ChamberPart));
	TestEqual(TEXT("Boss staggers"), W.Boss->GetState(), EEnemyState::Staggered);
	TestTrue(FString::Printf(TEXT("Interrupted: 6 s window (got %.2f)"), W.Boss->GetCoreOpenRemaining()), FMath::IsNearlyEqual(W.Boss->GetCoreOpenRemaining(), 6.f, 0.05f));
	W.Run(3.f);
	TestEqual(TEXT("The interrupted blast never lands"), W.Rig->GetCurrent(), RigBeforeInterrupt);

	// The chamber is whole again for the next furnace.
	TestTrue(TEXT("Third furnace"), W.RunUntil([&W] { return W.Boss->GetCurrentAttack() == EGatebreakerAttack::Furnace; }, 20.f));
	TestEqual(TEXT("Chamber rebuilt for each furnace"), W.Boss->GetPartHealth(AGatebreaker::ChamberPart), 240.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMGatebreakerCollapseTest, "ETM.Bosses.Gatebreaker.Phase3CollapseAndDeath", ETMBossTestFlags)
bool FETMGatebreakerCollapseTest::RunTest(const FString&)
{
	FTestWorld W;
	W.Activate();
	W.BreakNodes();

	// Bring the core to 900 (30%) inside one window.
	TestTrue(TEXT("Core opens"), W.RunUntil([&W] { return W.Boss->IsTargetExposed(AGatebreaker::CorePart); }, 20.f));
	W.Shoot(W.Boss->Core, 2000.f);
	TestEqual(TEXT("Core at 1000: still phase 2"), W.Boss->GetBossPhase(), EGatebreakerPhase::Breach);
	W.Shoot(W.Boss->Core, 100.f);
	TestEqual(TEXT("Core at 900 (30%): phase 3"), W.Boss->GetBossPhase(), EGatebreakerPhase::Collapse);

	// Phase 3: the next attack is the artillery shot. It is telegraphed, hits for 35, and does not open the core.
	TestTrue(TEXT("Artillery charges"), W.RunUntil([&W] { return W.Boss->GetCurrentAttack() == EGatebreakerAttack::Artillery; }, 20.f));
	W.Run(1.f);
	TestTrue(TEXT("Barrel rises during the warning (shape cue)"), W.Boss->ArtilleryPivot->GetRelativeRotation().Pitch > 20.0);
	const float RigBefore = W.Rig->GetCurrent();
	W.Run(1.1f);
	TestEqual(TEXT("Artillery hits for 35"), RigBefore - W.Rig->GetCurrent(), 35.f);
	TestFalse(TEXT("Artillery does not open the core"), W.Boss->IsTargetExposed(AGatebreaker::CorePart));

	// Then the furnace again, which does open it. Brace through the blast: 45 x 0.25.
	TestTrue(TEXT("Furnace follows"), W.RunUntil([&W] { return W.Boss->GetCurrentAttack() == EGatebreakerAttack::Furnace; }, 20.f));
	W.Brace->TryActivateBrace();
	const float RigBeforeFurnace = W.Rig->GetCurrent();
	TestTrue(TEXT("Core keeps opening in phase 3"), W.RunUntil([&W] { return W.Boss->IsTargetExposed(AGatebreaker::CorePart); }, 3.f));
	TestTrue(TEXT("Braced blast"), FMath::IsNearlyEqual(RigBeforeFurnace - W.Rig->GetCurrent(), 11.25f, 0.01f));

	W.Shoot(W.Boss->Core, 900.f);
	TestEqual(TEXT("Core destroyed: boss dies"), W.Boss->GetState(), EEnemyState::Dying);
	TestEqual(TEXT("Phase Destroyed"), W.Boss->GetBossPhase(), EGatebreakerPhase::Destroyed);
	W.Run(W.Boss->DeathDuration + 0.1f);
	TestTrue(TEXT("Wreck is parked, not destroyed (retries can bring it back)"), IsValid(W.Boss) && !W.Boss->IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMGatebreakerExposureTest, "ETM.Bosses.Gatebreaker.CoreExposuresAt70Percent", ETMBossTestFlags)
bool FETMGatebreakerExposureTest::RunTest(const FString&)
{
	// Two real heat-model cannons fire whenever the core is open and vent when it is shut. Every shot lands
	// on the core with the expected value at 70% accuracy (0.7 x 80). The bible and balance_sim.py expect
	// about 2.2 exposures, so the core must fall in the 2nd or 3rd window, never the 1st, never past the 4th.
	FTestWorld W;
	W.Rig->MaxIntegrity = 10000.f; // the test is about the core, not survival
	W.Rig->ResetToMax();
	W.Activate();
	W.BreakNodes();

	const ETM::FHeatTuning Tuning;
	ETM::FHeatState Cannons[2];
	for (float T = 0.f; T < 300.f && W.Boss->GetState() != EEnemyState::Dying; T += Frame72)
	{
		const bool bOpen = W.Boss->IsTargetExposed(AGatebreaker::CorePart);
		for (ETM::FHeatState& Cannon : Cannons)
		{
			for (float Sub = 0.f; Sub < Frame72 - KINDA_SMALL_NUMBER; Sub += Frame72 / 4.f)
			{
				if (ETM::StepHeat(Cannon, Tuning, Frame72 / 4.f, bOpen, !bOpen) && bOpen)
				{
					W.Shoot(W.Boss->Core, 0.7f * WeakPointShot);
				}
			}
		}
		W.Boss->SimulateStep(Frame72);
	}
	const int32 Exposures = W.Boss->GetCoreExposureCount();
	TestEqual(TEXT("Core destroyed"), W.Boss->GetState(), EEnemyState::Dying);
	TestTrue(FString::Printf(TEXT("Core falls in 2-3 exposures at 70%% accuracy (took %d)"), Exposures), Exposures >= 2 && Exposures <= 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMGatebreakerNoSoftlockTest, "ETM.Bosses.Gatebreaker.MissedWindowsNeverSoftlock", ETMBossTestFlags)
bool FETMGatebreakerNoSoftlockTest::RunTest(const FString&)
{
	FTestWorld W;
	W.Rig->MaxIntegrity = 100000.f;
	W.Rig->ResetToMax();
	W.Activate();

	// Two minutes of ignoring the nodes: they keep coming back.
	int32 NodeOpenings = 0;
	bool bWasOpen = false;
	for (float T = 0.f; T < 120.f; T += Frame72)
	{
		W.Boss->SimulateStep(Frame72);
		const bool bOpen = W.Boss->IsTargetExposed(AGatebreaker::NodeLeftPart) || W.Boss->IsTargetExposed(AGatebreaker::NodeRightPart);
		if (bOpen && !bWasOpen) ++NodeOpenings;
		bWasOpen = bOpen;
	}
	TestTrue(FString::Printf(TEXT("Node windows keep repeating (%d in 2 min)"), NodeOpenings), NodeOpenings >= 15);

	// Two minutes of ignoring the core in phase 2 and 3: it keeps opening, the boss keeps cycling.
	W.BreakNodes();
	const int32 Before = W.Boss->GetCoreExposureCount();
	W.Run(120.f);
	TestTrue(FString::Printf(TEXT("Core windows keep repeating (%d more in 2 min)"), W.Boss->GetCoreExposureCount() - Before), W.Boss->GetCoreExposureCount() - Before >= 8);
	TestTrue(TEXT("Boss is still in the fight"), W.Boss->IsThreat());

	// The same in phase 3.
	TestTrue(TEXT("Core opens"), W.RunUntil([&W] { return W.Boss->IsTargetExposed(AGatebreaker::CorePart); }, 20.f));
	W.Shoot(W.Boss->Core, 2200.f);
	TestEqual(TEXT("Phase 3"), W.Boss->GetBossPhase(), EGatebreakerPhase::Collapse);
	const int32 BeforeCollapse = W.Boss->GetCoreExposureCount();
	W.Run(120.f);
	TestTrue(TEXT("Core keeps opening in phase 3"), W.Boss->GetCoreExposureCount() - BeforeCollapse >= 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMGatebreakerDirectorTest, "ETM.Bosses.Gatebreaker.DirectorAndRetry", ETMBossTestFlags)
bool FETMGatebreakerDirectorTest::RunTest(const FString&)
{
	FTestWorld W;
	UMissionStateSubsystem* Mission = W.World->GetSubsystem<UMissionStateSubsystem>();
	W.World->SpawnActor<AMissionObjective>(FVector(200.f, 0.f, 0.f), FRotator::ZeroRotator);
	Mission->RegisterRig(W.Rig, W.Brace);

	AEncounterDirector* Director = W.World->SpawnActor<AEncounterDirector>();
	Director->RigActor = W.RigActor;
	FEncounterPhase BossPhase;
	BossPhase.Name = TEXT("Gatebreaker");
	BossPhase.SlotSeconds = 10.f; // short slot so the P-13 60% rule is met quickly in the test
	BossPhase.bBossPhase = true;
	BossPhase.bCompleteWhenCleared = false;
	BossPhase.bCheckpointOnStart = true;
	BossPhase.bBossCheckpoint = true;
	BossPhase.CheckpointName = TEXT("Boss");
	BossPhase.MaxConcurrentAttacks = 1; // adds share one token; the boss does not use one
	FEncounterPhase Resolution;
	Resolution.Name = TEXT("Resolution");
	Resolution.SlotSeconds = 30.f;
	Director->Phases = { BossPhase, Resolution };

	W.Boss->SetDirector(Director); // the boss already exists, so bind it as a level script would

	TestFalse(TEXT("Boss waits parked until its phase"), W.Boss->IsActive());
	TestTrue(TEXT("Encounter starts"), Director->StartEncounter(11));
	TestTrue(TEXT("Boss phase activates the boss"), W.Boss->IsActive());

	// Damage the core, then lose: the retry brings the boss back whole.
	W.Run(W.Boss->SpawnDuration + 0.1f);
	W.BreakNodes();
	TestTrue(TEXT("Core opens"), W.RunUntil([&W] { return W.Boss->IsTargetExposed(AGatebreaker::CorePart); }, 20.f));
	W.Shoot(W.Boss->Core, 1500.f);
	W.Rig->ApplyIntegrityDamage(5000.f);
	TestEqual(TEXT("Mission failed"), Mission->GetState(), EMissionState::Failed);
	TestTrue(TEXT("Retry"), Mission->RetryFromLastCheckpoint());
	TestTrue(TEXT("Boss is back"), W.Boss->IsActive());
	TestEqual(TEXT("P-04: boss at full health"), W.Boss->GetCoreHealth(), 3000.f);
	TestFalse(TEXT("Nodes are whole again"), W.Boss->IsPartBroken(AGatebreaker::NodeLeftPart));
	TestEqual(TEXT("Rig preset 300"), W.Rig->GetCurrent(), 300.f);

	// Killing the boss ends its phase.
	W.Run(W.Boss->SpawnDuration + 0.1f);
	W.BreakNodes();
	TestTrue(TEXT("Core opens"), W.RunUntil([&W] { return W.Boss->IsTargetExposed(AGatebreaker::CorePart); }, 20.f));
	W.Shoot(W.Boss->Core, 3000.f);
	for (float T = 0.f; T < 7.f; T += Frame72) Director->AdvanceDirector(Frame72);
	TestEqual(TEXT("Boss death completes the director's boss phase"), Director->GetPhaseIndex(), 1);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
