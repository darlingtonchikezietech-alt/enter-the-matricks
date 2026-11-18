// Automation tests for UCannonFeedbackComponent and its wiring on the rig pawn.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Weapons.Feedback; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Weapons/Feedback/CannonFeedbackComponent.h"
#include "Weapons/Feedback/Tests/FeedbackTestListener.h"
#include "Weapons/CannonComponent.h"
#include "Player/XRSeatedRigPawn.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	const EAutomationTestFlags ETMFeedbackTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;

	struct FTestWorld
	{
		UWorld* World = nullptr;
		AActor* Rig = nullptr;
		UCannonComponent* Cannon = nullptr;
		UCannonFeedbackComponent* Feedback = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ETMFeedbackTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true); // no game mode, so mark it begun or spawned actors skip BeginPlay
		}

		~FTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->SetBegunPlay(false);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		/** A rig stand-in with one cannon at 1 m, facing +X, and its feedback. */
		void MakeCannon()
		{
			Rig = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Rig);
			Rig->SetRootComponent(Root);
			Root->RegisterComponent();
			Cannon = NewObject<UCannonComponent>(Rig);
			Cannon->SetupAttachment(Root);
			Cannon->SetRelativeLocation(FVector(0.f, 0.f, 100.f));
			Cannon->RegisterComponent();
			Feedback = NewObject<UCannonFeedbackComponent>(Rig);
			Feedback->bLeftHand = true;
			Feedback->SetupAttachment(Cannon);
			Feedback->RegisterComponent();
		}

		AActor* MakeTarget(const FVector& Location, FName Tag = NAME_None) const
		{
			AActor* Target = World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Target);
			Target->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(50.f));
			Box->SetCollisionProfileName(TEXT("BlockAll"));
			if (!Tag.IsNone()) Box->ComponentTags.Add(Tag);
			Box->RegisterComponent();
			Target->SetActorLocation(Location);
			return Target;
		}

		void FireOnce() const
		{
			Cannon->SetTriggerHeld(true);
			Cannon->AdvanceCannon(Frame72);
			Cannon->SetTriggerHeld(false);
		}

		void Animate(float Seconds) const
		{
			for (float T = 0.f; T < Seconds - KINDA_SMALL_NUMBER; T += Frame72) Feedback->AdvanceFeedback(Frame72);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMFeedbackShotTest, "ETM.Weapons.Feedback.SevenPartShot", ETMFeedbackTestFlags)
