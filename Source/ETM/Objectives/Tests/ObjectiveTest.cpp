// Automation tests for AMissionObjective and UMissionStateSubsystem.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Objectives; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Objectives/Tests/ObjectiveTestListener.h"
#include "Combat/IntegrityComponent.h"
#include "Combat/DefenseComponent.h"
#include "Enemies/Base/EnemyBase.h"
#include "Enemies/Base/EnemyPoolSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const EAutomationTestFlags ETMObjectiveTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;

	struct FTestWorld
	{
		UWorld* World = nullptr;
		AMissionObjective* Objective = nullptr;
		AActor* RigActor = nullptr;
		UIntegrityComponent* Rig = nullptr;
		UDefenseComponent* Brace = nullptr;
		UMissionStateSubsystem* Mission = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ETMObjectiveTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
			World->SetBegunPlay(true); // no game mode, so mark it begun or spawned actors skip BeginPlay

			Mission = World->GetSubsystem<UMissionStateSubsystem>();
			Objective = World->SpawnActor<AMissionObjective>(FVector(0.f, 0.f, 0.f), FRotator::ZeroRotator);

			// A rig stand-in: an actor with the same health and brace components as AXRSeatedRigPawn.
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
			Mission->RegisterRig(Rig, Brace);
		}

		~FTestWorld()
		{
			World->EndPlay(EEndPlayReason::Quit);
			World->SetBegunPlay(false);
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		/** An enemy, optionally tagged as an infrastructure attacker, ready to attack. */
		AEnemyBase* SpawnAttacker(bool bDesignated) const
		{
			AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(FVector(1500.f, 300.f, 0.f), FRotator::ZeroRotator);
			if (bDesignated) Enemy->Tags.Add(ETMObjectiveTags::InfrastructureAttacker);
			Enemy->MarkPooled();
			Enemy->Activate(Enemy->GetActorTransform());
			for (float T = 0.f; T < 1.f; T += Frame72) Enemy->AdvanceState(Frame72);
			return Enemy;
		}
	};

	/** A telegraph-free attack, resolved on the next state step. */
	void StrikeOnce(AEnemyBase* Enemy, AActor* Target, float Damage)
	{
		Enemy->BeginAttack(Target, Damage, 0.f);
		Enemy->AdvanceState(Frame72);
	}

	void Listen(UMissionStateSubsystem* Mission, AMissionObjective* Objective, UETMObjectiveTestListener* Listener)
	{
		Mission->OnMissionFailed.AddDynamic(Listener, &UETMObjectiveTestListener::HandleFailed);
		Mission->OnCheckpointRecorded.AddDynamic(Listener, &UETMObjectiveTestListener::HandleCheckpoint);
		Mission->OnRetryStarted.AddDynamic(Listener, &UETMObjectiveTestListener::HandleRetry);
		Objective->OnUnderAttack.AddDynamic(Listener, &UETMObjectiveTestListener::HandleUnderAttack);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMObjectiveDesignatedAttackersTest, "ETM.Objectives.Objective.DesignatedAttackersOnly", ETMObjectiveTestFlags)
