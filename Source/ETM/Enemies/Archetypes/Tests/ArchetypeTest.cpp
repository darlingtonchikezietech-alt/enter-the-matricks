// Automation tests for the Shardling and Needle Drone prototypes.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Enemies.Archetypes; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Enemies/Archetypes/Shardling.h"
#include "Enemies/Archetypes/NeedleDrone.h"
#include "Enemies/Base/EnemyDeathBurst.h"
#include "Enemies/Base/EnemyPoolSubsystem.h"
#include "EngineUtils.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Combat/IntegrityComponent.h"
#include "Combat/DefenseComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const EAutomationTestFlags ETMArchetypeTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;

	struct FTestWorld
	{
		UWorld* World = nullptr;
		AMissionObjective* Objective = nullptr;
		AActor* RigActor = nullptr;
		UIntegrityComponent* Rig = nullptr;
		UDefenseComponent* Brace = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ETMArchetypeTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true); // no game mode, so mark it begun or spawned actors skip BeginPlay

			Objective = World->SpawnActor<AMissionObjective>(FVector::ZeroVector, FRotator::ZeroRotator);

			// Rig stand-in 300 cm in front of the objective, with the real health and brace components.
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
		T* SpawnEnemy(const FVector& Location) const
		{
			T* Enemy = World->SpawnActor<T>(Location, FRotator::ZeroRotator);
			Enemy->MarkPooled(); // keep the actor after death so it can be inspected
			Enemy->Activate(FTransform(Location));
			return Enemy;
		}
	};

	/** Cannon-style hit on an enemy's body mesh. */
	void Shoot(AEnemyBase* Enemy, UPrimitiveComponent* Body, float Damage, AActor* Shooter)
	{
		FHitResult Hit;
		Hit.Component = Body;
		UGameplayStatics::ApplyPointDamage(Enemy, Damage, FVector::ForwardVector, Hit, nullptr, Shooter, nullptr);
	}

	template <typename T>
	void Run(T* Enemy, float Seconds)
	{
		for (float Time = 0.f; Time < Seconds - KINDA_SMALL_NUMBER; Time += Frame72)
		{
			Enemy->SimulateStep(Frame72);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMShardlingAttackTest, "ETM.Enemies.Archetypes.Shardling.CutsObjectiveThenSelfDestructs", ETMArchetypeTestFlags)
bool FETMShardlingAttackTest::RunTest(const FString&)
{
	FTestWorld W;
	AShardling* Shardling = W.SpawnEnemy<AShardling>(FVector(2000.f, 400.f, 0.f));

	TestEqual(TEXT("Bible HP 80"), Shardling->GetHealth(), 80.f);
	TestEqual(TEXT("Bible objective damage 20"), Shardling->ObjectiveDamage, 20.f);
	TestEqual(TEXT("Bible limb raise 1 s"), Shardling->LimbRaiseSeconds, 1.f);
	TestTrue(TEXT("Tagged as an infrastructure attacker"), Shardling->ActorHasTag(ETMObjectiveTags::InfrastructureAttacker));
	TestTrue(TEXT("Body answers cannon traces"), Shardling->Body->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block);

	// No target set: it finds the mission's objective itself.
	Run(Shardling, 0.6f);
	TestEqual(TEXT("Approaching after the spawn"), Shardling->GetState(), EEnemyState::Approaching);
	TestTrue(TEXT("Picked up the active objective"), Shardling->GetTarget() == W.Objective);

	const float StartDistance = FVector::Dist2D(Shardling->GetActorLocation(), W.Objective->GetActorLocation());
	Run(Shardling, 1.f);
	const float AfterOneSecond = FVector::Dist2D(Shardling->GetActorLocation(), W.Objective->GetActorLocation());
	TestTrue(FString::Printf(TEXT("Moves at 250 cm/s (moved %.0f)"), StartDistance - AfterOneSecond), FMath::IsNearlyEqual(StartDistance - AfterOneSecond, 250.f, 5.f));
	TestEqual(TEXT("Stays on the ground"), Shardling->GetActorLocation().Z, 0.0);

	// Run until it reaches the objective and starts raising its limbs.
	for (int32 Frame = 0; Frame < 72 * 20 && Shardling->GetState() == EEnemyState::Approaching; ++Frame) Shardling->SimulateStep(Frame72);
	TestEqual(TEXT("Stops and raises its limbs"), Shardling->GetState(), EEnemyState::Attacking);
	TestTrue(TEXT("Stops at attack range"), FVector::Dist2D(Shardling->GetActorLocation(), W.Objective->GetActorLocation()) <= Shardling->AttackRange + 1.f);
	TestEqual(TEXT("No damage when the warning starts"), W.Objective->Integrity->GetCurrent(), 500.f);

	Run(Shardling, 0.5f);
	TestTrue(TEXT("Limbs are half raised at 0.5 s (shape cue)"), FMath::IsNearlyEqual(Shardling->GetTelegraphProgress(), 0.5f, 0.05f));
	TestTrue(TEXT("Limb pivot follows the progress"), FMath::IsNearlyEqual(Shardling->LeftLimbPivot->GetRelativeRotation().Pitch, 35.0, 4.0));
	TestEqual(TEXT("Still no damage during the warning"), W.Objective->Integrity->GetCurrent(), 500.f);

	Run(Shardling, 0.6f);
	TestEqual(TEXT("Objective takes 20"), W.Objective->Integrity->GetCurrent(), 480.f);
	TestEqual(TEXT("Shardling destroys itself after cutting"), Shardling->GetState(), EEnemyState::Dying);
	TestEqual(TEXT("The rig is never touched"), W.Rig->GetCurrent(), 300.f);

	Run(Shardling, 1.f);
	TestEqual(TEXT("Wreck returns to the pool"), Shardling->GetState(), EEnemyState::Inactive);
	TestEqual(TEXT("Exactly one cut"), W.Objective->Integrity->GetCurrent(), 480.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMShardlingKillTest, "ETM.Enemies.Archetypes.Shardling.KilledDuringWarning", ETMArchetypeTestFlags)
bool FETMShardlingKillTest::RunTest(const FString&)
{
	FTestWorld W;
	AShardling* Shardling = W.SpawnEnemy<AShardling>(FVector(400.f, 0.f, 0.f));
	Shardling->SetTarget(W.Objective);
	for (int32 Frame = 0; Frame < 72 * 10 && Shardling->GetState() != EEnemyState::Attacking; ++Frame) Shardling->SimulateStep(Frame72);
	TestEqual(TEXT("Reached the warning"), Shardling->GetState(), EEnemyState::Attacking);
	Run(Shardling, 0.6f);

	// Two body hits of 40 kill it (80 HP); a weak-point bonus is not needed.
	Shoot(Shardling, Shardling->Body, 40.f, W.RigActor);
	TestEqual(TEXT("One hit leaves it alive"), Shardling->GetHealth(), 40.f);
	Shoot(Shardling, Shardling->Body, 40.f, W.RigActor);
	TestEqual(TEXT("Second hit kills"), Shardling->GetState(), EEnemyState::Dying);

	Run(Shardling, 2.f);
	TestEqual(TEXT("A Shardling killed mid-warning never cuts"), W.Objective->Integrity->GetCurrent(), 500.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMNeedleDroneBurstTest, "ETM.Enemies.Archetypes.NeedleDrone.TelegraphAndBurst", ETMArchetypeTestFlags)
bool FETMNeedleDroneBurstTest::RunTest(const FString&)
{
	FTestWorld W;
	ANeedleDrone* Drone = W.SpawnEnemy<ANeedleDrone>(FVector(1500.f, -600.f, 100.f));
	Drone->SetTarget(W.RigActor);

	TestEqual(TEXT("Bible HP 120"), Drone->GetHealth(), 120.f);
	TestEqual(TEXT("Bible burst 15"), Drone->BurstDamage, 15.f);
	TestEqual(TEXT("Bible telegraph 1.5 s"), Drone->TelegraphSeconds, 1.5f);
	TestFalse(TEXT("Not an infrastructure attacker"), Drone->ActorHasTag(ETMObjectiveTags::InfrastructureAttacker));
	TestFalse(TEXT("No aiming line before the warning"), Drone->IsAimLineVisible());

	// Rises to its hover point, then starts the warning.
	for (int32 Frame = 0; Frame < 72 * 10 && Drone->GetState() != EEnemyState::Attacking; ++Frame) Drone->SimulateStep(Frame72);
	TestEqual(TEXT("Warning started"), Drone->GetState(), EEnemyState::Attacking);
	TestTrue(TEXT("Holds at its hover point"), Drone->GetActorLocation().Equals(Drone->GetHoverLocation(), Drone->ArriveTolerance + 1.f));
	TestTrue(TEXT("Hover point is 4 m above the spawn"), FMath::IsNearlyEqual(Drone->GetHoverLocation().Z, 500.0, 0.1));
	TestTrue(TEXT("Aiming line is shown (shape cue)"), Drone->IsAimLineVisible());

	// The aiming line runs from the drone to the rig.
	const FVector LineCentre = Drone->AimLine->GetComponentLocation();
	const FVector Expected = (Drone->GetActorLocation() + W.RigActor->GetActorLocation()) * 0.5f;
	TestTrue(TEXT("Aiming line spans drone to rig"), LineCentre.Equals(Expected, 1.f));

	Run(Drone, 0.75f);
	TestTrue(TEXT("Tone pitch is rising (sound cue)"), FMath::IsNearlyEqual(Drone->GetTelegraphTonePitch(), 1.5f, 0.05f));
	TestEqual(TEXT("No damage during the warning"), W.Rig->GetCurrent(), 300.f);

	Run(Drone, 0.8f);
	TestEqual(TEXT("Burst hits the rig for 15"), W.Rig->GetCurrent(), 285.f);
	TestEqual(TEXT("Back to holding position"), Drone->GetState(), EEnemyState::Approaching);
	TestFalse(TEXT("Aiming line hidden after the burst"), Drone->IsAimLineVisible());
	TestEqual(TEXT("Drones never hit the objective"), W.Objective->Integrity->GetCurrent(), 500.f);

	// Cooldown, then a second warning; this time the player braces: 15 x 0.25 = 3.75.
	Run(Drone, 2.9f);
	TestEqual(TEXT("Waits out the cooldown"), Drone->GetState(), EEnemyState::Approaching);
	for (int32 Frame = 0; Frame < 72 * 2 && Drone->GetState() != EEnemyState::Attacking; ++Frame) Drone->SimulateStep(Frame72);
	TestEqual(TEXT("Second warning"), Drone->GetState(), EEnemyState::Attacking);
	W.Brace->TryActivateBrace();
	Run(Drone, 1.6f);
	TestTrue(TEXT("Brace cuts the burst to 3.75"), FMath::IsNearlyEqual(W.Rig->GetCurrent(), 281.25f, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMNeedleDroneKillTest, "ETM.Enemies.Archetypes.NeedleDrone.KillableInsideWarning", ETMArchetypeTestFlags)
bool FETMNeedleDroneKillTest::RunTest(const FString&)
{
	FTestWorld W;
	ANeedleDrone* Drone = W.SpawnEnemy<ANeedleDrone>(FVector(1200.f, 0.f, 100.f));
	Drone->SetTarget(W.RigActor);
	Drone->SetHoverLocation(FVector(1200.f, 0.f, 300.f));
	for (int32 Frame = 0; Frame < 72 * 10 && Drone->GetState() != EEnemyState::Attacking; ++Frame) Drone->SimulateStep(Frame72);
	TestTrue(TEXT("Assigned hover point is used"), Drone->GetActorLocation().Equals(FVector(1200.f, 0.f, 300.f), Drone->ArriveTolerance + 1.f));

	// One cannon fires 4 shots/s: three 40-damage hits take 0.5 s, inside the 1.5 s warning (bible: shoot it before the burst).
	Run(Drone, 0.25f);
	Shoot(Drone, Drone->Body, 40.f, W.RigActor);
	Run(Drone, 0.25f);
	Shoot(Drone, Drone->Body, 40.f, W.RigActor);
	Run(Drone, 0.25f);
	Shoot(Drone, Drone->Body, 40.f, W.RigActor);
	TestEqual(TEXT("Three hits kill it"), Drone->GetState(), EEnemyState::Dying);
	TestFalse(TEXT("Aiming line drops with the drone"), Drone->IsAimLineVisible());

	Run(Drone, 2.f);
	TestEqual(TEXT("A drone killed mid-warning never bursts"), W.Rig->GetCurrent(), 300.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMArchetypePoolTest, "ETM.Enemies.Archetypes.PooledReuse", ETMArchetypeTestFlags)
bool FETMArchetypePoolTest::RunTest(const FString&)
{
	FTestWorld W;
	UEnemyPoolSubsystem* Pool = W.World->GetSubsystem<UEnemyPoolSubsystem>();
	Pool->Prewarm(AShardling::StaticClass(), 6);
	Pool->Prewarm(ANeedleDrone::StaticClass(), 6); // Level 1 phase 2: at most 6 active (bible section 9)
	const int32 Created = Pool->GetTotalCreated();
	TestEqual(TEXT("Twelve actors prewarmed"), Created, 12);

	for (int32 Round = 0; Round < 5; ++Round)
	{
		ANeedleDrone* Drone = Cast<ANeedleDrone>(Pool->Acquire(ANeedleDrone::StaticClass(), FTransform(FVector(1000.f, 0.f, 0.f))));
		AShardling* Shardling = Cast<AShardling>(Pool->Acquire(AShardling::StaticClass(), FTransform(FVector(1000.f, 200.f, 0.f))));
		if (!Drone || !Shardling) { AddError(TEXT("Pool returned the wrong class or nothing")); return false; }
		Drone->SetTarget(W.RigActor);
		for (int32 Frame = 0; Frame < 72 * 10 && Drone->GetState() != EEnemyState::Attacking; ++Frame)
		{
			Drone->SimulateStep(Frame72);
			Shardling->SimulateStep(Frame72);
		}
		if (Round > 0)
		{
			TestTrue(TEXT("Reused drone hovers above its new spawn, not its old one"), FMath::IsNearlyEqual(Drone->GetHoverLocation().Z, 400.0, 0.1));
		}
		Drone->Kill();
		Shardling->Kill();
		Run(Drone, 1.f);
		Run(Shardling, 1.f);
		TestFalse(TEXT("Reused drone hides its aiming line"), Drone->IsAimLineVisible());
		TestEqual(TEXT("Reused Shardling has its limbs down"), Shardling->LeftLimbPivot->GetRelativeRotation().Pitch, 0.0);
	}
	TestEqual(TEXT("Reuse creates no new actors"), Pool->GetTotalCreated(), Created);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMNeedleDroneDriftTest, "ETM.Enemies.Archetypes.NeedleDrone.DriftsAroundHoverPoint", ETMArchetypeTestFlags)
bool FETMNeedleDroneDriftTest::RunTest(const FString&)
{
	// Owner headset test (T-0058): drones should keep moving around their spot, mostly sideways and up and
	// down as the rig sees them, so the player tracks and shoots them, instead of standing in one place.
	FTestWorld W;
	ANeedleDrone* Drone = W.SpawnEnemy<ANeedleDrone>(FVector(1500.f, 0.f, 100.f));
	Drone->SetTarget(W.RigActor);
	for (int32 Frame = 0; Frame < 72 * 10 && !Drone->HasArrived(); ++Frame) Drone->SimulateStep(Frame72);
	TestTrue(TEXT("Arrived at its hover point"), Drone->HasArrived());

	const FVector Hover = Drone->GetHoverLocation();
	FVector Min(FLT_MAX), Max(-FLT_MAX);
	FVector Last = Drone->GetActorLocation();
	float SlowestSecond = FLT_MAX, Moved = 0.f;
	for (int32 Frame = 1; Frame <= 72 * 12; ++Frame)
	{
		Drone->SimulateStep(Frame72);
		const FVector Offset = Drone->GetActorLocation() - Hover;
		Min = Min.ComponentMin(Offset);
		Max = Max.ComponentMax(Offset);
		Moved += FVector::Dist(Drone->GetActorLocation(), Last);
		Last = Drone->GetActorLocation();
		if (Frame > 72 * 3 && Frame % 72 == 0) { SlowestSecond = FMath::Min(SlowestSecond, Moved); }
		if (Frame % 72 == 0) Moved = 0.f;
	}
	// The rig is along -X from the drone, so sideways is Y and up is Z.
	const FVector Ext = Drone->DriftHalfExtent;
	TestTrue(FString::Printf(TEXT("Sweeps sideways (%.0f cm)"), Max.Y - Min.Y), Max.Y - Min.Y >= Ext.Y * 0.8f);
	TestTrue(FString::Printf(TEXT("Moves up and down (%.0f cm)"), Max.Z - Min.Z), Max.Z - Min.Z >= Ext.Z * 0.6f);
	TestTrue(TEXT("Stays inside its zone"), Max.Y <= Ext.Y + 5.f && -Min.Y <= Ext.Y + 5.f && Max.Z <= Ext.Z + 5.f && -Min.Z <= Ext.Z + 5.f
		&& Max.X <= Ext.X + 5.f && -Min.X <= Ext.X + 5.f);
	TestTrue(FString::Printf(TEXT("Never stands still (slowest second %.0f cm)"), SlowestSecond), SlowestSecond >= 15.f);
	TestTrue(TEXT("Still attacking while it drifts"), W.Rig->GetCurrent() < 300.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMDeathBurstTest, "ETM.Enemies.Archetypes.DeathBurst", ETMArchetypeTestFlags)
bool FETMDeathBurstTest::RunTest(const FString&)
{
	// Owner headset test (T-0058): a killed enemy goes in a small explosion.
	FTestWorld W;
	AShardling* Shardling = W.SpawnEnemy<AShardling>(FVector(800.f, 0.f, 0.f));
	Run(Shardling, 1.f);
	Shardling->Kill();
	TestTrue(TEXT("Enemy hidden once it explodes"), Shardling->IsHidden());
	AEnemyDeathBurst* Burst = nullptr;
	for (TActorIterator<AEnemyDeathBurst> It(W.World); It; ++It) Burst = *It;
	TestNotNull(TEXT("Explosion spawned"), Burst);
	if (!Burst) return false;
	TestTrue(TEXT("Where the enemy was"), FVector::Dist2D(Burst->GetActorLocation(), Shardling->GetActorLocation()) < 100.f);
	TestEqual(TEXT("Debris pieces"), Burst->GetDebrisCount(), 6);
	Burst->AdvanceBurst(0.05f);
	TestTrue(TEXT("Flash shows"), Burst->IsFlashVisible());
	Burst->AdvanceBurst(0.4f);
	TestFalse(TEXT("Flash gone after a moment"), Burst->IsFlashVisible());
	Burst->AdvanceBurst(0.3f);
	TestTrue(TEXT("Burst removes itself"), !IsValid(Burst) || Burst->IsActorBeingDestroyed());

	// Pooled reuse shows the enemy again.
	UEnemyPoolSubsystem* Pool = W.World->GetSubsystem<UEnemyPoolSubsystem>();
	Pool->Prewarm(AShardling::StaticClass(), 1);
	AShardling* Pooled = Cast<AShardling>(Pool->Acquire(AShardling::StaticClass(), FTransform(FVector(900.f, 0.f, 0.f))));
	if (!Pooled) return false;
	Pooled->Kill();
	Run(Pooled, 1.f);
	AShardling* Again = Cast<AShardling>(Pool->Acquire(AShardling::StaticClass(), FTransform(FVector(900.f, 0.f, 0.f))));
	TestTrue(TEXT("Reused enemy is visible again"), Again == Pooled && !Again->IsHidden());
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
