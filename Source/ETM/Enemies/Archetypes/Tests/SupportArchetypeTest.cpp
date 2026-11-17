// Automation tests for the Bulwark, Lance Walker, Siphon and Cradle Bomber, and silhouettes for all six enemies.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Enemies.Archetypes; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Enemies/Archetypes/Bulwark.h"
#include "Enemies/Archetypes/LanceWalker.h"
#include "Enemies/Archetypes/Siphon.h"
#include "Enemies/Archetypes/CradleBomber.h"
#include "Enemies/Archetypes/Shardling.h"
#include "Enemies/Archetypes/NeedleDrone.h"
#include "Enemies/Base/EnemyPoolSubsystem.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionTypes.h"
#include "Combat/IntegrityComponent.h"
#include "Combat/DefenseComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const EAutomationTestFlags ETMSupportTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;
	const float WeakShot = 80.f; // 40 x2
	const float BodyShot = 40.f;

	struct FTestWorld
	{
		UWorld* World = nullptr;
		AMissionObjective* Objective = nullptr;
		AActor* RigActor = nullptr;
		UIntegrityComponent* Rig = nullptr;
		UDefenseComponent* Brace = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ETMSupportArchetypeWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true); // no game mode, so mark it begun or spawned actors skip BeginPlay

			Objective = World->SpawnActor<AMissionObjective>(FVector::ZeroVector, FRotator::ZeroRotator);
			RigActor = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(RigActor);
			RigActor->SetRootComponent(Root);
			Root->RegisterComponent();
			RigActor->SetActorLocation(FVector(300.f, 0.f, 120.f));
			Brace = NewObject<UDefenseComponent>(RigActor);
			Brace->RegisterComponent();
			Rig = NewObject<UIntegrityComponent>(RigActor);
			Rig->MaxIntegrity = 300.f;
			Rig->RegisterComponent();
			Rig->SetDefenseComponent(Brace);
			Rig->ResetToMax();
		}

		~FTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->SetBegunPlay(false);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		template <typename T>
		T* Spawn(const FVector& Location) const
		{
			T* Enemy = World->SpawnActor<T>(Location, FRotator::ZeroRotator);
			Enemy->MarkPooled();
			Enemy->Activate(FTransform(Location));
			return Enemy;
		}

		float Shoot(AEnemyBase* Enemy, UPrimitiveComponent* Mesh, float Damage) const
		{
			FHitResult Hit;
			Hit.Component = Mesh;
			return UGameplayStatics::ApplyPointDamage(Enemy, Damage, FVector::ForwardVector, Hit, nullptr, RigActor, nullptr);
		}
	};

	template <typename T>
	void Run(T* Enemy, float Seconds)
	{
		for (float Time = 0.f; Time < Seconds - KINDA_SMALL_NUMBER; Time += Frame72) Enemy->SimulateStep(Frame72);
	}

	template <typename T, typename F>
	bool RunUntil(T* Enemy, F&& Condition, float MaxSeconds)
	{
		for (float Time = 0.f; Time < MaxSeconds; Time += Frame72)
		{
			if (Condition()) return true;
			Enemy->SimulateStep(Frame72);
		}
		return Condition();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMBulwarkTest, "ETM.Enemies.Archetypes.Bulwark.ShieldNodesAndSlam", ETMSupportTestFlags)
