// Automation tests for AETMSandboxGameMode: default classes, the range it builds, and auto-retry.
// Uses the engine's FTestWorldWrapper, so the world has a real game mode and ticks real frames.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Core.Sandbox; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/Game/ETMSandboxGameMode.h"
#include "Encounters/EncounterDirector.h"
#include "Enemies/Base/EnemyBase.h"
#include "Input/RigPlayerController.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Player/XRSeatedRigPawn.h"
#include "Combat/IntegrityComponent.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/GameModeBase.h"
#include "Tests/AutomationCommon.h"

namespace
{
	const EAutomationTestFlags ETMSandboxTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMSandboxDefaultsTest, "ETM.Core.Sandbox.DefaultGameModeAndRig", ETMSandboxTestFlags)
bool FETMSandboxDefaultsTest::RunTest(const FString&)
{
	const AETMSandboxGameMode* Defaults = GetDefault<AETMSandboxGameMode>();
	TestTrue(TEXT("Player is RIG-6"), Defaults->DefaultPawnClass == AXRSeatedRigPawn::StaticClass());
	TestTrue(TEXT("Driven by the rig controller"), Defaults->PlayerControllerClass == ARigPlayerController::StaticClass());

	// The project's default game mode (Config/DefaultEngine.ini) is the sandbox.
	FTestWorldWrapper Wrapper;
	Wrapper.CreateTestWorld(EWorldType::Game);
	Wrapper.BeginPlayInTestWorld();
	UWorld* World = Wrapper.GetTestWorld();
	AETMSandboxGameMode* Mode = World ? World->GetAuthGameMode<AETMSandboxGameMode>() : nullptr;
	TestNotNull(TEXT("Worlds get the sandbox game mode by default"), Mode);
	if (Mode)
	{
		TestFalse(TEXT("A test world (not the Entry map) does not build the range by itself"), Mode->ShouldBuildSandbox());
		int32 Objectives = 0;
		for (TActorIterator<AMissionObjective> It(World); It; ++It) ++Objectives;
		TestEqual(TEXT("... so nothing was spawned"), Objectives, 0);
	}
	Wrapper.DestroyTestWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMSandboxRangeTest, "ETM.Core.Sandbox.RangeAndPracticeEncounter", ETMSandboxTestFlags)
bool FETMSandboxRangeTest::RunTest(const FString&)
{
	FTestWorldWrapper Wrapper;
	Wrapper.CreateTestWorld(EWorldType::Game);
	Wrapper.BeginPlayInTestWorld();
	UWorld* World = Wrapper.GetTestWorld();
	AETMSandboxGameMode* Mode = World ? World->GetAuthGameMode<AETMSandboxGameMode>() : nullptr;
	if (!Mode) { AddError(TEXT("No sandbox game mode")); return false; }

	// Stand in for the local player: a rig at the origin, registered the way RestartPlayer does it.
	AXRSeatedRigPawn* Rig = World->SpawnActor<AXRSeatedRigPawn>(FVector::ZeroVector, FRotator::ZeroRotator);
	World->GetSubsystem<UMissionStateSubsystem>()->RegisterRig(Rig->RigIntegrity, Rig->Defense);

	Mode->SetFlow(nullptr); // no campaign flow: the practice range (T-0031 adds the campaign stages)
	Mode->BuildSandbox();
	AEncounterDirector* Director = Mode->GetSandboxDirector();
	AMissionObjective* Objective = Mode->GetSandboxObjective();
	TestNotNull(TEXT("Director spawned"), Director);
	TestNotNull(TEXT("Objective spawned"), Objective);
	if (!Director || !Objective) return false;
	Director->RigActor = Rig;

	// Layout checks.
	const FVector Eye = Rig->GetActorTransform().TransformPosition(Rig->DesignEyeLocation);
	TestTrue(TEXT("Objective is in front of the rig"), Objective->GetActorLocation().X > 200.f && FMath::Abs(Objective->GetActorLocation().Y) < 1.f);
	TestTrue(TEXT("Objective is below the eye (P-22)"), Objective->GetActorLocation().Z < Eye.Z);
	// Nothing the range built blocks the view down the centre lane: the line from the eye to a point on the
	// floor 10 m out stays clear (owner headset test, T-0031: the objective bulk hid the centre lane).
	{
		FHitResult Block;
		FCollisionQueryParams Params;
		Params.AddIgnoredActor(Rig);
		const bool bBlocked = World->LineTraceSingleByChannel(Block, Eye, FVector(1000.f, 0.f, 50.f), ECC_Visibility, Params);
		TestFalse(FString::Printf(TEXT("Centre lane visible from the eye (blocked by %s)"), bBlocked && Block.GetActor() ? *Block.GetActor()->GetName() : TEXT("nothing")), bBlocked);
	}
	TestEqual(TEXT("Three lane entrances"), Director->Entrances.Num(), 3);
	TestTrue(TEXT("No entrance is behind the player"), Director->ValidateEntrances().IsEmpty());
	int32 Lights = 0, ShadowLights = 0;
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		++Lights;
		if (It->GetLightComponent()->CastShadows) ++ShadowLights;
	}
	TestEqual(TEXT("One light"), Lights, 1);
	TestEqual(TEXT("No shadow-casting lights (Quest baseline)"), ShadowLights, 0);
	TestTrue(TEXT("Floor, posts and fittings spawned"), Mode->GetSandboxActors().Num() > 15);

	// The practice encounter runs: enemies appear within the first seconds.
	TestEqual(TEXT("Encounter running"), Director->GetState(), EEncounterState::Running);
	for (float T = 0.f; T < 10.f; T += Frame72) Wrapper.TickTestWorld(Frame72);
	TestTrue(FString::Printf(TEXT("Enemies arrive (%d live)"), Director->GetLiveCount()), Director->GetLiveCount() > 0);

	// Failure retries by itself after the beat.
	UMissionStateSubsystem* Mission = World->GetSubsystem<UMissionStateSubsystem>();
	Rig->RigIntegrity->ApplyIntegrityDamage(10000.f);
	TestEqual(TEXT("Mission failed"), Mission->GetState(), EMissionState::Failed);
	for (float T = 0.f; T < Mode->AutoRetrySeconds + 0.5f; T += Frame72) Wrapper.TickTestWorld(Frame72);
	TestEqual(TEXT("Retried automatically"), Mission->GetState(), EMissionState::Running);
	TestEqual(TEXT("Rig back to full"), Rig->RigIntegrity->GetCurrent(), Rig->RigIntegrity->GetMax());

	const int32 Built = Mode->GetSandboxActors().Num();
	Mode->BuildSandbox();
	TestEqual(TEXT("Building twice does nothing"), Mode->GetSandboxActors().Num(), Built);
	Wrapper.DestroyTestWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
