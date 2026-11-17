// Automation tests for UETMInterruptionSubsystem. These tick the whole world (World->Tick), as the game does,
// so they show the engine's pause really freezing enemy attacks, the director, the brace and cannon heat.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Platform.Interruptions; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Platform/InterruptionSubsystem.h"
#include "Combat/DefenseComponent.h"
#include "Combat/IntegrityComponent.h"
#include "Encounters/EncounterDirector.h"
#include "Enemies/Base/EnemyBase.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/CoreDelegates.h"
#include "Tests/AutomationCommon.h"

namespace
{
	const EAutomationTestFlags ETMPauseTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;

	/** A real game world (game instance, game mode, game state) from the engine's test helper, ticked as frames. */
	struct FTestWorld
	{
		FTestWorldWrapper Wrapper;
		UWorld* World = nullptr;
		UETMInterruptionSubsystem* Pause = nullptr;
		AActor* RigActor = nullptr;
		UIntegrityComponent* Rig = nullptr;
		UDefenseComponent* Brace = nullptr;
		AEncounterDirector* Director = nullptr;
		AEnemyBase* Enemy = nullptr;

		FTestWorld()
		{
			Wrapper.CreateTestWorld(EWorldType::Game);
			Wrapper.BeginPlayInTestWorld();
			World = Wrapper.GetTestWorld();
			Pause = World->GetSubsystem<UETMInterruptionSubsystem>();

			World->SpawnActor<AMissionObjective>(FVector(600.f, 0.f, 0.f), FRotator::ZeroRotator);
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
			World->GetSubsystem<UMissionStateSubsystem>()->RegisterRig(Rig, Brace);

			Director = World->SpawnActor<AEncounterDirector>();
			Director->RigActor = RigActor;
			FEncounterPhase Phase;
			Phase.Name = TEXT("Hold");
			Phase.SlotSeconds = 600.f;
			Director->Phases = { Phase };
			Director->StartEncounter(1);

			// An enemy that ticks with the world and is ready to attack.
			Enemy = World->SpawnActor<AEnemyBase>(FVector(2000.f, 0.f, 0.f), FRotator::ZeroRotator);
			Enemy->MarkPooled();
			Enemy->Activate(Enemy->GetActorTransform());
		}

		~FTestWorld()
		{
			Wrapper.DestroyTestWorld(/*bForceGarbageCollect*/ false);
		}

