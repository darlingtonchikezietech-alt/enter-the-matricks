// Automation tests for the campaign flow (T-0031): the full loop start -> L1 -> results -> relocation fade ->
// L2 -> ending -> campaign results -> title, run tracking for results, and the sandbox building each stage
// with menu panels that the cannons press.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Core.Flow; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/Flow/CampaignFlow.h"
#include "Core/Flow/ETMMenuPanel.h"
#include "Core/Game/ETMSandboxGameMode.h"
#include "Combat/DefenseComponent.h"
#include "Encounters/EncounterDirector.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Player/XRSeatedRigPawn.h"
#include "Weapons/CannonComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Tests/AutomationCommon.h"

namespace
{
	const EAutomationTestFlags ETMFlowTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;

	/** A flow whose map loads are recorded and finish at once. */
	UETMCampaignFlow* MakeFlow(TArray<FString>& OutLoads)
	{
		UETMCampaignFlow* Flow = NewObject<UETMCampaignFlow>();
		Flow->LevelMaps = { TEXT("/Game/L1"), TEXT("/Game/L2") };
		Flow->TitleMap = TEXT("/Game/Title");
		Flow->PracticeMap = TEXT("/Game/Practice");
		Flow->Loader = [Flow, &OutLoads](const FString& Map)
		{
			OutLoads.Add(Map);
			Flow->NotifyWorldReady(nullptr);
		};
		return Flow;
	}

	void RunFlow(UETMCampaignFlow* Flow, float Seconds)
	{
		for (float T = 0.f; T < Seconds; T += Frame72) Flow->Tick(Frame72);
	}

