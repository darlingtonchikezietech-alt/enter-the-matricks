// Automation tests for AEncounterDirector.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Encounters; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Encounters/EncounterDirector.h"
#include "Enemies/Base/EnemyBase.h"
#include "Enemies/Base/EnemyPoolSubsystem.h"
#include "Enemies/Base/AttackTokenSubsystem.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Combat/IntegrityComponent.h"
#include "Combat/DefenseComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	const EAutomationTestFlags ETMEncounterTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;

	struct FTestWorld
	{
		UWorld* World = nullptr;
		AActor* RigActor = nullptr;
		UIntegrityComponent* Rig = nullptr;
		AEncounterDirector* Director = nullptr;
		UMissionStateSubsystem* Mission = nullptr;
		/** Every enemy the director has spawned, so death sequences can be stepped to the end. */
		TSet<AEnemyBase*> Seen;
		int32 MaxLiveSeen = 0;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ETMEncounterTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true); // no game mode, so mark it begun or spawned actors skip BeginPlay

			Mission = World->GetSubsystem<UMissionStateSubsystem>();
			World->SpawnActor<AMissionObjective>(FVector(200.f, 0.f, 0.f), FRotator::ZeroRotator);

			// Rig at the origin facing +X, with the real health and brace components.
			RigActor = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(RigActor);
			RigActor->SetRootComponent(Root);
			Root->RegisterComponent();
			UDefenseComponent* Brace = NewObject<UDefenseComponent>(RigActor);
			Brace->RegisterComponent();
			Rig = NewObject<UIntegrityComponent>(RigActor);
			Rig->MaxIntegrity = 300.f;
			Rig->RegisterComponent();
			Rig->SetDefenseComponent(Brace);
			Mission->RegisterRig(Rig, Brace);

			Director = World->SpawnActor<AEncounterDirector>();
			Director->RigActor = RigActor;
			Director->EntranceScatter = 50.f;
			Director->Entrances.Add(MakeEntrance(FVector(3000.f, -1500.f, 0.f)));  // 0: left lane
			Director->Entrances.Add(MakeEntrance(FVector(4000.f, 0.f, 0.f)));      // 1: centre
			Director->Entrances.Add(MakeEntrance(FVector(3000.f, 1500.f, 400.f))); // 2: right, raised
		}

		~FTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->SetBegunPlay(false);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		AActor* MakeEntrance(const FVector& Location) const
		{
			AActor* Entrance = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Entrance);
			Entrance->SetRootComponent(Root);
			Root->RegisterComponent();
			Entrance->SetActorLocation(Location);
			return Entrance;
		}

		/** Steps the director and every enemy it ever spawned. */
		void Run(float Seconds)
		{
			for (float T = 0.f; T < Seconds - KINDA_SMALL_NUMBER; T += Frame72)
			{
				Director->AdvanceDirector(Frame72);
				for (AEnemyBase* Enemy : Director->GetLiveEnemies()) Seen.Add(Enemy);
				MaxLiveSeen = FMath::Max(MaxLiveSeen, Director->GetLiveCount());
				for (AEnemyBase* Enemy : Seen) Enemy->AdvanceState(Frame72);
			}
		}

		void KillAll() const
		{
			for (AEnemyBase* Enemy : Director->GetLiveEnemies()) Enemy->Kill();
		}
	};

	FEncounterSpawnGroup Group(int32 Count, int32 Entrance, float Interval, float StartDelay = 0.f)
	{
		FEncounterSpawnGroup G;
		G.EnemyClass = AEnemyBase::StaticClass();
		G.Count = Count;
		G.EntranceIndex = Entrance;
		G.Interval = Interval;
		G.StartDelay = StartDelay;
		return G;
	}

	FEncounterPhase Phase(const TCHAR* Name, float SlotSeconds, int32 MaxActive, TArray<FEncounterSpawnGroup> Groups)
	{
		FEncounterPhase P;
		P.Name = Name;
		P.SlotSeconds = SlotSeconds;
		P.MaxActive = MaxActive;
		P.Groups = MoveTemp(Groups);
		return P;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMEncounterCapsTest, "ETM.Encounters.Director.CapsAndHoldBack", ETMEncounterTestFlags)