		void Tick(float Seconds)
		{
			for (float T = 0.f; T < Seconds - KINDA_SMALL_NUMBER; T += Frame72) Wrapper.TickTestWorld(Frame72);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMPauseFreezesTest, "ETM.Platform.Interruptions.MenuPauseFreezesTimersAndDamage", ETMPauseTestFlags)
bool FETMPauseFreezesTest::RunTest(const FString&)
{
	FTestWorld W;
	TestFalse(TEXT("Test world built"), W.Wrapper.HasFailed());
	TestNotNull(TEXT("Interruption subsystem exists"), W.Pause);
	if (!W.Pause) return false;

	W.Tick(1.f);
	TestEqual(TEXT("World ticks the enemy into its approach"), W.Enemy->GetState(), EEnemyState::Approaching);
	TestTrue(TEXT("Director's mission clock runs"), W.Director->GetMissionTime() > 0.9f);

	// A 2 s attack and an active brace, half way through.
	W.Enemy->BeginAttack(W.RigActor, 40.f, 2.f);
	W.Brace->TryActivateBrace();
	W.Tick(1.f);
	const float AttackLeft = W.Enemy->GetAttackTimeRemaining();
	const float BraceLeft = W.Brace->GetActiveRemaining();
	const float MissionTime = W.Director->GetMissionTime();

	W.Pause->OpenPauseMenu();
	TestTrue(TEXT("Paused immediately"), W.Pause->IsGameplayPaused() && W.World->IsPaused());
	W.Tick(20.f);
	TestEqual(TEXT("Attack timer frozen"), W.Enemy->GetAttackTimeRemaining(), AttackLeft);
	TestEqual(TEXT("No damage while paused"), W.Rig->GetCurrent(), 300.f);
	TestEqual(TEXT("Brace timer frozen"), W.Brace->GetActiveRemaining(), BraceLeft);
	TestEqual(TEXT("Mission clock frozen"), W.Director->GetMissionTime(), MissionTime);

	// Resume: a 3 s countdown during which nothing moves yet.
	W.Pause->ResumeFromMenu();
	TestEqual(TEXT("Countdown first"), W.Pause->GetPlayState(), EETMPlayState::Countdown);
	W.Tick(2.8f);
	TestTrue(TEXT("Still frozen during the countdown"), W.World->IsPaused() && W.Enemy->GetAttackTimeRemaining() == AttackLeft);
	W.Tick(0.3f);
	TestEqual(TEXT("Running after the countdown"), W.Pause->GetPlayState(), EETMPlayState::Running);
	TestFalse(TEXT("World unpaused"), W.World->IsPaused());

	// The state is exactly as it was: the attack still lands, braced, after its remaining second.
	W.Tick(AttackLeft + 0.05f);
	TestTrue(FString::Printf(TEXT("Attack resumes and lands braced (rig %.2f)"), W.Rig->GetCurrent()), FMath::IsNearlyEqual(W.Rig->GetCurrent(), 290.f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMPauseHeadsetTest, "ETM.Platform.Interruptions.HeadsetRemovalAndSystem", ETMPauseTestFlags)
bool FETMPauseHeadsetTest::RunTest(const FString&)
{
	FTestWorld W;
	if (!W.Pause) return false;
	W.Tick(0.5f);

	// Headset off: immediate pause. On again: countdown, then play.
	FCoreDelegates::VRHeadsetRemovedFromHead.Broadcast();
	TestTrue(TEXT("Headset removal pauses"), W.Pause->HasReason(EETMPauseReason::HeadsetRemoved) && W.World->IsPaused());
	FCoreDelegates::VRHeadsetPutOnHead.Broadcast();
	TestEqual(TEXT("Headset back on: countdown"), W.Pause->GetPlayState(), EETMPlayState::Countdown);

	// Taking it off again during the countdown cancels it.
	W.Tick(1.f);
	FCoreDelegates::VRHeadsetRemovedFromHead.Broadcast();
	TestEqual(TEXT("Removal during the countdown pauses again"), W.Pause->GetPlayState(), EETMPlayState::Paused);
	FCoreDelegates::VRHeadsetPutOnHead.Broadcast();
	TestTrue(TEXT("Countdown restarts from the top"), FMath::IsNearlyEqual(W.Pause->GetCountdownRemaining(), W.Pause->CountdownSeconds));

	// Several reasons: play resumes only when all are gone.
	W.Tick(3.1f);
	W.Pause->OpenPauseMenu();
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.Broadcast();
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.Broadcast();
	TestEqual(TEXT("Back from the background with the menu still open: stays paused"), W.Pause->GetPlayState(), EETMPlayState::Paused);
	W.Pause->ToggleMenuPause();
	TestEqual(TEXT("Closing the menu starts the countdown"), W.Pause->GetPlayState(), EETMPlayState::Countdown);

	FCoreDelegates::VRHeadsetLost.Broadcast();
	TestTrue(TEXT("Lost headset pauses"), W.Pause->HasReason(EETMPauseReason::Background));
	FCoreDelegates::VRHeadsetReconnected.Broadcast();
	W.Tick(3.1f);
	TestEqual(TEXT("Reconnected: running again"), W.Pause->GetPlayState(), EETMPlayState::Running);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMPauseTrackingTest, "ETM.Platform.Interruptions.TrackingLossWithGrace", ETMPauseTestFlags)
bool FETMPauseTrackingTest::RunTest(const FString&)
{
	FTestWorld W;
	if (!W.Pause) return false;
	UETMInterruptionSubsystem* P = W.Pause;

	P->UpdateTrackingWatch(false, false, false, 1.f);
	TestFalse(TEXT("No headset (editor): never pauses for tracking"), P->IsGameplayPaused());
	P->UpdateTrackingWatch(true, false, false, 1.f);
	TestFalse(TEXT("Controllers not yet found at start-up: no pause"), P->IsGameplayPaused());
	P->UpdateTrackingWatch(true, true, true, Frame72);

	P->UpdateTrackingWatch(true, true, false, 0.1f);
	TestFalse(TEXT("A 0.1 s dropout is ignored"), P->IsGameplayPaused());
	P->UpdateTrackingWatch(true, true, true, Frame72);
	P->UpdateTrackingWatch(true, false, true, 0.2f);
	P->UpdateTrackingWatch(true, false, true, 0.2f);
	TestTrue(TEXT("Losing one controller for 0.4 s pauses"), P->HasReason(EETMPauseReason::TrackingLost));
	P->UpdateTrackingWatch(true, true, true, Frame72);
	TestEqual(TEXT("Tracking back: countdown (bible: after tracking returns)"), P->GetPlayState(), EETMPlayState::Countdown);

	// Pause-menu recenter is only offered while paused.
	W.Tick(3.1f);
	TestFalse(TEXT("No recenter during play"), P->RequestRecenter());
	TestFalse(TEXT("No re-anchor during play"), P->RequestForwardReAnchor());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
