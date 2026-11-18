// Automation tests for the HUD rules (HudModel.h) and UHUDPresenterComponent on the real rig pawn.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.UI; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "UI/HUDPresenterComponent.h"
#include "UI/HudModel.h"
#include "Player/XRSeatedRigPawn.h"
#include "Bosses/Gatebreaker/Gatebreaker.h"
#include "Combat/DefenseComponent.h"
#include "Combat/IntegrityComponent.h"
#include "Enemies/Base/EnemyBase.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionTypes.h"
#include "Weapons/CannonComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

namespace
{
	const EAutomationTestFlags ETMHudTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;

	struct FTestWorld
	{
		UWorld* World = nullptr;
		AXRSeatedRigPawn* Rig = nullptr;
		UHUDPresenterComponent* Hud = nullptr;
		AMissionObjective* Objective = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ETMHudTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true); // no game mode, so mark it begun or spawned actors skip BeginPlay

			Objective = World->SpawnActor<AMissionObjective>(FVector(600.f, 0.f, -100.f), FRotator::ZeroRotator);
			Rig = World->SpawnActor<AXRSeatedRigPawn>(FVector::ZeroVector, FRotator::ZeroRotator);
			Hud = Rig->HUD;
			Hud->RefreshSources();
			Hud->AdvanceHud(Frame72);
		}

		~FTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->SetBegunPlay(false);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		AEnemyBase* ReadyEnemy(const FVector& Location) const
		{
			AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(Location, FRotator::ZeroRotator);
			Enemy->MarkPooled();
			Enemy->bUsesAttackToken = false; // the HUD test needs more simultaneous attacks than the 2-token cap
			Enemy->Activate(FTransform(Location));
			for (float T = 0.f; T < 1.f; T += Frame72) Enemy->AdvanceState(Frame72);
			return Enemy;
		}

		void Run(float Seconds, TArray<AEnemyBase*> Enemies = {}) const
		{
			for (float T = 0.f; T < Seconds - KINDA_SMALL_NUMBER; T += Frame72)
			{
				for (AEnemyBase* Enemy : Enemies) Enemy->AdvanceState(Frame72);
				Rig->Defense->AdvanceBrace(Frame72);
				Hud->AdvanceHud(Frame72);
			}
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMHudModelTest, "ETM.UI.HudModel.RankingAndPriority", ETMHudTestFlags)
bool FETMHudModelTest::RunTest(const FString&)
{
	int A, B, C, D;
	TArray<ETMHud::FWarning> In = { { &A, 3.f, false }, { &B, 1.f, false }, { &C, 1.f, true }, { &D, 2.f, false } };
	const TArray<ETMHud::FWarning> Out = ETMHud::RankWarnings(In, 3);
	TestEqual(TEXT("Capped at 3 (limit simultaneous messages)"), Out.Num(), 3);
	TestTrue(TEXT("Soonest first; on a tie the rig attack wins"), Out[0].SourceKey == &C && Out[1].SourceKey == &B && Out[2].SourceKey == &D);

	ETMHud::FSecondaryLine Line;
	Line.Show(FText::FromString(TEXT("+100")), 2.f);
	TestTrue(TEXT("Secondary text shows"), Line.IsVisible(false));
	TestFalse(TEXT("Critical warning hides it"), Line.IsVisible(true));
	Line.Advance(2.1f);
	TestFalse(TEXT("It expires even while hidden"), Line.IsVisible(false));

	TestFalse(TEXT("Straight ahead is in view"), ETMHud::IsOutsideView(FVector::ZeroVector, FVector::ForwardVector, FVector(1000.f, 100.f, 0.f), 50.f));
	TestTrue(TEXT("To the side is off view"), ETMHud::IsOutsideView(FVector::ZeroVector, FVector::ForwardVector, FVector(100.f, 1000.f, 0.f), 50.f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMHudPlacementTest, "ETM.UI.Presenter.PlacementPerSection11", ETMHudTestFlags)
bool FETMHudPlacementTest::RunTest(const FString&)
{
	FTestWorld W;
	TestNotNull(TEXT("The rig pawn carries a HUD presenter"), W.Hud);
	if (!W.Hud) return false;

	// Frame elements: 1-2 m from the seated eye (bible section 11) and below the central sightline.
	const FVector Eye = W.Rig->GetActorTransform().TransformPosition(W.Rig->DesignEyeLocation);
	const TCHAR* Names[] = { TEXT("Rig integrity (central lower frame)"), TEXT("Brace readiness"), TEXT("Current objective line"), TEXT("Secondary line") };
	const TArray<USceneComponent*> Frame = W.Hud->GetFrameElements();
	for (int32 Index = 0; Index < Frame.Num(); ++Index)
	{
		if (!Frame[Index]) { AddError(FString::Printf(TEXT("%s missing"), Names[Index])); continue; }
		const FVector At = Frame[Index]->GetComponentLocation();
		const float Distance = FVector::Dist(At, Eye);
		TestTrue(FString::Printf(TEXT("%s at %.0f cm from the eye (100-200)"), Names[Index], Distance), Distance >= 100.f && Distance <= 200.f);
		TestTrue(FString::Printf(TEXT("%s is below eye height"), Names[Index]), At.Z < Eye.Z - 20.f);
		TestTrue(FString::Printf(TEXT("%s rides on the rig frame"), Names[Index]), Frame[Index]->GetAttachParent() == W.Rig->RigFrame);
	}
	TestTrue(TEXT("Rig integrity is central"), FMath::Abs(Frame[0]->GetComponentLocation().Y) < 1.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMHudGaugesTest, "ETM.UI.Presenter.HeatRigBraceObjective", ETMHudTestFlags)
bool FETMHudGaugesTest::RunTest(const FString&)
{
	FTestWorld W;

	// Heat: left cannon fires until it locks; only the left housing changes, and the lock bracket appears.
	W.Rig->SetTrigger(true, true);
	for (int32 Frame = 0; Frame < 72 * 11 && !W.Rig->LeftCannon->IsOverheated(); ++Frame) W.Rig->LeftCannon->AdvanceCannon(Frame72);
	W.Rig->SetTrigger(true, false);
	TestTrue(TEXT("Left heat shown full"), W.Hud->GetShownHeat(true) > 0.95f);
	TestEqual(TEXT("Right heat untouched"), W.Hud->GetShownHeat(false), 0.f);
	TestTrue(TEXT("Lock bracket on the left housing (shape cue)"), W.Hud->IsHeatLockShown(true));
	TestFalse(TEXT("No lock bracket on the right"), W.Hud->IsHeatLockShown(false));
	for (int32 Frame = 0; Frame < 72 * 6 && W.Rig->LeftCannon->IsOverheated(); ++Frame) W.Rig->LeftCannon->AdvanceCannon(Frame72);
	TestFalse(TEXT("Bracket clears when the lock does"), W.Hud->IsHeatLockShown(true));

	// Rig integrity on the frame.
	TestEqual(TEXT("Rig shows 300"), W.Hud->GetRigText().ToString(), FString(TEXT("RIG 300")));
	W.Rig->RigIntegrity->ApplyIntegrityDamage(45.f);
	TestEqual(TEXT("Rig shows the hit"), W.Hud->GetRigText().ToString(), FString(TEXT("RIG 255")));

	// Brace readiness: full, empty while up, half way through the 12 s cooldown, full again.
	TestEqual(TEXT("Brace ready"), W.Hud->GetBraceShown(), 1.f);
	W.Rig->ActivateBrace();
	W.Run(Frame72);
	TestEqual(TEXT("Brace in use"), W.Hud->GetBraceShown(), 0.f);
	W.Run(2.f + 6.f);
	TestTrue(FString::Printf(TEXT("Brace half recharged (%.2f)"), W.Hud->GetBraceShown()), FMath::IsNearlyEqual(W.Hud->GetBraceShown(), 0.5f, 0.05f));
	W.Run(6.1f);
	TestEqual(TEXT("Brace ready again"), W.Hud->GetBraceShown(), 1.f);

	// Objective marker above the equipment; it pulses while being hit.
	TestEqual(TEXT("Objective marker shows 100%"), W.Hud->GetObjectiveMarkerText().ToString(), FString(TEXT("POWER 100%")));
	AEnemyBase* Siphon = W.ReadyEnemy(FVector(900.f, 300.f, 0.f));
	Siphon->Tags.Add(ETMObjectiveTags::InfrastructureAttacker);
	Siphon->DealDamageTo(W.Objective, 50.f);
	W.Run(Frame72);
	TestEqual(TEXT("Objective marker shows the damage"), W.Hud->GetObjectiveMarkerText().ToString(), FString(TEXT("POWER 90%")));
	TestTrue(TEXT("Marker alarms while under attack"), W.Hud->IsObjectiveAlarmShown());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMHudWarningsTest, "ETM.UI.Presenter.WarningsAtSourceAndPriority", ETMHudTestFlags)
bool FETMHudWarningsTest::RunTest(const FString&)
{
	FTestWorld W;
	AEnemyBase* Front = W.ReadyEnemy(FVector(2000.f, 0.f, 0.f));
	AEnemyBase* Side = W.ReadyEnemy(FVector(0.f, 2000.f, 0.f));
	AEnemyBase* Far = W.ReadyEnemy(FVector(2500.f, -400.f, 0.f));
	AEnemyBase* Late = W.ReadyEnemy(FVector(2500.f, 400.f, 0.f));
	const TArray<AEnemyBase*> All = { Front, Side, Far, Late };
	W.Hud->RefreshSources();

	W.Hud->ShowSecondaryMessage(FText::FromString(TEXT("SCORE +100")), 6.f);
	W.Run(Frame72, All);
	TestTrue(TEXT("Secondary text shows when nothing is incoming"), W.Hud->IsSecondaryMessageShown());

	Front->BeginAttack(W.Rig, 15.f, 1.f);
	Side->BeginAttack(W.Rig, 15.f, 2.f);
	Far->BeginAttack(W.Rig, 15.f, 3.f);
	Late->BeginAttack(W.Rig, 15.f, 4.f);
	W.Run(Frame72, All);
	TestEqual(TEXT("At most 3 warnings at once"), W.Hud->GetShownWarningCount(), 3);
	TestFalse(TEXT("Critical warnings override secondary text"), W.Hud->IsSecondaryMessageShown());

	FVector At; bool bEdge = false; FString Text;
	TestTrue(TEXT("Most urgent warning shown first"), W.Hud->GetWarningMarker(0, At, bEdge, Text));
	TestTrue(TEXT("... above its source"), FVector::Dist2D(At, Front->GetActorLocation()) < 1.f && At.Z > Front->GetActorLocation().Z + 50.f);
	TestTrue(FString::Printf(TEXT("... with a countdown (%s)"), *Text), Text.StartsWith(TEXT("! 1.0")) || Text.StartsWith(TEXT("! 0.9")));
	TestFalse(TEXT("A source in view needs no edge arrow"), bEdge);
	W.Hud->GetWarningMarker(1, At, bEdge, Text);
	TestTrue(TEXT("A source off to the side gets an edge arrow"), bEdge && FVector::Dist2D(At, Side->GetActorLocation()) < 1.f);

	// The front attack lands; the fourth warning moves up into view.
	W.Run(1.1f, All);
	TestEqual(TEXT("Still 3 shown after one resolves"), W.Hud->GetShownWarningCount(), 3);
	W.Hud->GetWarningMarker(2, At, bEdge, Text);
	TestTrue(TEXT("The waiting warning appears"), FVector::Dist2D(At, Late->GetActorLocation()) < 1.f);

	// Kill the rest: warnings go, and the secondary line comes back while its time lasts.
	Side->Kill(); Far->Kill(); Late->Kill();
	W.Run(Frame72, All);
	TestEqual(TEXT("Warnings end with their attacks"), W.Hud->GetShownWarningCount(), 0);
	TestTrue(TEXT("Secondary text returns"), W.Hud->IsSecondaryMessageShown());
	W.Run(5.f, All);
	TestFalse(TEXT("... until it expires"), W.Hud->IsSecondaryMessageShown());

	// An attack already running before the HUD first saw its enemy still gets a warning.
	AEnemyBase* Unseen = W.World->SpawnActor<AEnemyBase>(FVector(1800.f, 200.f, 0.f), FRotator::ZeroRotator);
	Unseen->MarkPooled();
	Unseen->bUsesAttackToken = false;
	Unseen->Activate(Unseen->GetActorTransform());
	for (float T = 0.f; T < 1.f; T += Frame72) Unseen->AdvanceState(Frame72);
	Unseen->BeginAttack(W.Rig, 15.f, 2.f);
	W.Hud->RefreshSources();
	W.Run(Frame72, { Unseen });
	TestEqual(TEXT("Late-bound attack still warned"), W.Hud->GetShownWarningCount(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMHudBossTest, "ETM.UI.Presenter.BossVulnerabilityOnWeakPoint", ETMHudTestFlags)
bool FETMHudBossTest::RunTest(const FString&)
{
	FTestWorld W;
	AGatebreaker* Boss = W.World->SpawnActor<AGatebreaker>(FVector(3000.f, 0.f, 0.f), FRotator(0.f, 180.f, 0.f));
	Boss->Target = W.Rig;
	W.Hud->RefreshSources();
	Boss->Activate(Boss->GetActorTransform());

	for (float T = 0.f; T < 10.f && !Boss->IsTargetExposed(AGatebreaker::NodeLeftPart); T += Frame72)
	{
		Boss->SimulateStep(Frame72);
		W.Hud->AdvanceHud(Frame72);
	}
	W.Hud->AdvanceHud(Frame72);
	TestEqual(TEXT("One weak-point marker while a node is open"), W.Hud->GetShownBossMarkerCount(), 1);
	FVector At; FString Text;
	TestTrue(TEXT("Marker exists"), W.Hud->GetBossMarker(0, At, Text));
	TestTrue(TEXT("Marker sits on the open node"), FVector::Dist2D(At, Boss->NodeLeft->GetComponentLocation()) < 1.f);
	TestTrue(FString::Printf(TEXT("Marker shows the window (%s)"), *Text), Text.Contains(TEXT("OPEN")));

	for (float T = 0.f; T < 6.f && Boss->IsTargetExposed(AGatebreaker::NodeLeftPart); T += Frame72)
	{
		Boss->SimulateStep(Frame72);
		W.Hud->AdvanceHud(Frame72);
	}
	W.Hud->AdvanceHud(Frame72);
	TestFalse(TEXT("Node closed"), Boss->IsTargetExposed(AGatebreaker::NodeLeftPart));
	bool bNodeMarker = false;
	for (int32 Index = 0; Index < 4; ++Index)
	{
		if (W.Hud->GetBossMarker(Index, At, Text) && FVector::Dist2D(At, Boss->NodeLeft->GetComponentLocation()) < 1.f) bNodeMarker = true;
	}
	TestFalse(TEXT("The node's marker goes when it closes"), bNodeMarker);
	// Anything still marked must be a target that really is open (by now the furnace chamber may be).
	TestEqual(TEXT("Markers match the open targets"), W.Hud->GetShownBossMarkerCount(),
		(Boss->IsTargetExposed(AGatebreaker::ChamberPart) ? 1 : 0) + (Boss->IsTargetExposed(AGatebreaker::NodeRightPart) ? 1 : 0));
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
