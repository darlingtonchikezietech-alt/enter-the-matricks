// Automation tests for AEnemyBase and UEnemyPoolSubsystem.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Enemies; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Enemies/Base/EnemyBase.h"
#include "Enemies/Base/EnemyPoolSubsystem.h"
#include "Enemies/Base/Tests/EnemyTestListener.h"
#include "Combat/IntegrityComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const EAutomationTestFlags ETMEnemyTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;
	const FName NodePart(TEXT("Node"));
	const FName ShieldPart(TEXT("Shield"));

	/** A throwaway game world. */
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ETMEnemyTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			// With no game mode the world never flags itself as begun, so spawned actors would skip BeginPlay.
			World->SetBegunPlay(true);
		}

		~FTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->SetBegunPlay(false);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		/** A mission objective stand-in: an actor whose engine damage feeds a 500-point integrity pool. */
		UIntegrityComponent* SpawnObjective() const
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Actor);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			UIntegrityComponent* Integrity = NewObject<UIntegrityComponent>(Actor);
			Integrity->MaxIntegrity = 500.f;
			Integrity->bIsRig = false;
			Integrity->RegisterComponent();
			return Integrity;
		}

		AEnemyBase* SpawnEnemy(float MaxHealth = 80.f) const
		{
			AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>();
			Enemy->MaxHealth = MaxHealth;
			return Enemy;
		}

		UBoxComponent* AddHitBox(AEnemyBase* Enemy, FName Tag = NAME_None) const
		{
			UBoxComponent* Box = NewObject<UBoxComponent>(Enemy);
			Box->SetupAttachment(Enemy->GetRootComponent());
			Box->SetCollisionProfileName(TEXT("BlockAll"));
			if (!Tag.IsNone()) Box->ComponentTags.Add(Tag);
			Box->RegisterComponent();
			return Box;
		}
	};

	void Listen(AEnemyBase* Enemy, UETMEnemyTestListener* Listener)
	{
		Enemy->OnStateChanged.AddDynamic(Listener, &UETMEnemyTestListener::HandleStateChanged);
		Enemy->OnDamaged.AddDynamic(Listener, &UETMEnemyTestListener::HandleDamaged);
		Enemy->OnPartBroken.AddDynamic(Listener, &UETMEnemyTestListener::HandlePartBroken);
		Enemy->OnAttackTelegraph.AddDynamic(Listener, &UETMEnemyTestListener::HandleTelegraph);
		Enemy->OnAttackInterrupted.AddDynamic(Listener, &UETMEnemyTestListener::HandleInterrupted);
		Enemy->OnAttackResolved.AddDynamic(Listener, &UETMEnemyTestListener::HandleResolved);
		Enemy->OnKilled.AddDynamic(Listener, &UETMEnemyTestListener::HandleKilled);
	}

	void Advance(AEnemyBase* Enemy, float Seconds)
	{
		for (float T = 0.f; T < Seconds - KINDA_SMALL_NUMBER; T += Frame72)
		{
			Enemy->AdvanceState(FMath::Min(Frame72, Seconds - T));
		}
	}

	/** Damage the way the cannon delivers it: point damage with the hit component set. */
	float Shoot(AEnemyBase* Enemy, UPrimitiveComponent* Component, float Damage)
	{
		FHitResult Hit;
		Hit.Component = Component;
		return UGameplayStatics::ApplyPointDamage(Enemy, Damage, FVector::ForwardVector, Hit, nullptr, nullptr, nullptr);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMEnemyTransitionTableTest, "ETM.Enemies.Base.TransitionTable", ETMEnemyTestFlags)