bool FETMBulwarkTest::RunTest(const FString&)
{
	FTestWorld W;
	ABulwark* Bulwark = W.Spawn<ABulwark>(FVector(1500.f, 0.f, 0.f));
	TestEqual(TEXT("Bible HP 480"), Bulwark->GetHealth(), 480.f);
	TestEqual(TEXT("Bible node HP 80"), Bulwark->GetPartHealth(ABulwark::LeftNodePart), 80.f);
	TestEqual(TEXT("Bible slam 30"), Bulwark->SlamDamage, 30.f);
	TestTrue(TEXT("Infrastructure attacker"), Bulwark->ActorHasTag(ETMObjectiveTags::InfrastructureAttacker));
	TestTrue(TEXT("Nodes are weak points"), Bulwark->LeftNode->ComponentHasTag(TEXT("ETM.WeakPoint")) && Bulwark->RightNode->ComponentHasTag(TEXT("ETM.WeakPoint")));

	// Shield up: x0.1 through the shield; the body has no collision behind it.
	Run(Bulwark, 0.9f);
	W.Shoot(Bulwark, Bulwark->Shield, BodyShot);
	TestEqual(TEXT("Shield takes x0.1 (P-02)"), Bulwark->GetHealth(), 476.f);
	TestEqual(TEXT("Body is covered while the shield is up"), Bulwark->Body->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	// Walks to the objective and slams it every 4 s, each with a 1 s wind-up.
	TestTrue(TEXT("First slam wind-up starts"), RunUntil(Bulwark, [Bulwark] { return Bulwark->GetState() == EEnemyState::Attacking; }, 20.f));
	TestTrue(TEXT("In contact with the objective"), FVector::Dist2D(Bulwark->GetActorLocation(), W.Objective->GetActorLocation()) <= Bulwark->ContactRange + 1.f);
	Run(Bulwark, 0.5f);
	TestTrue(TEXT("Shield heaves up during the wind-up (shape cue)"), Bulwark->ShieldPivot->GetRelativeRotation().Pitch > 10.0);
	TestEqual(TEXT("No damage during the wind-up"), W.Objective->Integrity->GetCurrent(), 500.f);
	Run(Bulwark, 0.6f);
	TestEqual(TEXT("Slam 30"), W.Objective->Integrity->GetCurrent(), 470.f);
	Run(Bulwark, 3.0f);
	TestEqual(TEXT("Nothing between slams"), W.Objective->Integrity->GetCurrent(), 470.f);
	Run(Bulwark, 1.0f);
	TestEqual(TEXT("Second slam 4 s after the first"), W.Objective->Integrity->GetCurrent(), 440.f);
	TestEqual(TEXT("The rig is never hit"), W.Rig->GetCurrent(), 300.f);

	// One weak-point shot breaks a node: the shield drops, the body opens, and a wind-up is cancelled.
	TestTrue(TEXT("Third wind-up"), RunUntil(Bulwark, [Bulwark] { return Bulwark->GetState() == EEnemyState::Attacking; }, 5.f));
	W.Shoot(Bulwark, Bulwark->LeftNode, WeakShot);
	TestTrue(TEXT("Node broken"), Bulwark->IsPartBroken(ABulwark::LeftNodePart));
	TestFalse(TEXT("Shield dropped"), Bulwark->IsShieldUp());
	TestFalse(TEXT("Shield no longer shown"), Bulwark->Shield->IsVisible());
	TestEqual(TEXT("Breaking a node staggers it"), Bulwark->GetState(), EEnemyState::Staggered);
	Run(Bulwark, 0.5f);
	TestEqual(TEXT("The cancelled slam never lands"), W.Objective->Integrity->GetCurrent(), 440.f);
	TestEqual(TEXT("Body is open"), Bulwark->Body->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	const float Before = Bulwark->GetHealth();
	W.Shoot(Bulwark, Bulwark->Body, BodyShot);
	TestEqual(TEXT("Open body takes full damage"), Before - Bulwark->GetHealth(), BodyShot);
	TestEqual(TEXT("A hit on the dropped shield does nothing special"), W.Shoot(Bulwark, Bulwark->Shield, 0.f), 0.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMLanceTest, "ETM.Enemies.Archetypes.LanceWalker.ChargeAndInterrupt", ETMSupportTestFlags)
bool FETMLanceTest::RunTest(const FString&)
{
	FTestWorld W;
	ALanceWalker* Lance = W.Spawn<ALanceWalker>(FVector(3000.f, 800.f, 0.f));
	Lance->SetTarget(W.RigActor);
	TestEqual(TEXT("Bible HP 320"), Lance->GetHealth(), 320.f);
	TestEqual(TEXT("Bible chamber 160 (2 shots)"), Lance->GetPartHealth(ALanceWalker::ChamberPart), 160.f);
	TestEqual(TEXT("Bible charge 2 s"), Lance->ChargeSeconds, 2.f);
	TestEqual(TEXT("Bible strike 35"), Lance->StrikeDamage, 35.f);
	TestFalse(TEXT("Aimed at the rig: not an infrastructure attacker"), Lance->ActorHasTag(ETMObjectiveTags::InfrastructureAttacker));

	const FVector Start = Lance->GetActorLocation();
	TestTrue(TEXT("Plants and starts charging"), RunUntil(Lance, [Lance] { return Lance->GetState() == EEnemyState::Attacking; }, 20.f));
	TestTrue(TEXT("Planted"), Lance->IsPlanted());
	TestTrue(TEXT("Walked in no further than its advance distance"), FVector::Dist2D(Start, Lance->GetActorLocation()) <= Lance->AdvanceDistance + 1.f);
	TestTrue(TEXT("Chamber exposed while charging"), Lance->IsChamberExposed());
	Run(Lance, 1.f);
	TestTrue(TEXT("Lance swells as it charges (shape cue)"), Lance->Lance->GetRelativeScale3D().Z > 2.5);
	Run(Lance, 1.1f);
	TestEqual(TEXT("Strike 35 on the rig"), W.Rig->GetCurrent(), 265.f);
	TestFalse(TEXT("Chamber closes after the strike"), Lance->IsChamberExposed());
	TestEqual(TEXT("A closed chamber cannot be hit"), W.Shoot(Lance, Lance->Chamber, WeakShot), 0.f);

	// Two weak-point shots on the chamber interrupt the next charge.
	TestTrue(TEXT("Second charge"), RunUntil(Lance, [Lance] { return Lance->GetState() == EEnemyState::Attacking; }, 10.f));
	W.Shoot(Lance, Lance->Chamber, WeakShot);
	TestEqual(TEXT("One shot is not enough"), Lance->GetState(), EEnemyState::Attacking);
	W.Shoot(Lance, Lance->Chamber, WeakShot);
	TestEqual(TEXT("Second shot breaks it: interrupted"), Lance->GetState(), EEnemyState::Staggered);
	Run(Lance, 2.5f);
	TestEqual(TEXT("The interrupted strike never lands"), W.Rig->GetCurrent(), 265.f);

	// Designated target can be the objective; then the objective accepts its strike.
	Lance->SetTarget(W.Objective);
	TestTrue(TEXT("Aimed at the objective: tagged"), Lance->ActorHasTag(ETMObjectiveTags::InfrastructureAttacker));
	TestTrue(TEXT("Charges at the objective"), RunUntil(Lance, [Lance] { return Lance->GetState() == EEnemyState::Attacking; }, 10.f));
	Run(Lance, 2.1f);
	TestEqual(TEXT("Objective takes 35"), W.Objective->Integrity->GetCurrent(), 465.f);
	Lance->SetTarget(W.RigActor);
	TestFalse(TEXT("Retargeted at the rig: tag removed"), Lance->ActorHasTag(ETMObjectiveTags::InfrastructureAttacker));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMSiphonTest, "ETM.Enemies.Archetypes.Siphon.TetherAndDrain", ETMSupportTestFlags)
bool FETMSiphonTest::RunTest(const FString&)
{
	FTestWorld W;
	ASiphon* Siphon = W.Spawn<ASiphon>(FVector(-1200.f, 900.f, 0.f));
	TestEqual(TEXT("Bible HP 200"), Siphon->GetHealth(), 200.f);
	TestEqual(TEXT("Bible drain 5/s"), Siphon->DrainPerSecond, 5.f);
	TestTrue(TEXT("Infrastructure attacker"), Siphon->ActorHasTag(ETMObjectiveTags::InfrastructureAttacker));

	TestTrue(TEXT("Crawls up and starts to latch"), RunUntil(Siphon, [Siphon] { return Siphon->GetState() == EEnemyState::Attacking; }, 20.f));
	TestTrue(TEXT("Tether is out during the latch warning"), Siphon->IsTetherVisible());
	Run(Siphon, 1.4f);
	TestEqual(TEXT("No drain during the warning"), W.Objective->Integrity->GetCurrent(), 500.f);
	Run(Siphon, 0.2f);
	TestTrue(TEXT("Latched and draining"), Siphon->IsDraining());

	Run(Siphon, 4.f);
	TestTrue(FString::Printf(TEXT("Drains about 5/s (lost %.2f in 4 s)"), 500.f - W.Objective->Integrity->GetCurrent()),
		FMath::IsNearlyEqual(500.f - W.Objective->Integrity->GetCurrent(), 20.f, 1.3f));
	TestTrue(TEXT("Tether stays out while draining"), Siphon->IsTetherVisible());

	for (int32 Shot = 0; Shot < 5; ++Shot) W.Shoot(Siphon, Siphon->Body, BodyShot);
	TestEqual(TEXT("Five hits destroy it"), Siphon->GetState(), EEnemyState::Dying);
	const float AtDeath = W.Objective->Integrity->GetCurrent();
	Run(Siphon, 3.f);
	TestEqual(TEXT("Destroying it stops the drain"), W.Objective->Integrity->GetCurrent(), AtDeath);
	TestFalse(TEXT("Tether gone"), Siphon->IsTetherVisible());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMCradleTest, "ETM.Enemies.Archetypes.CradleBomber.PodAndRelease", ETMSupportTestFlags)
bool FETMCradleTest::RunTest(const FString&)
{
	FTestWorld W;
	ACradleBomber* Bomber = W.Spawn<ACradleBomber>(FVector(2000.f, -1500.f, 400.f));
	Bomber->SetTarget(W.RigActor);
	TestEqual(TEXT("Bible HP 160"), Bomber->GetHealth(), 160.f);
	TestEqual(TEXT("Bible pod 80 (1 shot)"), Bomber->GetPartHealth(ACradleBomber::PodPart), 80.f);
	TestEqual(TEXT("Bible warning 2 s"), Bomber->WarningSeconds, 2.f);
	TestEqual(TEXT("Bible impact 40"), Bomber->ImpactDamage, 40.f);

	TestTrue(TEXT("Flies over the rig and warns"), RunUntil(Bomber, [Bomber] { return Bomber->GetState() == EEnemyState::Attacking; }, 20.f));
	TestTrue(TEXT("Hovers above the rig"), FVector::Dist2D(Bomber->GetActorLocation(), W.RigActor->GetActorLocation()) < 25.f
		&& Bomber->GetActorLocation().Z > W.RigActor->GetActorLocation().Z + 500.f);
	Run(Bomber, 1.f);
	TestTrue(TEXT("Pod swells during the warning (shape cue)"), Bomber->Pod->GetRelativeScale3D().X > 0.75);
	TestTrue(TEXT("Tone descends (sound cue)"), Bomber->GetTelegraphTonePitch() < 1.2f);
	Run(Bomber, 1.1f);
	TestEqual(TEXT("Release hits the rig for 40"), W.Rig->GetCurrent(), 260.f);
	TestFalse(TEXT("Pod released"), Bomber->HasPod());

	// Re-arms, then the player shoots the pod: it detonates in the cradle and takes the carrier with it.
	TestTrue(TEXT("Re-arms"), RunUntil(Bomber, [Bomber] { return Bomber->HasPod(); }, 7.f));
	TestTrue(TEXT("Warns again"), RunUntil(Bomber, [Bomber] { return Bomber->GetState() == EEnemyState::Attacking; }, 3.f));
	W.Shoot(Bomber, Bomber->Pod, WeakShot);
	TestEqual(TEXT("One weak-point shot on the pod destroys the bomber"), Bomber->GetState(), EEnemyState::Dying);
	Run(Bomber, 3.f);
	TestEqual(TEXT("No release after the pod is shot"), W.Rig->GetCurrent(), 260.f);

	// Aimed at the objective instead.
	ACradleBomber* Second = W.Spawn<ACradleBomber>(FVector(1500.f, 1500.f, 400.f));
	Second->SetTarget(W.Objective);
	TestTrue(TEXT("Aimed at the objective: tagged"), Second->ActorHasTag(ETMObjectiveTags::InfrastructureAttacker));
	TestTrue(TEXT("Warns over the objective"), RunUntil(Second, [Second] { return Second->GetState() == EEnemyState::Attacking; }, 20.f));
	Run(Second, 2.1f);
	TestEqual(TEXT("Objective takes 40"), W.Objective->Integrity->GetCurrent(), 460.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMSilhouetteTest, "ETM.Enemies.Archetypes.SilhouettesDistinct", ETMSupportTestFlags)
bool FETMSilhouetteTest::RunTest(const FString&)
{
	// "Each distinguishable by silhouette at combat distance": every pair of enemies must differ by at least
	// 30% in height, width or depth, so none is a resized copy of another at 5-45 m.
	FTestWorld W;
	struct FEntry { const TCHAR* Name; AActor* Actor; };
	const FEntry Entries[] = {
		{ TEXT("Shardling"), W.Spawn<AShardling>(FVector(0.f, 0.f, 0.f)) },
		{ TEXT("Needle Drone"), W.Spawn<ANeedleDrone>(FVector(0.f, 1000.f, 0.f)) },
		{ TEXT("Bulwark"), W.Spawn<ABulwark>(FVector(0.f, 2000.f, 0.f)) },
		{ TEXT("Lance Walker"), W.Spawn<ALanceWalker>(FVector(0.f, 3000.f, 0.f)) },
		{ TEXT("Siphon"), W.Spawn<ASiphon>(FVector(0.f, 4000.f, 0.f)) },
		{ TEXT("Cradle Bomber"), W.Spawn<ACradleBomber>(FVector(0.f, 5000.f, 0.f)) },
	};
	TArray<FVector> Sizes;
	for (const FEntry& Entry : Entries)
	{
		FVector Origin, Extent;
		Entry.Actor->GetActorBounds(/*bOnlyColliding*/ false, Origin, Extent, /*bIncludeFromChildActors*/ false);
		const FVector Size(FMath::Max(Extent.X, Extent.Y) * 2.f, FMath::Min(Extent.X, Extent.Y) * 2.f, Extent.Z * 2.f);
		AddInfo(FString::Printf(TEXT("%s silhouette: %.0f x %.0f x %.0f cm"), Entry.Name, Size.X, Size.Y, Size.Z));
		Sizes.Add(Size);
	}
	for (int32 A = 0; A < Sizes.Num(); ++A)
	{
		for (int32 B = A + 1; B < Sizes.Num(); ++B)
		{
			const auto Differs = [](float X, float Y) { return FMath::Abs(X - Y) / FMath::Max(1.f, FMath::Max(X, Y)) >= 0.3f; };
			const bool bDistinct = Differs(Sizes[A].X, Sizes[B].X) || Differs(Sizes[A].Y, Sizes[B].Y) || Differs(Sizes[A].Z, Sizes[B].Z);
			TestTrue(FString::Printf(TEXT("%s and %s have different silhouettes"), Entries[A].Name, Entries[B].Name), bDistinct);
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMSupportPoolTest, "ETM.Enemies.Archetypes.SupportPooledReuse", ETMSupportTestFlags)
bool FETMSupportPoolTest::RunTest(const FString&)
{
	FTestWorld W;
	UEnemyPoolSubsystem* Pool = W.World->GetSubsystem<UEnemyPoolSubsystem>();
	for (UClass* Class : { ABulwark::StaticClass(), ALanceWalker::StaticClass(), ASiphon::StaticClass(), ACradleBomber::StaticClass() })
	{
		Pool->Prewarm(Class, 1);
	}
	const int32 Created = Pool->GetTotalCreated();

	ABulwark* Bulwark = Cast<ABulwark>(Pool->Acquire(ABulwark::StaticClass(), FTransform(FVector(1500.f, 0.f, 0.f))));
	ALanceWalker* Lance = Cast<ALanceWalker>(Pool->Acquire(ALanceWalker::StaticClass(), FTransform(FVector(3000.f, 0.f, 0.f))));
	ACradleBomber* Bomber = Cast<ACradleBomber>(Pool->Acquire(ACradleBomber::StaticClass(), FTransform(FVector(2000.f, 0.f, 400.f))));
	if (!Bulwark || !Lance || !Bomber) { AddError(TEXT("Pool returned the wrong class")); return false; }

	// Wear each one down to a changed state, kill it, let it return to the pool.
	Run(Bulwark, 1.f);
	W.Shoot(Bulwark, Bulwark->RightNode, WeakShot);
	Lance->SetTarget(W.Objective);
	Bomber->SetTarget(W.RigActor);
	RunUntil(Bomber, [Bomber] { return Bomber->GetState() == EEnemyState::Attacking; }, 20.f);
	Run(Bomber, 2.1f);
	for (AEnemyBase* Enemy : TArray<AEnemyBase*>{ Bulwark, Lance, Bomber }) Enemy->Kill();
	Run(Bulwark, 2.f); Run(Lance, 2.f); Run(Bomber, 2.f);

	// Reacquire: each comes back in its starting state.
	TestTrue(TEXT("Same Bulwark reused"), Pool->Acquire(ABulwark::StaticClass(), FTransform(FVector(1500.f, 0.f, 0.f))) == Bulwark);
	TestTrue(TEXT("Reused Bulwark has its shield up"), Bulwark->IsShieldUp() && Bulwark->Shield->IsVisible());
	TestFalse(TEXT("Reused Bulwark nodes are whole"), Bulwark->IsPartBroken(ABulwark::RightNodePart));
	TestTrue(TEXT("Same Lance reused"), Pool->Acquire(ALanceWalker::StaticClass(), FTransform(FVector(3000.f, 0.f, 0.f))) == Lance);
	TestFalse(TEXT("Reused Lance has no leftover infrastructure tag"), Lance->ActorHasTag(ETMObjectiveTags::InfrastructureAttacker));
	TestFalse(TEXT("Reused Lance chamber is closed"), Lance->IsChamberExposed());
	TestTrue(TEXT("Same bomber reused"), Pool->Acquire(ACradleBomber::StaticClass(), FTransform(FVector(2000.f, 0.f, 400.f))) == Bomber);
	TestTrue(TEXT("Reused bomber carries a pod again"), Bomber->HasPod());
	TestEqual(TEXT("Reuse created nothing"), Pool->GetTotalCreated(), Created);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