bool FETMObjectiveDesignatedAttackersTest::RunTest(const FString&)
{
	FTestWorld W;
	UETMObjectiveTestListener* Listener = NewObject<UETMObjectiveTestListener>();
	Listen(W.Mission, W.Objective, Listener);

	TestTrue(TEXT("Objective registered itself"), W.Mission->GetObjective() == W.Objective);
	TestEqual(TEXT("Objective starts at 500"), W.Objective->Integrity->GetCurrent(), 500.f);
	TestFalse(TEXT("Objective is not the rig"), W.Objective->Integrity->bIsRig);

	// A designated attacker's strike lands, flags the marker and records the direction.
	AEnemyBase* Siphon = W.SpawnAttacker(true);
	StrikeOnce(Siphon, W.Objective, 20.f);
	TestEqual(TEXT("Designated attacker damages the objective"), W.Objective->Integrity->GetCurrent(), 480.f);
	TestTrue(TEXT("Marker shows under attack"), W.Objective->IsUnderAttack());
	TestEqual(TEXT("Under-attack cue fires once"), Listener->UnderAttack, 1);
	TestTrue(TEXT("Attacker direction is recorded"), W.Objective->GetLastAttackerLocation().Equals(FVector(1500.f, 300.f, 0.f), 0.1f));
	StrikeOnce(Siphon, W.Objective, 20.f);
	TestEqual(TEXT("Second hit in the same attack window does not re-cue"), Listener->UnderAttack, 1);

	// An enemy that is not an infrastructure attacker (a Needle Drone aimed at the rig) cannot hurt it.
	AEnemyBase* Drone = W.SpawnAttacker(false);
	StrikeOnce(Drone, W.Objective, 15.f);
	TestEqual(TEXT("Undesignated enemy is refused"), W.Objective->Integrity->GetCurrent(), 460.f);

	// The player's cannon fire (causer: the rig) and anonymous damage are refused.
	FHitResult Hit;
	UGameplayStatics::ApplyPointDamage(W.Objective, 40.f, FVector::ForwardVector, Hit, nullptr, W.RigActor, nullptr);
	UGameplayStatics::ApplyDamage(W.Objective, 40.f, nullptr, nullptr, nullptr);
	TestEqual(TEXT("Cannon fire and sourceless damage do nothing"), W.Objective->Integrity->GetCurrent(), 460.f);
	TestEqual(TEXT("Every refusal is counted"), W.Objective->GetRejectedDamageCount(), 3);

	// Class allow-list works as an alternative to the tag.
	W.Objective->DesignatedAttackerClasses.Add(AEnemyBase::StaticClass());
	StrikeOnce(Drone, W.Objective, 15.f);
	TestEqual(TEXT("Allow-listed class may damage"), W.Objective->Integrity->GetCurrent(), 445.f);

	// A brace on the rig does not protect the objective.
	W.Brace->TryActivateBrace();
	StrikeOnce(Siphon, W.Objective, 20.f);
	TestEqual(TEXT("Brace never protects the objective"), W.Objective->Integrity->GetCurrent(), 425.f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMMissionObjectiveFailureTest, "ETM.Objectives.Mission.ObjectiveFailureAndRetry", ETMObjectiveTestFlags)
bool FETMMissionObjectiveFailureTest::RunTest(const FString&)
{
	FTestWorld W;
	UETMObjectiveTestListener* Listener = NewObject<UETMObjectiveTestListener>();
	Listen(W.Mission, W.Objective, Listener);

	TestEqual(TEXT("Idle before the mission starts"), W.Mission->GetState(), EMissionState::Idle);
	TestFalse(TEXT("No checkpoint before the start"), W.Mission->RecordCheckpoint(TEXT("Early"), 0, 0.f, false));
	W.Mission->StartMission(1234);
	TestEqual(TEXT("Running"), W.Mission->GetState(), EMissionState::Running);
	TestEqual(TEXT("Start checkpoint recorded"), Listener->LastCheckpointName, FName(TEXT("Start")));

	AEnemyBase* Attacker = W.SpawnAttacker(true);
	StrikeOnce(Attacker, W.Objective, 100.f);
	W.Rig->ApplyIntegrityDamage(90.f);

	// Level 1 midpoint (05:00): health at that moment is what a retry restores.
	TestTrue(TEXT("Midpoint checkpoint recorded"), W.Mission->RecordCheckpoint(TEXT("Midpoint"), 3, 300.f, false, 1234));
	const FMissionCheckpoint Midpoint = W.Mission->GetLastCheckpoint();
	TestEqual(TEXT("Checkpoint stores rig health"), Midpoint.RigIntegrity, 210.f);
	TestEqual(TEXT("Checkpoint stores objective health"), Midpoint.ObjectiveIntegrity, 400.f);
	TestEqual(TEXT("Checkpoint stores the phase"), Midpoint.PhaseIndex, 3);
	TestEqual(TEXT("Checkpoint stores the seed"), Midpoint.EncounterSeed, 1234);

	// Objective destroyed: failure fires once with the right reason.
	StrikeOnce(Attacker, W.Objective, 1000.f);
	TestEqual(TEXT("Mission failed"), W.Mission->GetState(), EMissionState::Failed);
	TestEqual(TEXT("Fail reason"), W.Mission->GetFailReason(), EMissionFailReason::ObjectiveDestroyed);
	TestEqual(TEXT("One failure event"), Listener->Failed, 1);
	W.Rig->ApplyIntegrityDamage(1000.f);
	TestEqual(TEXT("A second pool emptying does not fail again"), Listener->Failed, 1);
	TestFalse(TEXT("No checkpoints while failed"), W.Mission->RecordCheckpoint(TEXT("Late"), 4, 400.f, false));

	// Retry: back to the midpoint, enemies parked, director told which phase to rebuild.
	TestTrue(TEXT("Retry succeeds"), W.Mission->RetryFromLastCheckpoint());
	TestEqual(TEXT("Running again"), W.Mission->GetState(), EMissionState::Running);
	TestEqual(TEXT("Rig restored to its checkpoint value"), W.Rig->GetCurrent(), 210.f);
	TestEqual(TEXT("Objective restored to its checkpoint value"), W.Objective->Integrity->GetCurrent(), 400.f);
	TestFalse(TEXT("Rig is alive again"), W.Rig->IsDepleted());
	TestFalse(TEXT("Objective is alive again"), W.Objective->Integrity->IsDepleted());
	TestFalse(TEXT("Enemies outside the pool are parked too"), Attacker->IsActive());
	TestEqual(TEXT("Retry event names the phase"), Listener->LastRetryPhase, 3);
	TestEqual(TEXT("Retry count"), W.Mission->GetRetryCount(), 1);
	TestFalse(TEXT("Cannot retry while running"), W.Mission->RetryFromLastCheckpoint());

	// The failure watch is re-armed after a retry.
	W.Rig->ApplyIntegrityDamage(1000.f);
	TestEqual(TEXT("Rig destruction fails the mission"), W.Mission->GetFailReason(), EMissionFailReason::RigDestroyed);
	TestEqual(TEXT("Second failure event"), Listener->Failed, 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMMissionBossPresetTest, "ETM.Objectives.Mission.BossCheckpointPreset", ETMObjectiveTestFlags)
bool FETMMissionBossPresetTest::RunTest(const FString&)
{
	FTestWorld W;
	W.Mission->StartMission();
	AEnemyBase* Attacker = W.SpawnAttacker(true);

	// Reach the boss (07:15) badly hurt: rig 40, objective 100.
	W.Rig->ApplyIntegrityDamage(260.f);
	StrikeOnce(Attacker, W.Objective, 400.f);
	W.Mission->RecordCheckpoint(TEXT("Boss"), 6, 435.f, true);

	W.Rig->ApplyIntegrityDamage(1000.f);
	TestTrue(TEXT("Retry from the boss checkpoint"), W.Mission->RetryFromLastCheckpoint());
	TestEqual(TEXT("P-04: rig restored to 300"), W.Rig->GetCurrent(), 300.f);
	TestEqual(TEXT("P-04 Standard: objective raised to 60%"), W.Objective->Integrity->GetCurrent(), 300.f);

	TestFalse(TEXT("Retry parked the attacker"), Attacker->IsActive());

	// A healthier objective keeps its saved value. (Later hits go straight to the pool: the attacker is parked.)
	W.Objective->Integrity->ResetTo(450.f);
	W.Mission->RecordCheckpoint(TEXT("Boss"), 6, 435.f, true);
	W.Objective->Integrity->ApplyIntegrityDamage(1000.f);
	TestEqual(TEXT("Objective destroyed at the boss"), W.Mission->GetFailReason(), EMissionFailReason::ObjectiveDestroyed);
	W.Mission->RetryFromLastCheckpoint();
	TestEqual(TEXT("Saved value above the floor is kept"), W.Objective->Integrity->GetCurrent(), 450.f);

	// Assisted floor is 80%.
	W.Objective->Integrity->ResetTo(100.f);
	W.Mission->RecordCheckpoint(TEXT("Boss"), 6, 435.f, true);
	W.Mission->BossObjectiveFloor = 0.8f;
	W.Objective->Integrity->ApplyIntegrityDamage(1000.f);
	W.Mission->RetryFromLastCheckpoint();
	TestEqual(TEXT("P-04 Assisted: objective raised to 80%"), W.Objective->Integrity->GetCurrent(), 400.f);

	// The brace is ready after a retry (5000 so the braced hit still empties the rig).
	W.Brace->TryActivateBrace();
	W.Rig->ApplyIntegrityDamage(5000.f);
	W.Mission->RetryFromLastCheckpoint();
	TestTrue(TEXT("Brace is reset by a retry"), W.Brace->IsBraceReady());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMMissionPoolRetryTest, "ETM.Objectives.Mission.RetryParksPooledEnemies", ETMObjectiveTestFlags)
bool FETMMissionPoolRetryTest::RunTest(const FString&)
{
	FTestWorld W;
	UEnemyPoolSubsystem* Pool = W.World->GetSubsystem<UEnemyPoolSubsystem>();
	Pool->Prewarm(AEnemyBase::StaticClass(), 6);
	W.Mission->StartMission();

	for (int32 Index = 0; Index < 6; ++Index)
	{
		Pool->Acquire(AEnemyBase::StaticClass(), FTransform(FVector(1000.f, 100.f * Index, 0.f)));
	}
	TestEqual(TEXT("Six enemies in play"), Pool->GetThreatCount(), 6);

	W.Rig->ApplyIntegrityDamage(1000.f);
	W.Mission->RetryFromLastCheckpoint();
	TestEqual(TEXT("Retry parks every pooled enemy"), Pool->GetActiveCount(), 0);
	TestEqual(TEXT("Retry created no new enemies"), Pool->GetTotalCreated(), 6);
	TestEqual(TEXT("Level-start retry restores full rig"), W.Rig->GetCurrent(), 300.f);
	TestEqual(TEXT("Level-start retry restores full objective"), W.Objective->Integrity->GetCurrent(), 500.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