bool FETMEnemyTransitionTableTest::RunTest(const FString&)
{
	using S = EEnemyState;
	TestTrue(TEXT("Pool to spawn"), ETMEnemy::CanTransition(S::Inactive, S::Spawning));
	TestFalse(TEXT("Cannot skip the spawn"), ETMEnemy::CanTransition(S::Inactive, S::Attacking));
	TestFalse(TEXT("Cannot attack while spawning"), ETMEnemy::CanTransition(S::Spawning, S::Attacking));
	TestTrue(TEXT("Approach to attack"), ETMEnemy::CanTransition(S::Approaching, S::Attacking));
	TestTrue(TEXT("Attack can be staggered"), ETMEnemy::CanTransition(S::Attacking, S::Staggered));
	TestFalse(TEXT("Stagger cannot attack directly"), ETMEnemy::CanTransition(S::Staggered, S::Attacking));
	TestTrue(TEXT("Dying only goes to the pool"), ETMEnemy::CanTransition(S::Dying, S::Inactive));
	for (const S To : { S::Spawning, S::Approaching, S::Attacking, S::Staggered })
	{
		TestFalse(TEXT("Nothing comes back from dying"), ETMEnemy::CanTransition(S::Dying, To));
	}
	for (const S From : { S::Spawning, S::Approaching, S::Attacking, S::Staggered })
	{
		TestTrue(TEXT("Every live state can die"), ETMEnemy::CanTransition(From, S::Dying));
	}

	TestTrue(TEXT("Approaching may deal damage"), ETMEnemy::CanDealDamage(S::Approaching));
	TestTrue(TEXT("Attacking may deal damage"), ETMEnemy::CanDealDamage(S::Attacking));
	for (const S State : { S::Inactive, S::Spawning, S::Staggered, S::Dying })
	{
		TestFalse(TEXT("No damage from this state"), ETMEnemy::CanDealDamage(State));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMEnemyLifecycleTest, "ETM.Enemies.Base.Lifecycle", ETMEnemyTestFlags)
bool FETMEnemyLifecycleTest::RunTest(const FString&)
{
	FTestWorld W;
	UIntegrityComponent* Objective = W.SpawnObjective();
	TestEqual(TEXT("Objective starts at 500"), Objective->GetCurrent(), 500.f);

	AEnemyBase* Enemy = W.SpawnEnemy();
	UETMEnemyTestListener* Listener = NewObject<UETMEnemyTestListener>();
	Listen(Enemy, Listener);

	TestEqual(TEXT("Starts parked"), Enemy->GetState(), EEnemyState::Inactive);
	TestTrue(TEXT("Parked enemy is hidden"), Enemy->IsHidden());
	TestFalse(TEXT("Parked enemy cannot attack"), Enemy->BeginAttack(Objective->GetOwner(), 20.f, 1.f));

	Enemy->Activate(FTransform(FVector(1000.f, 0.f, 0.f)));
	TestEqual(TEXT("Activate enters spawn"), Enemy->GetState(), EEnemyState::Spawning);
	TestFalse(TEXT("Active enemy is visible"), Enemy->IsHidden());
	TestTrue(TEXT("Placed at the spawn transform"), Enemy->GetActorLocation().Equals(FVector(1000.f, 0.f, 0.f), 0.1f));
	TestFalse(TEXT("Cannot attack while spawning"), Enemy->BeginAttack(Objective->GetOwner(), 20.f, 1.f));

	Advance(Enemy, Enemy->SpawnDuration + 0.05f);
	TestEqual(TEXT("Spawn ends in approach"), Enemy->GetState(), EEnemyState::Approaching);

	// Shardling reaching the objective: 1 s warning, then 20 damage.
	TestTrue(TEXT("Attack starts"), Enemy->BeginAttack(Objective->GetOwner(), 20.f, 1.f));
	TestEqual(TEXT("Attacking"), Enemy->GetState(), EEnemyState::Attacking);
	TestEqual(TEXT("Telegraph event"), Listener->Telegraphs, 1);
	TestEqual(TEXT("Telegraph length"), Listener->LastTelegraph, 1.f);
	TestFalse(TEXT("Only one attack at a time"), Enemy->BeginAttack(Objective->GetOwner(), 20.f, 1.f));

	Advance(Enemy, 0.9f);
	TestEqual(TEXT("No damage during the warning"), Objective->GetCurrent(), 500.f);
	Advance(Enemy, 0.2f);
	TestEqual(TEXT("Attack lands after the warning"), Objective->GetCurrent(), 480.f);
	TestEqual(TEXT("Resolved event reports the damage"), Listener->LastResolvedDamage, 20.f);
	TestEqual(TEXT("Back to approach"), Enemy->GetState(), EEnemyState::Approaching);
	TestEqual(TEXT("A resolved attack is not an interruption"), Listener->Interrupted, 0);

	TestTrue(TEXT("Stagger"), Enemy->Interrupt());
	TestEqual(TEXT("Staggered"), Enemy->GetState(), EEnemyState::Staggered);
	TestEqual(TEXT("Staggered enemy deals nothing"), Enemy->DealDamageTo(Objective->GetOwner(), 20.f), 0.f);
	Advance(Enemy, Enemy->StaggerDuration + 0.05f);
	TestEqual(TEXT("Recovers to approach"), Enemy->GetState(), EEnemyState::Approaching);

	Enemy->Kill();
	TestEqual(TEXT("Dying"), Enemy->GetState(), EEnemyState::Dying);
	TestEqual(TEXT("Kill event"), Listener->Killed, 1);
	TestFalse(TEXT("Dying enemy is no longer a threat"), Enemy->IsThreat());
	TestFalse(TEXT("Dying enemy has no collision"), Enemy->GetActorEnableCollision());

	const TArray<EEnemyState> Expected = {
		EEnemyState::Spawning, EEnemyState::Approaching, EEnemyState::Attacking, EEnemyState::Approaching,
		EEnemyState::Staggered, EEnemyState::Approaching, EEnemyState::Dying };
	TestTrue(TEXT("State events arrive in order"), Listener->States == Expected);
	TestEqual(TEXT("Objective took exactly one hit"), Objective->GetCurrent(), 480.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMEnemyDyingStopsDamageTest, "ETM.Enemies.Base.DyingStopsDamage", ETMEnemyTestFlags)
bool FETMEnemyDyingStopsDamageTest::RunTest(const FString&)
{
	FTestWorld W;
	UIntegrityComponent* Objective = W.SpawnObjective();
	AActor* Target = Objective->GetOwner();

	// Killed half way through the warning: the attack never lands.
	{
		AEnemyBase* Enemy = W.SpawnEnemy();
		UBoxComponent* Body = W.AddHitBox(Enemy);
		UETMEnemyTestListener* Listener = NewObject<UETMEnemyTestListener>();
		Listen(Enemy, Listener);
		Enemy->MarkPooled(); // keep the actor alive after death so it can be inspected
		Enemy->Activate(FTransform::Identity);
		Advance(Enemy, 1.f);
		Enemy->BeginAttack(Target, 35.f, 2.f);
		Advance(Enemy, 1.f);

		TestEqual(TEXT("Lethal shot is taken"), Shoot(Enemy, Body, 80.f), 80.f);
		TestEqual(TEXT("Enemy is dying"), Enemy->GetState(), EEnemyState::Dying);
		TestEqual(TEXT("Attack reported as interrupted"), Listener->Interrupted, 1);
		TestEqual(TEXT("Prevented damage is the attack's damage"), Listener->PreventedTotal, 35.f);
		TestEqual(TEXT("Direct damage call is refused while dying"), Enemy->DealDamageTo(Target, 35.f), 0.f);
		TestFalse(TEXT("Cannot start an attack while dying"), Enemy->BeginAttack(Target, 35.f, 0.f));
		TestEqual(TEXT("Further shots do nothing"), Shoot(Enemy, Body, 80.f), 0.f);

		Advance(Enemy, 5.f);
		TestEqual(TEXT("Objective never took the hit"), Objective->GetCurrent(), 500.f);
		TestEqual(TEXT("Attack never resolved"), Listener->Resolved, 0);
		TestEqual(TEXT("Enemy is parked after the death sequence"), Enemy->GetState(), EEnemyState::Inactive);
		TestEqual(TEXT("Killed once"), Listener->Killed, 1);
	}

	// Killed on the very frame the attack would have landed: death wins.
	{
		AEnemyBase* Enemy = W.SpawnEnemy();
		Enemy->MarkPooled();
		Enemy->Activate(FTransform::Identity);
		Advance(Enemy, 1.f);
		Enemy->BeginAttack(Target, 40.f, 0.5f);
		Advance(Enemy, 0.5f - Frame72);
		Enemy->Kill();
		Advance(Enemy, 1.f);
		TestEqual(TEXT("No damage from an enemy killed just before impact"), Objective->GetCurrent(), 500.f);
	}

	// Staggered during the warning: the attack is cancelled and the enemy recovers without re-firing it.
	{
		AEnemyBase* Enemy = W.SpawnEnemy();
		UETMEnemyTestListener* Listener = NewObject<UETMEnemyTestListener>();
		Listen(Enemy, Listener);
		Enemy->MarkPooled();
		Enemy->Activate(FTransform::Identity);
		Advance(Enemy, 1.f);
		Enemy->BeginAttack(Target, 35.f, 2.f);
		Advance(Enemy, 1.f);
		Enemy->Interrupt();
		Advance(Enemy, 5.f);
		TestEqual(TEXT("Stagger cancels the attack"), Objective->GetCurrent(), 500.f);
		TestEqual(TEXT("Stagger reports the prevented damage"), Listener->PreventedTotal, 35.f);
		TestEqual(TEXT("Enemy recovered"), Enemy->GetState(), EEnemyState::Approaching);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMEnemyPartsTest, "ETM.Enemies.Base.PartsAndDamage", ETMEnemyTestFlags)
bool FETMEnemyPartsTest::RunTest(const FString&)
{
	FTestWorld W;
	UIntegrityComponent* Objective = W.SpawnObjective();

	// A Bulwark-like unit: 480 HP body, an 80 HP node, and a shield that passes 10% to the body.
	AEnemyBase* Enemy = W.SpawnEnemy(480.f);
	Enemy->MarkPooled();
	FEnemyPart Node;
	Node.PartName = NodePart;
	Node.MaxHealth = 80.f;
	FEnemyPart Shield;
	Shield.PartName = ShieldPart;
	Shield.MaxHealth = 100000.f;
	Shield.DamageScale = 0.1f;
	Shield.bPassDamageToBody = true;
	Shield.bInterruptOnBreak = false;
	Enemy->Parts = { Node, Shield };

	UBoxComponent* Body = W.AddHitBox(Enemy);
	UBoxComponent* NodeBox = W.AddHitBox(Enemy, NodePart);
	UBoxComponent* ShieldBox = W.AddHitBox(Enemy, ShieldPart);
	UETMEnemyTestListener* Listener = NewObject<UETMEnemyTestListener>();
	Listen(Enemy, Listener);

	Enemy->Activate(FTransform::Identity);
	Advance(Enemy, 1.f);

	Shoot(Enemy, Body, 40.f);
	TestEqual(TEXT("Body hit reduces health"), Enemy->GetHealth(), 440.f);
	TestTrue(TEXT("Body hit has no part name"), Listener->LastPart.IsNone());

	Shoot(Enemy, ShieldBox, 40.f);
	TestEqual(TEXT("Shield passes 10% to the body"), Enemy->GetHealth(), 436.f);
	TestEqual(TEXT("Shield hit is attributed to the shield"), Listener->LastPart, ShieldPart);

	// One weak-point shot (40 x2 from the cannon) breaks the 80 HP node and interrupts an attack.
	Enemy->BeginAttack(Objective->GetOwner(), 30.f, 4.f);
	Shoot(Enemy, NodeBox, 80.f);
	TestTrue(TEXT("Node is broken"), Enemy->IsPartBroken(NodePart));
	TestEqual(TEXT("Part-broken event"), Listener->PartsBroken, 1);
	TestEqual(TEXT("Part-broken event names the node"), Listener->LastBrokenPart, NodePart);
	TestEqual(TEXT("Node damage does not touch the body"), Enemy->GetHealth(), 436.f);
	TestEqual(TEXT("Breaking the node staggers"), Enemy->GetState(), EEnemyState::Staggered);
	TestEqual(TEXT("The slam was prevented"), Listener->PreventedTotal, 30.f);
	TestEqual(TEXT("Broken node stops taking traces"), NodeBox->GetCollisionEnabled(), ECollisionEnabled::NoCollision);

	// A shot that still reports the broken node's component now counts as a body hit.
	Shoot(Enemy, NodeBox, 40.f);
	TestEqual(TEXT("Broken part no longer absorbs damage"), Enemy->GetHealth(), 396.f);

	// Overkill is clamped and kills once.
	Shoot(Enemy, Body, 5000.f);
	TestEqual(TEXT("Health floors at zero"), Enemy->GetHealth(), 0.f);
	TestEqual(TEXT("Killed once"), Listener->Killed, 1);
	Advance(Enemy, 5.f);
	TestEqual(TEXT("Objective was never hit"), Objective->GetCurrent(), 500.f);

	// Reuse: full health, intact parts, collision restored.
	Enemy->Activate(FTransform::Identity);
	TestEqual(TEXT("Reused enemy has full health"), Enemy->GetHealth(), 480.f);
	TestFalse(TEXT("Reused enemy has an intact node"), Enemy->IsPartBroken(NodePart));
	TestEqual(TEXT("Reused node has full health"), Enemy->GetPartHealth(NodePart), 80.f);
	TestEqual(TEXT("Reused node takes traces again"), NodeBox->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);

	// Concentrated fire staggers an armored unit.
	Enemy->StaggerDamageThreshold = 100.f;
	Advance(Enemy, 1.f);
	Shoot(Enemy, Body, 40.f);
	Shoot(Enemy, Body, 40.f);
	TestEqual(TEXT("Below the threshold: no stagger"), Enemy->GetState(), EEnemyState::Approaching);
	Advance(Enemy, Enemy->StaggerWindow + 0.1f);
	Shoot(Enemy, Body, 40.f);
	TestEqual(TEXT("Slow fire does not add up"), Enemy->GetState(), EEnemyState::Approaching);
	Shoot(Enemy, Body, 40.f);
	Shoot(Enemy, Body, 40.f);
	TestEqual(TEXT("Three quick hits stagger"), Enemy->GetState(), EEnemyState::Staggered);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMEnemyPoolTest, "ETM.Enemies.Base.PoolSteadyState", ETMEnemyTestFlags)
bool FETMEnemyPoolTest::RunTest(const FString&)
{
	FTestWorld W;
	UEnemyPoolSubsystem* Pool = W.World->GetSubsystem<UEnemyPoolSubsystem>();
	TestNotNull(TEXT("Pool subsystem exists"), Pool);
	if (!Pool) return false;

	const int32 Cap = 8; // normal active-enemy cap (bible section 14)
	Pool->Prewarm(AEnemyBase::StaticClass(), Cap);
	TestEqual(TEXT("Prewarm creates the pool"), Pool->GetTotalCreated(), Cap);
	TestEqual(TEXT("Everything starts parked"), Pool->GetPooledCount(AEnemyBase::StaticClass()), Cap);
	TestEqual(TEXT("No threats yet"), Pool->GetThreatCount(), 0);
	Pool->Prewarm(AEnemyBase::StaticClass(), Cap);
	TestEqual(TEXT("Prewarming again creates nothing"), Pool->GetTotalCreated(), Cap);

	// Twenty full waves: spawn the cap, kill them all, let them finish dying.
	TArray<AEnemyBase*> Wave;
	Wave.Reserve(Cap);
	for (int32 Round = 0; Round < 20; ++Round)
	{
		Wave.Reset();
		for (int32 Index = 0; Index < Cap; ++Index)
		{
			AEnemyBase* Enemy = Pool->Acquire(AEnemyBase::StaticClass(), FTransform(FVector(100.f * Index, 0.f, 0.f)));
			if (!Enemy) { AddError(TEXT("Acquire returned null inside the prewarmed size")); return false; }
			Wave.Add(Enemy);
		}
		if (Round == 0)
		{
			TestEqual(TEXT("All eight are threats"), Pool->GetThreatCount(), Cap);
			TestEqual(TEXT("Pool is empty at the cap"), Pool->GetPooledCount(AEnemyBase::StaticClass()), 0);
			TestTrue(TEXT("Acquired enemy is pooled"), Wave[0]->IsPooled());
			TestEqual(TEXT("Acquired enemy has full health"), Wave[0]->GetHealth(), Wave[0]->MaxHealth);
		}
		for (AEnemyBase* Enemy : Wave) Enemy->Kill();
		if (Round == 0)
		{
			TestEqual(TEXT("Dying enemies are not threats"), Pool->GetThreatCount(), 0);
			TestEqual(TEXT("Dying enemies are still active"), Pool->GetActiveCount(), Cap);
		}
		for (AEnemyBase* Enemy : Wave) Advance(Enemy, Enemy->DeathDuration + 0.05f);
	}
	TestEqual(TEXT("160 spawns created no new actors"), Pool->GetTotalCreated(), Cap);
	TestEqual(TEXT("The pool never had to grow"), Pool->GetGrowthCount(), 0);
	TestEqual(TEXT("Everything is parked again"), Pool->GetPooledCount(AEnemyBase::StaticClass()), Cap);

	// Past the prewarmed size with growth off: refuse instead of allocating.
	Pool->bAllowGrowth = false;
	for (int32 Index = 0; Index < Cap; ++Index) Pool->Acquire(AEnemyBase::StaticClass(), FTransform::Identity);
	TestNull(TEXT("Ninth enemy is refused when growth is off"), Pool->Acquire(AEnemyBase::StaticClass(), FTransform::Identity));
	TestEqual(TEXT("Refusal created nothing"), Pool->GetTotalCreated(), Cap);

	// With growth on the pool grows and counts it, so an undersized prewarm is visible.
	Pool->bAllowGrowth = true;
	AddExpectedError(TEXT("Enemy pool grew"), EAutomationExpectedErrorFlags::Contains, 1);
	TestNotNull(TEXT("Ninth enemy is created when growth is on"), Pool->Acquire(AEnemyBase::StaticClass(), FTransform::Identity));
	TestEqual(TEXT("Growth is counted"), Pool->GetGrowthCount(), 1);

	// Checkpoint retry parks everything with no kill events.
	Pool->DeactivateAll();
	TestEqual(TEXT("DeactivateAll parks everything"), Pool->GetActiveCount(), 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