bool FETMEncounterCapsTest::RunTest(const FString&)
{
	// Phase cap 3 with 10 queued: never more than 3 at once, the rest trickle in as slots open.
	{
		FTestWorld W;
		W.Director->Phases = { Phase(TEXT("Trickle"), 30.f, 3, { Group(10, 0, 0.25f) }) };
		TestTrue(TEXT("Encounter starts"), W.Director->StartEncounter(1));
		W.Run(5.f);
		TestEqual(TEXT("Cap of 3 holds"), W.Director->GetLiveCount(), 3);
		TestEqual(TEXT("Only 3 spawned so far"), W.Director->GetSpawnedCount(0), 3);
		TestTrue(TEXT("Held-back spawns are counted"), W.Director->GetHeldBackCount() > 0);

		W.KillAll();
		W.Run(Frame72);
		TestEqual(TEXT("One spawn per tick, not a clump"), W.Director->GetLiveCount(), 1);
		for (int32 Round = 0; Round < 10 && W.Director->GetSpawnedCount(0) < 10; ++Round)
		{
			W.Run(0.5f);
			W.KillAll();
		}
		TestEqual(TEXT("All 10 eventually appear"), W.Director->GetSpawnedCount(0), 10);
		TestEqual(TEXT("Never more than 3 at once"), W.MaxLiveSeen, 3);
		TestEqual(TEXT("Prewarm covered live and dying enemies: the pool never grew"), W.World->GetSubsystem<UEnemyPoolSubsystem>()->GetGrowthCount(), 0);
	}

	// A phase asking for 12 at once still stops at the global cap of 8.
	{
		FTestWorld W;
		W.Director->Phases = { Phase(TEXT("Flood"), 30.f, 12, { Group(6, 0, 0.f), Group(6, 1, 0.f) }) };
		W.Director->StartEncounter(2);
		W.Run(3.f);
		TestEqual(TEXT("Global cap 8 (bible section 14)"), W.Director->GetLiveCount(), 8);
		TestEqual(TEXT("Never above 8"), W.MaxLiveSeen, 8);
	}

	// Boss phase: at most 4 adds, even if the phase allows more.
	{
		FTestWorld W;
		FEncounterPhase Boss = Phase(TEXT("Boss"), 30.f, 6, { Group(10, 1, 0.f) });
		Boss.bBossPhase = true;
		Boss.bCompleteWhenCleared = false;
		W.Director->Phases = { Boss };
		W.Director->StartEncounter(3);
		W.Run(3.f);
		TestEqual(TEXT("Boss phase allows 4 adds"), W.Director->GetLiveCount(), 4);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMEncounterTransitionTest, "ETM.Encounters.Director.EarlyTransitionRule", ETMEncounterTestFlags)
bool FETMEncounterTransitionTest::RunTest(const FString&)
{
	FTestWorld W;
	W.Director->Phases = {
		Phase(TEXT("Cutters"), 100.f, 0, { Group(2, 0, 0.f) }),
		Phase(TEXT("Drones"), 10.f, 0, { Group(1, 2, 0.f) }),
	};
	W.Director->StartEncounter(4);

	// Cleared at 1 s, but the slot is 100 s: no move before 60 s (P-13).
	W.Run(1.f);
	W.KillAll();
	W.Run(54.f);
	TestEqual(TEXT("Cleared early but held until 60% of the slot"), W.Director->GetPhaseIndex(), 0);
	TestTrue(TEXT("No reinforcements after the groups are spent"), W.Director->GetLiveCount() == 0 && W.Director->AreGroupsSpent());

	// At 60 s the dialogue is still playing: keep waiting.
	W.Director->SetDialoguePending(true);
	W.Run(10.f);
	TestEqual(TEXT("Required dialogue holds the phase"), W.Director->GetPhaseIndex(), 0);
	W.Director->SetDialoguePending(false);
	W.Run(Frame72);
	TestEqual(TEXT("Moves on once dialogue ends"), W.Director->GetPhaseIndex(), 1);

	// An unfinished phase runs long instead of being cut off.
	W.Run(60.f);
	TestEqual(TEXT("Unfinished phase extends past its slot"), W.Director->GetPhaseIndex(), 1);
	TestEqual(TEXT("Still running"), W.Director->GetState(), EEncounterState::Running);
	W.KillAll();
	W.Run(Frame72 * 2.f);
	TestEqual(TEXT("Last phase cleared: encounter complete"), W.Director->GetState(), EEncounterState::Complete);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMEncounterNeverDeletesTest, "ETM.Encounters.Director.NeverDeletesVisibleEnemies", ETMEncounterTestFlags)
bool FETMEncounterNeverDeletesTest::RunTest(const FString&)
{
	FTestWorld W;
	FEncounterPhase Boss = Phase(TEXT("Boss"), 0.f, 3, { Group(4, 1, 0.f) });
	Boss.bBossPhase = true;
	Boss.bCompleteWhenCleared = false; // ends when the boss dies
	W.Director->Phases = { Boss, Phase(TEXT("Resolution"), 30.f, 5, { Group(4, 0, 0.f) }) };
	W.Director->StartEncounter(5);

	W.Run(2.f);
	const TArray<AEnemyBase*> Adds = W.Director->GetLiveEnemies();
	TestEqual(TEXT("Three adds alongside the boss"), Adds.Num(), 3);

	W.Director->CompletePhase(); // boss dies with adds still on screen
	W.Run(Frame72);
	TestEqual(TEXT("Next phase started"), W.Director->GetPhaseIndex(), 1);
	for (const AEnemyBase* Add : Adds)
	{
		TestTrue(TEXT("Surviving adds are not removed"), Add->IsActive() && Add->IsThreat());
	}
	W.Run(2.f);
	TestEqual(TEXT("Carried-over adds count against the new cap (3 + 2 = 5)"), W.Director->GetLiveCount(), 5);
	TestEqual(TEXT("So only 2 new enemies appear"), W.Director->GetSpawnedCount(0), 2);
	TestEqual(TEXT("Carry-over did not force the pool to grow"), W.World->GetSubsystem<UEnemyPoolSubsystem>()->GetGrowthCount(), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMEncounterCheckpointTest, "ETM.Encounters.Director.CheckpointRepairRetry", ETMEncounterTestFlags)
bool FETMEncounterCheckpointTest::RunTest(const FString&)
{
	FTestWorld W;
	FEncounterPhase Midpoint = Phase(TEXT("Lances"), 60.f, 0, { Group(2, 1, 0.5f) });
	Midpoint.bCheckpointOnStart = true;
	Midpoint.CheckpointName = TEXT("Midpoint");
	Midpoint.RigRepairOnStart = 60.f;
	Midpoint.MaxConcurrentAttacks = 1;
	W.Director->Phases = { Phase(TEXT("Opening"), 2.f, 0, { Group(1, 0, 0.f) }), Midpoint };

	TestTrue(TEXT("Encounter starts"), W.Director->StartEncounter(77));
	TestEqual(TEXT("Mission started with it"), W.Mission->GetState(), EMissionState::Running);
	UAttackTokenSubsystem* Tokens = W.World->GetSubsystem<UAttackTokenSubsystem>();
	TestEqual(TEXT("Opening phase allows 2 attacks"), Tokens->GetMaxConcurrentAttacks(), 2);

	W.Rig->ApplyIntegrityDamage(100.f);
	W.Run(0.5f);
	W.KillAll();
	W.Run(2.f);
	TestEqual(TEXT("Reached the midpoint phase"), W.Director->GetPhaseIndex(), 1);
	TestEqual(TEXT("Midpoint repair +60 (P-05)"), W.Rig->GetCurrent(), 260.f);
	TestEqual(TEXT("Midpoint phase allows 1 attack"), Tokens->GetMaxConcurrentAttacks(), 1);
	const FMissionCheckpoint Checkpoint = W.Mission->GetLastCheckpoint();
	TestEqual(TEXT("Checkpoint recorded"), Checkpoint.Name, FName(TEXT("Midpoint")));
	TestEqual(TEXT("Checkpoint holds the phase"), Checkpoint.PhaseIndex, 1);
	TestEqual(TEXT("Checkpoint holds the seed"), Checkpoint.EncounterSeed, 77);
	TestEqual(TEXT("Checkpoint saved the repaired rig"), Checkpoint.RigIntegrity, 260.f);

	W.Run(1.f);
	const TArray<AEnemyBase*> FirstSpawns = W.Director->GetLiveEnemies();
	TestEqual(TEXT("Both lances spawned"), FirstSpawns.Num(), 2);
	const FVector FirstLocation = FirstSpawns.Num() > 0 ? FirstSpawns[0]->GetActorLocation() : FVector::ZeroVector;

	// Rig destroyed: the director stops.
	W.Rig->ApplyIntegrityDamage(1000.f);
	TestEqual(TEXT("Director suspends on failure"), W.Director->GetState(), EEncounterState::Suspended);
	const float TimeAtFailure = W.Director->GetMissionTime();
	W.Run(5.f);
	TestEqual(TEXT("Mission time stops while failed"), W.Director->GetMissionTime(), TimeAtFailure);

	// Retry: the same phase rebuilt with the same seed, no second repair.
	TestTrue(TEXT("Retry"), W.Mission->RetryFromLastCheckpoint());
	TestEqual(TEXT("Director running again"), W.Director->GetState(), EEncounterState::Running);
	TestEqual(TEXT("Back in the checkpoint phase"), W.Director->GetPhaseIndex(), 1);
	TestEqual(TEXT("Rig restored without a second repair"), W.Rig->GetCurrent(), 260.f);
	TestTrue(TEXT("Mission time rewound to the checkpoint"), FMath::IsNearlyEqual(W.Director->GetMissionTime(), Checkpoint.MissionTime, 0.001f));
	TestEqual(TEXT("Spawn counters reset"), W.Director->GetSpawnedCount(0), 0);

	W.Run(1.f);
	const TArray<AEnemyBase*> RetrySpawns = W.Director->GetLiveEnemies();
	TestEqual(TEXT("Phase repopulated"), RetrySpawns.Num(), 2);
	TestTrue(TEXT("Same seed, same spawn position"), RetrySpawns.Num() > 0 && RetrySpawns[0]->GetActorLocation().Equals(FirstLocation, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMEncounterEntranceTest, "ETM.Encounters.Director.EntranceValidation", ETMEncounterTestFlags)
bool FETMEncounterEntranceTest::RunTest(const FString&)
{
	{
		FTestWorld W;
		W.Director->Entrances.Add(W.MakeEntrance(FVector(-2000.f, 300.f, 0.f))); // 3: behind the rig
		W.Director->Phases = { Phase(TEXT("Ambush"), 30.f, 0, { Group(2, 3, 1.f) }) };
		AddExpectedError(TEXT("behind the player"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("Refuses an entrance behind the player"), W.Director->StartEncounter(6));
		TestEqual(TEXT("Nothing started"), W.Director->GetState(), EEncounterState::Idle);
		W.Run(2.f);
		TestEqual(TEXT("Nothing spawned"), W.Director->GetSpawnedCount(0), 0);
	}
	{
		FTestWorld W;
		W.Director->Phases = { Phase(TEXT("Typo"), 30.f, 0, { Group(2, 9, 1.f) }) };
		AddExpectedError(TEXT("does not exist"), EAutomationExpectedErrorFlags::Contains, 1);
		TestFalse(TEXT("Refuses a missing entrance"), W.Director->StartEncounter(7));
	}
	{
		FTestWorld W;
		W.Director->Entrances.Add(W.MakeEntrance(FVector(0.f, 2000.f, 0.f))); // 3: straight to the side
		W.Director->Phases = { Phase(TEXT("Flank"), 30.f, 0, { Group(1, 3, 1.f) }) };
		TestTrue(TEXT("A side entrance (90 degrees) is allowed"), W.Director->StartEncounter(8));
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