bool FETMFeedbackShotTest::RunTest(const FString&)
{
	FTestWorld W;
	W.MakeCannon();
	W.MakeTarget(FVector(2000.f, 0.f, 100.f), TEXT("ETM.WeakPoint"));
	UETMFeedbackTestListener* Listener = NewObject<UETMFeedbackTestListener>();
	W.Feedback->OnImpact.AddDynamic(Listener, &UETMFeedbackTestListener::HandleImpact);
	const int32 Visuals = W.Feedback->GetCreatedVisualCount();
	TestTrue(TEXT("Visuals built once at BeginPlay"), Visuals > 0);

	W.FireOnce();
	TestTrue(TEXT("The cannon's shot hit"), W.Cannon->GetLastShot().bHit);

	W.Feedback->AdvanceFeedback(0.001f);
	TestTrue(FString::Printf(TEXT("1+5. Barrel and mount kick back (%.1f cm)"), W.Feedback->GetRecoilOffset().X), W.Feedback->GetRecoilOffset().X < -5.f);
	TestTrue(TEXT("2. Muzzle discharge shows"), W.Feedback->IsMuzzleFlashVisible());
	TestEqual(TEXT("3. One tracer in flight"), W.Feedback->GetActiveTracerCount(), 1);
	TestEqual(TEXT("4. Impact flash on the hit"), W.Feedback->GetActiveImpactCount(), 1);
	TestEqual(TEXT("4. Debris hook fired once"), Listener->Impacts, 1);
	TestTrue(TEXT("4. ... at the impact point, marked as a weak point"), Listener->LastLocation.Equals(FVector(1950.f, 0.f, 100.f), 1.f) && Listener->bLastWeakPoint);
	TestTrue(TEXT("6. Haptic pulse"), FMath::IsNearlyEqual(W.Feedback->GetLastHaptic().Amplitude, 0.8f));
	TestTrue(TEXT("6. ... on this cannon's hand"), W.Feedback->GetLastHaptic().bLeftHand);
	TestEqual(TEXT("7. Report played"), W.Feedback->GetReportsPlayed(), 1);

	// The tracer races out from the muzzle and its tail follows.
	FVector Tail, Head;
	TestTrue(TEXT("Tracer segment exists"), W.Feedback->GetTracerSegment(0, Tail, Head));
	TestTrue(TEXT("Tracer starts at the muzzle"), Tail.Equals(W.Cannon->GetLastShot().MuzzleLocation, 1.f));
	W.Animate(0.04f);
	W.Feedback->GetTracerSegment(0, Tail, Head);
	TestTrue(TEXT("Head reaches the impact at half its life"), Head.Equals(W.Cannon->GetLastShot().EndLocation, 30.f));

	// Everything settles after a short pause in firing.
	W.Animate(0.3f);
	TestTrue(TEXT("Recoil settles"), W.Feedback->GetRecoilOffset().IsNearlyZero(0.01f));
	TestFalse(TEXT("Muzzle flash gone"), W.Feedback->IsMuzzleFlashVisible());
	TestEqual(TEXT("Tracer gone"), W.Feedback->GetActiveTracerCount(), 0);
	TestEqual(TEXT("Impact gone"), W.Feedback->GetActiveImpactCount(), 0);
	TestFalse(TEXT("Stops ticking when idle"), W.Feedback->IsComponentTickEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMFeedbackSettingsTest, "ETM.Weapons.Feedback.HapticsAndReducedFlash", ETMFeedbackTestFlags)
bool FETMFeedbackSettingsTest::RunTest(const FString&)
{
	FTestWorld W;
	W.MakeCannon();
	W.MakeTarget(FVector(2000.f, 0.f, 100.f));

	W.Feedback->HapticStrength = 0.f;
	W.FireOnce();
	TestEqual(TEXT("Haptics off sends nothing"), W.Feedback->GetLastHaptic().Amplitude, 0.f);
	W.Animate(0.3f);

	W.Feedback->HapticStrength = 0.5f;
	W.Cannon->ResetHeat();
	W.FireOnce();
	TestTrue(TEXT("Half strength halves the pulse"), FMath::IsNearlyEqual(W.Feedback->GetLastHaptic().Amplitude, 0.4f));
	W.Animate(0.3f);

	// Reduced flash: the muzzle and impact flashes shrink and end sooner; the tracer stays (it shows where the shot went).
	W.Feedback->bReducedFlash = true;
	W.Cannon->ResetHeat();
	W.FireOnce();
	W.Feedback->AdvanceFeedback(0.001f);
	TestEqual(TEXT("Tracer still drawn on reduced flash"), W.Feedback->GetActiveTracerCount(), 1);
	W.Animate(0.04f);
	TestFalse(TEXT("Reduced muzzle flash is shorter"), W.Feedback->IsMuzzleFlashVisible());
	W.Animate(0.3f);

	// Rapid fire (10 shots/s): the muzzle flash shows on the first shot and then at most every 0.25 s, never
	// on every shot, and it sits 40 cm past the muzzle, away from the face (owner headset test, T-0056).
	W.Feedback->bReducedFlash = false;
	W.Cannon->ResetHeat();
	const int32 FlashesBefore = W.Feedback->GetMuzzleFlashesShown();
	const int32 ShotsBefore = W.Cannon->GetShotsFired();
	W.Cannon->SetTriggerHeld(true);
	for (float T = 0.f; T < 3.f; T += Frame72)
	{
		W.Cannon->AdvanceCannon(Frame72);
		W.Feedback->AdvanceFeedback(Frame72);
	}
	W.Cannon->SetTriggerHeld(false);
	const int32 Shots = W.Cannon->GetShotsFired() - ShotsBefore;
	const int32 Flashes = W.Feedback->GetMuzzleFlashesShown() - FlashesBefore;
	TestTrue(FString::Printf(TEXT("Rapid fire: %d flashes for %d shots"), Flashes, Shots), Shots >= 28 && Flashes >= 8 && Flashes * 2 < Shots);
	TestTrue(TEXT("Flash sits well past the muzzle"), W.Feedback->GetMuzzleFlashOffset().X >= W.Cannon->MuzzleOffset.X + 30.f);
	W.Animate(0.5f);

	// Overheat: one long, full-strength buzz (scaled by the setting).
	W.Feedback->HapticStrength = 1.f;
	W.Cannon->ResetHeat();
	W.Cannon->SetTriggerHeld(true);
	for (int32 Frame = 0; Frame < 72 * 11 && !W.Cannon->IsOverheated(); ++Frame) W.Cannon->AdvanceCannon(Frame72);
	W.Cannon->SetTriggerHeld(false);
	TestTrue(TEXT("Cannon locked"), W.Cannon->IsOverheated());
	TestEqual(TEXT("Overheat buzz is full strength"), W.Feedback->GetLastHaptic().Amplitude, 1.f);
	TestEqual(TEXT("Overheat buzz is long"), W.Feedback->GetLastHaptic().Duration, W.Feedback->OverheatHapticSeconds);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMFeedbackPoolTest, "ETM.Weapons.Feedback.PooledAndMisses", ETMFeedbackTestFlags)
bool FETMFeedbackPoolTest::RunTest(const FString&)
{
	FTestWorld W;
	W.MakeCannon();
	const int32 Visuals = W.Feedback->GetCreatedVisualCount();

	// A miss: tracer to the end of the 60 m trace, no impact flash.
	W.FireOnce();
	W.Feedback->AdvanceFeedback(0.001f);
	TestEqual(TEXT("Miss still draws a tracer"), W.Feedback->GetActiveTracerCount(), 1);
	TestEqual(TEXT("Miss makes no impact flash"), W.Feedback->GetActiveImpactCount(), 0);

	// Two minutes of burst fire with venting: nothing new is created, the pools never overflow.
	W.MakeTarget(FVector(1500.f, 0.f, 100.f));
	int32 MaxTracers = 0;
	for (float T = 0.f; T < 120.f; T += Frame72)
	{
		const bool bVent = W.Cannon->GetHeatNormalized() > 0.8f || W.Cannon->IsOverheated();
		W.Cannon->SetTriggerHeld(!bVent);
		W.Cannon->SetVentHeld(bVent);
		W.Cannon->AdvanceCannon(Frame72);
		W.Feedback->AdvanceFeedback(Frame72);
		MaxTracers = FMath::Max(MaxTracers, W.Feedback->GetActiveTracerCount());
	}
	TestTrue(FString::Printf(TEXT("Hundreds of shots fired (%d)"), W.Cannon->GetShotsFired()), W.Cannon->GetShotsFired() > 200);
	TestEqual(TEXT("No visual created per shot"), W.Feedback->GetCreatedVisualCount(), Visuals);
	TestTrue(TEXT("Tracer pool never overflows"), MaxTracers <= W.Feedback->TracerPoolSize);
	TestEqual(TEXT("A report per shot"), W.Feedback->GetReportsPlayed(), W.Cannon->GetShotsFired());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMFeedbackPawnTest, "ETM.Weapons.Feedback.RigPawnNeverMovesCamera", ETMFeedbackTestFlags)
bool FETMFeedbackPawnTest::RunTest(const FString&)
{
	FTestWorld W;
	AXRSeatedRigPawn* Pawn = W.World->SpawnActor<AXRSeatedRigPawn>();
	TestNotNull(TEXT("Left feedback exists"), Pawn->LeftFeedback.Get());
	TestNotNull(TEXT("Right feedback exists"), Pawn->RightFeedback.Get());
	if (!Pawn->LeftFeedback || !Pawn->RightFeedback) return false;
	TestTrue(TEXT("Left feedback rides on the left cannon"), Pawn->LeftFeedback->GetAttachParent() == Pawn->LeftCannon);
	TestTrue(TEXT("Right feedback rides on the right cannon"), Pawn->RightFeedback->GetAttachParent() == Pawn->RightCannon);
	TestTrue(TEXT("Left feedback drives the left controller"), Pawn->LeftFeedback->bLeftHand);
	TestFalse(TEXT("Right feedback drives the right controller"), Pawn->RightFeedback->bLeftHand);

	const FTransform CameraBefore = Pawn->Camera->GetRelativeTransform();
	const FTransform OriginBefore = Pawn->VROrigin->GetRelativeTransform();
	const FTransform ActorBefore = Pawn->GetActorTransform();

	// Fire the left cannon for two seconds with the full presentation running.
	Pawn->SetTrigger(true, true);
	for (float T = 0.f; T < 2.f; T += Frame72)
	{
		Pawn->LeftCannon->AdvanceCannon(Frame72);
		Pawn->LeftFeedback->AdvanceFeedback(Frame72);
		TestTrue(TEXT("Camera never moves during fire (bible section 11)"), Pawn->Camera->GetRelativeTransform().Equals(CameraBefore, 0.001f));
		if (!Pawn->Camera->GetRelativeTransform().Equals(CameraBefore, 0.001f)) break;
	}
	Pawn->SetTrigger(true, false);
	TestTrue(TEXT("Shots were fired"), Pawn->LeftCannon->GetShotsFired() > 4);
	TestTrue(TEXT("XR origin never moves"), Pawn->VROrigin->GetRelativeTransform().Equals(OriginBefore, 0.001f));
	TestTrue(TEXT("Pawn never moves"), Pawn->GetActorTransform().Equals(ActorBefore, 0.001f));
	TestTrue(TEXT("Left hand felt it"), Pawn->LeftFeedback->GetLastHaptic().Amplitude > 0.f);
	TestEqual(TEXT("Right hand did not"), Pawn->RightFeedback->GetLastHaptic().Amplitude, 0.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