	/** Fires one cannon of the rig at a world point: aims, lets the servo settle, then fires one frame. */
	void ShootAt(UCannonComponent* Cannon, const FVector& Point)
	{
		Cannon->SetAimDirection((Point - Cannon->GetComponentLocation()).GetSafeNormal());
		for (float T = 0.f; T < 1.f; T += Frame72) Cannon->AdvanceCannon(Frame72);
		Cannon->ResetHeat();
		Cannon->SetTriggerHeld(true);
		Cannon->AdvanceCannon(Frame72);
		Cannon->SetTriggerHeld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMFlowLoopTest, "ETM.Core.Flow.FullLoop", ETMFlowTestFlags)
bool FETMFlowLoopTest::RunTest(const FString&)
{
	TArray<FString> Loads;
	UETMCampaignFlow* Flow = MakeFlow(Loads);
	TArray<EETMFlowStage> Seen;
	TestEqual(TEXT("Boots into the title"), Flow->GetStage(), EETMFlowStage::Title);
	TestFalse(TEXT("Continue means nothing on the title"), Flow->Continue());
	TestFalse(TEXT("Nor does returning to it"), Flow->ReturnToTitle());

	// Start: fade out, load Level 1, fade in.
	TestTrue(TEXT("Start campaign"), Flow->StartCampaign());
	TestTrue(TEXT("Fading"), Flow->IsTransitioning());
	TestEqual(TEXT("Heading for Level 1"), Flow->GetStageToBuild(), EETMFlowStage::Level);
	RunFlow(Flow, Flow->FadeSeconds * 0.5f);
	TestTrue(FString::Printf(TEXT("Half way out (alpha %.2f)"), Flow->GetFadeAlpha()), Flow->GetFadeAlpha() > 0.4f && Flow->GetFadeAlpha() < 0.6f);
	TestEqual(TEXT("Nothing loaded before fully black"), Loads.Num(), 0);
	RunFlow(Flow, Flow->FadeSeconds * 0.5f + 0.05f);
	TestEqual(TEXT("Level 1 map loaded once"), Loads, TArray<FString>{ TEXT("/Game/L1") });
	TestEqual(TEXT("In Level 1"), Flow->GetStage(), EETMFlowStage::Level);
	TestEqual(TEXT("Level index 0"), Flow->GetLevelIndex(), 0);
	TestTrue(TEXT("Fading back in"), Flow->GetFadeAlpha() > 0.9f);
	RunFlow(Flow, Flow->FadeSeconds + 0.05f);
	TestEqual(TEXT("Fully faded in"), Flow->GetFadeAlpha(), 0.f);

	// Level 1 won: brief results in place (no load), then relocation to Level 2.
	Flow->BeginLevel(nullptr, nullptr);
	TestTrue(TEXT("Level 1 complete"), Flow->NotifyLevelComplete(nullptr));
	TestEqual(TEXT("Brief results"), Flow->GetStage(), EETMFlowStage::LevelResults);
	TestTrue(TEXT("Result recorded as completed"), Flow->GetLevelResult(0).bCompleted);
	TestEqual(TEXT("Result is for L1"), Flow->GetLevelResult(0).Level, FName(TEXT("L1")));
	TestEqual(TEXT("Completion alone scores 5000"), Flow->GetLevelResult(0).Score, 5000);
	TestFalse(TEXT("A second completion is ignored"), Flow->NotifyLevelComplete(nullptr));
	TestTrue(TEXT("Continue"), Flow->Continue());
	RunFlow(Flow, 2.f * Flow->FadeSeconds + 0.1f);
	TestEqual(TEXT("Level 2 loaded"), Loads.Last(), FString(TEXT("/Game/L2")));
	TestEqual(TEXT("In Level 2"), Flow->GetStage(), EETMFlowStage::Level);
	TestEqual(TEXT("Level index 1"), Flow->GetLevelIndex(), 1);

	// Level 2 won: the ending, which gives way to the campaign results by itself.
	Flow->BeginLevel(nullptr, nullptr);
	TestTrue(TEXT("Level 2 complete"), Flow->NotifyLevelComplete(nullptr));
	TestEqual(TEXT("Ending"), Flow->GetStage(), EETMFlowStage::Ending);
	RunFlow(Flow, Flow->EndingSeconds - 1.f);
	TestEqual(TEXT("Ending still holding"), Flow->GetStage(), EETMFlowStage::Ending);
	RunFlow(Flow, 1.1f);
	TestEqual(TEXT("Campaign results"), Flow->GetStage(), EETMFlowStage::CampaignResults);
	TestEqual(TEXT("Campaign score sums both levels"), Flow->GetCampaignScore(), 10000);
	TestEqual(TEXT("No map loads during the ending or results"), Loads.Num(), 2);

	// Back to the title.
	TestTrue(TEXT("Return to title"), Flow->ReturnToTitle());
	RunFlow(Flow, 2.f * Flow->FadeSeconds + 0.1f);
	TestEqual(TEXT("Title map loaded"), Loads.Last(), FString(TEXT("/Game/Title")));
	TestEqual(TEXT("At the title again"), Flow->GetStage(), EETMFlowStage::Title);

	// The ending can be skipped; practice goes to its own map and back.
	TestTrue(TEXT("Practice"), Flow->StartPractice());
	RunFlow(Flow, 2.f * Flow->FadeSeconds + 0.1f);
	TestEqual(TEXT("Practice map"), Loads.Last(), FString(TEXT("/Game/Practice")));
	TestEqual(TEXT("Practising"), Flow->GetStage(), EETMFlowStage::Practice);
	TestFalse(TEXT("Practice does not complete levels"), Flow->NotifyLevelComplete(nullptr));
	TestTrue(TEXT("Leave practice"), Flow->ReturnToTitle());
	RunFlow(Flow, 2.f * Flow->FadeSeconds + 0.1f);
	TestEqual(TEXT("Title"), Flow->GetStage(), EETMFlowStage::Title);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMFlowScoreTest, "ETM.Core.Flow.ScoreAndFormatting", ETMFlowTestFlags)
bool FETMFlowScoreTest::RunTest(const FString&)
{
	FETMMissionResult R;
	TestEqual(TEXT("Nothing done scores 0"), ETMFlow::ComputeScore(R), 0);
	R.bCompleted = true;
	R.ShotsFired = 200;
	R.ShotsHit = 140;
	R.WeakPointHits = 30;
	R.DamagePrevented = 100.f;
	R.Retries = 1;
	// 5000 + 0.7 x 3000 + 30 x 20 + 100 x 5 - 500
	TestEqual(TEXT("Provisional score"), ETMFlow::ComputeScore(R), 5000 + 2100 + 600 + 500 - 500);
	R.Retries = 100;
	TestEqual(TEXT("Never negative"), ETMFlow::ComputeScore(R), 0);
	TestEqual(TEXT("m:ss"), ETMFlow::FormatTime(125.7f), FString(TEXT("2:05")));
	TestEqual(TEXT("Levels have ids"), ETMFlow::LevelId(1), FName(TEXT("L2")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMFlowTrackingTest, "ETM.Core.Flow.RunTracking", ETMFlowTestFlags)
bool FETMFlowTrackingTest::RunTest(const FString&)
{
	FTestWorldWrapper Wrapper;
	Wrapper.CreateTestWorld(EWorldType::Game);
	Wrapper.BeginPlayInTestWorld();
	UWorld* World = Wrapper.GetTestWorld();
	AXRSeatedRigPawn* Rig = World->SpawnActor<AXRSeatedRigPawn>(FVector::ZeroVector, FRotator::ZeroRotator);
	UMissionStateSubsystem* Mission = World->GetSubsystem<UMissionStateSubsystem>();

	TArray<FString> Loads;
	UETMCampaignFlow* Flow = MakeFlow(Loads);
	Flow->StartCampaign();
	RunFlow(Flow, 2.f * Flow->FadeSeconds + 0.1f);
	Flow->BeginLevel(World, Rig);

	// Three shots: a miss, a hit, a weak-point hit. A blocked shot counts as fired, not hit.
	FCannonShot Miss;
	FCannonShot Hit; Hit.bHit = true; Hit.Damage = 25.f;
	FCannonShot Weak; Weak.bHit = true; Weak.bWeakPoint = true; Weak.Damage = 50.f;
	FCannonShot Blocked; Blocked.bBlocked = true;
	Rig->LeftCannon->OnFired.Broadcast(Miss);
	Rig->RightCannon->OnFired.Broadcast(Hit);
	Rig->LeftCannon->OnFired.Broadcast(Weak);
	Rig->RightCannon->OnFired.Broadcast(Blocked);
	Rig->Defense->OnBraceAbsorbedDamage.Broadcast(30.f, 10.f);
	Mission->OnRetryStarted.Broadcast(FMissionCheckpoint());
	for (float T = 0.f; T < 3.f; T += Frame72) Wrapper.TickTestWorld(Frame72);

	TestTrue(TEXT("Level complete"), Flow->NotifyLevelComplete(World));
	const FETMMissionResult R = Flow->GetLevelResult(0);
	TestEqual(TEXT("Shots fired"), R.ShotsFired, 4);
	TestEqual(TEXT("Shots hit"), R.ShotsHit, 2);
	TestEqual(TEXT("Weak-point hits"), R.WeakPointHits, 1);
	TestEqual(TEXT("Damage prevented = brace absorbed"), R.DamagePrevented, 30.f);
	TestEqual(TEXT("Retries"), R.Retries, 1);
	TestTrue(FString::Printf(TEXT("Duration from world time (%.2f s)"), R.DurationSeconds), FMath::IsNearlyEqual(R.DurationSeconds, 3.f, 0.1f));
	TestEqual(TEXT("Score from the result"), R.Score, ETMFlow::ComputeScore(R));

	// Tracking stops with the level: later shots do not change the stored result.
	Rig->LeftCannon->OnFired.Broadcast(Hit);
	TestEqual(TEXT("Stopped tracking"), Flow->GetLevelResult(0).ShotsFired, 4);
	Wrapper.DestroyTestWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMFlowSandboxTest, "ETM.Core.Flow.SandboxStagesAndPanels", ETMFlowTestFlags)
bool FETMFlowSandboxTest::RunTest(const FString&)
{
	TArray<FString> Loads;
	UETMCampaignFlow* Flow = MakeFlow(Loads);

	// --- Title world: a panel with two buttons, no enemies, no objective. ---
	{
		FTestWorldWrapper Wrapper;
		Wrapper.CreateTestWorld(EWorldType::Game);
		Wrapper.BeginPlayInTestWorld();
		UWorld* World = Wrapper.GetTestWorld();
		AETMSandboxGameMode* Mode = World->GetAuthGameMode<AETMSandboxGameMode>();
		if (!Mode) { AddError(TEXT("No sandbox game mode")); return false; }
		// Spawned high and turned, as the Entry map's player start does: the range puts the rig on its floor.
		AXRSeatedRigPawn* Rig = World->SpawnActor<AXRSeatedRigPawn>(FVector(0.f, 0.f, 250.f), FRotator(0.f, 30.f, 0.f));
		Mode->SetFlow(Flow);
		Mode->BuildSandbox();
		TestTrue(TEXT("Rig placed at the range origin, on its mount"), Rig->GetActorLocation().Equals(FVector(0.f, 0.f, Mode->RigBaseHeight), 0.1f) && Rig->GetActorRotation().IsNearlyZero(0.01f));

		AETMMenuPanel* Panel = Mode->GetPanel();
		TestNotNull(TEXT("Title panel"), Panel);
		if (!Panel) return false;
		TestEqual(TEXT("Title"), Panel->GetTitleString(), FString(TEXT("ENTER THE MATRICKS")));
		TestEqual(TEXT("Two buttons"), Panel->GetButtonIds(), TArray<FName>{ TEXT("Start"), TEXT("Practice") });
		TestNull(TEXT("No objective on the title"), Mode->GetSandboxObjective());
		TestTrue(TEXT("No encounter on the title"), !Mode->GetSandboxDirector() || Mode->GetSandboxDirector()->GetState() != EEncounterState::Running);
		const float Distance = FVector::Dist(Panel->GetActorLocation(), Rig->GetActorTransform().TransformPosition(Rig->DesignEyeLocation));
		TestTrue(FString::Printf(TEXT("Panel 1-2 m from the eye (%.0f cm)"), Distance), Distance >= 100.f && Distance <= 200.f);
		const FVector StartAt = Panel->GetButtonBox(TEXT("Start"))->GetComponentLocation();
		const FVector PracticeAt = Panel->GetButtonBox(TEXT("Practice"))->GetComponentLocation();
		TestTrue(TEXT("First button on the player's left"), StartAt.Y < PracticeAt.Y);
		// Both cannons can aim at every button without hitting their pitch or yaw limits.
		for (const FName Id : Panel->GetButtonIds())
		{
			for (UCannonComponent* Cannon : { Rig->LeftCannon.Get(), Rig->RightCannon.Get() })
			{
				const FRotator Aim = (Panel->GetButtonBox(Id)->GetComponentLocation() - Cannon->GetComponentLocation()).Rotation();
				TestTrue(FString::Printf(TEXT("%s reachable (pitch %.1f, yaw %.1f)"), *Id.ToString(), Aim.Pitch, Aim.Yaw),
					Aim.Pitch > -25.f && Aim.Pitch < 40.f && FMath::Abs(Aim.Yaw) < 60.f);
			}
		}

		// Not armed straight away: a held trigger cannot pick.
		TestFalse(TEXT("Not armed at once"), Panel->IsArmed());
		ShootAt(Rig->LeftCannon, StartAt);
		TestEqual(TEXT("Early shot does nothing"), Flow->GetStage(), EETMFlowStage::Title);
		for (float T = 0.f; T < 1.f; T += Frame72) Wrapper.TickTestWorld(Frame72);
		TestTrue(TEXT("Armed after the delay"), Panel->IsArmed());

		// A real cannon shot on the Start button starts the campaign.
		ShootAt(Rig->LeftCannon, StartAt);
		TestTrue(TEXT("The shot hit the button"), Rig->LeftCannon->GetLastShot().bHit && Rig->LeftCannon->GetLastShot().HitActor == Panel);
		TestTrue(TEXT("Shooting START starts the campaign"), Flow->IsTransitioning());
		TestEqual(TEXT("... towards Level 1"), Flow->GetStageToBuild(), EETMFlowStage::Level);
		TestFalse(TEXT("One press per panel"), Panel->Press(TEXT("Practice")));

		// The fade dims the range's light.
		RunFlow(Flow, Flow->FadeSeconds * 0.5f);
		Wrapper.TickTestWorld(Frame72);
		TestTrue(FString::Printf(TEXT("Light dims while fading out (%.2f)"), Mode->GetCurrentLightIntensity()), Mode->GetCurrentLightIntensity() < Mode->LightIntensity * 0.6f);
		RunFlow(Flow, Flow->FadeSeconds * 0.5f + 0.05f);
		Wrapper.DestroyTestWorld(false);
	}
	TestEqual(TEXT("Level 1 requested"), Loads.Last(), FString(TEXT("/Game/L1")));

	// --- Level 1 world: objective, a short encounter, results in place, CONTINUE relocates. ---
	{
		FTestWorldWrapper Wrapper;
		Wrapper.CreateTestWorld(EWorldType::Game);
		Wrapper.BeginPlayInTestWorld();
		UWorld* World = Wrapper.GetTestWorld();
		AETMSandboxGameMode* Mode = World->GetAuthGameMode<AETMSandboxGameMode>();
		if (!Mode) { AddError(TEXT("No sandbox game mode")); return false; }
		AXRSeatedRigPawn* Rig = World->SpawnActor<AXRSeatedRigPawn>(FVector::ZeroVector, FRotator::ZeroRotator);
		World->GetSubsystem<UMissionStateSubsystem>()->RegisterRig(Rig->RigIntegrity, Rig->Defense);
		Mode->SetFlow(Flow);
		Mode->BuildSandbox();

		AEncounterDirector* Director = Mode->GetSandboxDirector();
		TestNotNull(TEXT("Objective"), Mode->GetSandboxObjective());
		TestNull(TEXT("No panel during combat"), Mode->GetPanel());
		if (!Director) return false;
		Director->RigActor = Rig;
		TestEqual(TEXT("Encounter running"), Director->GetState(), EEncounterState::Running);
		TestEqual(TEXT("Stand-in Level 1 has two phases"), Director->Phases.Num(), 2);
		for (float T = 0.f; T < 8.f; T += Frame72) Wrapper.TickTestWorld(Frame72);
		TestTrue(TEXT("Enemies arrive"), Director->GetLiveCount() > 0);

		// The encounter completes (as clearing both phases would): results appear in this world.
		Director->OnEncounterComplete.Broadcast();
		TestEqual(TEXT("Brief results"), Flow->GetStage(), EETMFlowStage::LevelResults);
		AETMMenuPanel* Panel = Mode->GetPanel();
		TestNotNull(TEXT("Results panel"), Panel);
		if (!Panel) return false;
		TestTrue(TEXT("Results title names the level"), Panel->GetTitleString().Contains(TEXT("LEVEL 1 COMPLETE")));
		TestTrue(TEXT("Results show accuracy"), Panel->GetBodyString().Contains(TEXT("Accuracy")));
		TestTrue(TEXT("Run time recorded"), Flow->GetLevelResult(0).DurationSeconds > 7.f);
		for (float T = 0.f; T < 1.f; T += Frame72) Wrapper.TickTestWorld(Frame72);
		TestTrue(TEXT("CONTINUE"), Panel->Press(TEXT("Continue")));
		TestTrue(TEXT("Relocating"), Flow->IsTransitioning());
		RunFlow(Flow, Flow->FadeSeconds + 0.05f);
		Wrapper.DestroyTestWorld(false);
	}
	TestEqual(TEXT("Level 2 requested"), Loads.Last(), FString(TEXT("/Game/L2")));

	// --- Level 2 world: completing it shows the ending, then the campaign results, then back to title. ---
	{
		FTestWorldWrapper Wrapper;
		Wrapper.CreateTestWorld(EWorldType::Game);
		Wrapper.BeginPlayInTestWorld();
		UWorld* World = Wrapper.GetTestWorld();
		AETMSandboxGameMode* Mode = World->GetAuthGameMode<AETMSandboxGameMode>();
		if (!Mode) { AddError(TEXT("No sandbox game mode")); return false; }
		AXRSeatedRigPawn* Rig = World->SpawnActor<AXRSeatedRigPawn>(FVector::ZeroVector, FRotator::ZeroRotator);
		World->GetSubsystem<UMissionStateSubsystem>()->RegisterRig(Rig->RigIntegrity, Rig->Defense);
		Mode->SetFlow(Flow);
		Mode->BuildSandbox();
		if (!Mode->GetSandboxDirector()) return false;
		TestEqual(TEXT("Level index 1"), Flow->GetLevelIndex(), 1);
		Mode->GetSandboxDirector()->OnEncounterComplete.Broadcast();
		TestEqual(TEXT("Ending"), Flow->GetStage(), EETMFlowStage::Ending);
		TestTrue(TEXT("Ending panel carries Mara's closing line"), Mode->GetPanel() && Mode->GetPanel()->GetBodyString().Contains(TEXT("tomorrow")));
		for (float T = 0.f; T < 1.f; T += Frame72) Wrapper.TickTestWorld(Frame72);
		TestTrue(TEXT("SKIP"), Mode->GetPanel() && Mode->GetPanel()->Press(TEXT("Skip")));
		TestEqual(TEXT("Campaign results"), Flow->GetStage(), EETMFlowStage::CampaignResults);
		TestTrue(TEXT("Campaign results panel"), Mode->GetPanel() && Mode->GetPanel()->GetTitleString() == TEXT("HAVEN NINE EVACUATED"));
		for (float T = 0.f; T < 1.f; T += Frame72) Wrapper.TickTestWorld(Frame72);
		TestTrue(TEXT("RETURN TO TITLE"), Mode->GetPanel() && Mode->GetPanel()->Press(TEXT("Title")));
		RunFlow(Flow, 2.f * Flow->FadeSeconds + 0.1f);
		TestEqual(TEXT("Back at the title"), Flow->GetStage(), EETMFlowStage::Title);
		Wrapper.DestroyTestWorld(false);
	}
	TestEqual(TEXT("Loads: L1, L2, title"), Loads, (TArray<FString>{ TEXT("/Game/L1"), TEXT("/Game/L2"), TEXT("/Game/Title") }));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMFlowLevelPhasesTest, "ETM.Core.Flow.StandInLevelsComplete", ETMFlowTestFlags)
bool FETMFlowLevelPhasesTest::RunTest(const FString&)
{
	for (int32 Level = 0; Level < ETMFlow::LevelCount; ++Level)
	{
		const TArray<FEncounterPhase> Phases = AETMSandboxGameMode::MakeLevelPhases(Level);
		TestTrue(FString::Printf(TEXT("L%d has phases"), Level + 1), Phases.Num() >= 2);
		int32 Enemies = 0;
		for (const FEncounterPhase& P : Phases)
		{
			TestTrue(FString::Printf(TEXT("L%d %s completes when cleared"), Level + 1, *P.Name.ToString()), P.bCompleteWhenCleared);
			for (const FEncounterSpawnGroup& G : P.Groups)
			{
				Enemies += G.Count;
				TestTrue(TEXT("Group has an enemy class"), G.EnemyClass != nullptr);
				TestTrue(TEXT("Group uses one of the three lanes"), G.EntranceIndex >= 0 && G.EntranceIndex < 3);
			}
		}
		TestTrue(FString::Printf(TEXT("L%d is finite and short (%d enemies)"), Level + 1, Enemies), Enemies > 0 && Enemies <= 40);
		TestTrue(TEXT("First phase records a start checkpoint for retries"), Phases[0].bCheckpointOnStart);
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
