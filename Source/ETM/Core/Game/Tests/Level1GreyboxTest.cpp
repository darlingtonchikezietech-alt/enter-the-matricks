// Automation tests for the Level 1 greybox (T-0020): the generated map follows the bible's layout rules, and
// the game mode plays any arena map through the campaign flow.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Level1; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Core/Flow/CampaignFlow.h"
#include "Core/Game/ETMSandboxGameMode.h"
#include "Encounters/EncounterDirector.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Player/XRSeatedRigPawn.h"
#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TargetPoint.h"
#include "Engine/World.h"
#include "GameFramework/PlayerStart.h"
#include "Tests/AutomationCommon.h"

namespace
{
	const EAutomationTestFlags ETMLevelTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const TCHAR* L1MapObject = TEXT("/Game/ETM/Maps/L1_Breach.L1_Breach");
	const float Frame72 = 1.f / 72.f;

	/** Actors of a loaded (not playing) map are not registered: their root's relative transform is their world transform. */
	FTransform RootTransform(const AActor* Actor)
	{
		return Actor && Actor->GetRootComponent() ? Actor->GetRootComponent()->GetRelativeTransform() : FTransform::Identity;
	}

	/** Does the segment From -> To pass through this static mesh's bounding box? */
	bool SegmentHitsMesh(const AStaticMeshActor* Actor, const FVector& From, const FVector& To)
	{
		const UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();
		const UStaticMesh* Mesh = Comp ? Comp->GetStaticMesh() : nullptr;
		if (!Mesh) return false;
		const FTransform T = RootTransform(Actor);
		const FBox Local = Mesh->GetBoundingBox();
		const FVector A = T.InverseTransformPosition(From);
		const FVector B = T.InverseTransformPosition(To);
		FVector HitLocation, HitNormal;
		float HitTime = 0.f;
		return FMath::LineExtentBoxIntersection(Local, A, B, FVector::ZeroVector, HitLocation, HitNormal, HitTime);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMLevel1LayoutTest, "ETM.Level1.GreyboxLayout", ETMLevelTestFlags)
bool FETMLevel1LayoutTest::RunTest(const FString&)
{
	UWorld* Map = LoadObject<UWorld>(nullptr, L1MapObject);
	TestNotNull(TEXT("L1_Breach exists (Tools/LevelGen/make_l1_greybox.py)"), Map);
	if (!Map || !Map->PersistentLevel) return false;

	TArray<APlayerStart*> Starts;
	TArray<AMissionObjective*> Objectives;
	TArray<AEncounterDirector*> Directors;
	TArray<AStaticMeshActor*> Meshes;
	int32 Lights = 0, ShadowLights = 0;
	for (AActor* Actor : Map->PersistentLevel->Actors)
	{
		if (!Actor) continue;
		if (APlayerStart* S = Cast<APlayerStart>(Actor)) Starts.Add(S);
		if (AMissionObjective* O = Cast<AMissionObjective>(Actor)) Objectives.Add(O);
		if (AEncounterDirector* D = Cast<AEncounterDirector>(Actor)) Directors.Add(D);
		if (AStaticMeshActor* M = Cast<AStaticMeshActor>(Actor)) Meshes.Add(M);
		if (ADirectionalLight* L = Cast<ADirectionalLight>(Actor))
		{
			++Lights;
			if (L->GetLightComponent()->CastShadows) ++ShadowLights;
		}
	}
	TestEqual(TEXT("One rig start"), Starts.Num(), 1);
	TestEqual(TEXT("One objective (the transfer equipment)"), Objectives.Num(), 1);
	TestEqual(TEXT("One encounter director"), Directors.Num(), 1);
	TestTrue(TEXT("Greybox geometry present"), Meshes.Num() > 30);
	TestEqual(TEXT("One light"), Lights, 1);
	TestEqual(TEXT("No shadow-casting lights (Quest baseline)"), ShadowLights, 0);
	if (Starts.Num() != 1 || Objectives.Num() != 1 || Directors.Num() != 1) return false;

	// The rig frame: the start's location and yaw. The eye is the rig's design eye above it.
	const FTransform Rig = RootTransform(Starts[0]);
	const FVector Eye = Rig.TransformPosition(GetDefault<AXRSeatedRigPawn>()->DesignEyeLocation);
	const auto InRig = [&Rig](const FVector& World) { return Rig.InverseTransformPosition(World); };
	const auto YawOf = [](const FVector& Local) { return FMath::RadiansToDegrees(FMath::Atan2(Local.Y, Local.X)); };

	// P-22: the transfer equipment is in front of and below the rig, in the centre lane.
	const FVector Equipment = InRig(RootTransform(Objectives[0]).GetLocation());
	TestTrue(TEXT("P-22: objective in front"), Equipment.X > 150.f);
	TestTrue(TEXT("P-22: objective in the centre lane"), FMath::Abs(YawOf(Equipment)) < 10.f);
	TestTrue(TEXT("P-22: objective below the eye"), RootTransform(Objectives[0]).GetLocation().Z + 100.f < Eye.Z);

	// Three lane entrances, all in front, inside the cannons' +/-65 degree envelope, and well apart.
	TArray<FVector> Entrances;
	for (const TObjectPtr<AActor>& Entrance : Directors[0]->Entrances)
	{
		TestNotNull(TEXT("Entrance set"), Entrance.Get());
		if (Entrance) Entrances.Add(RootTransform(Entrance).GetLocation());
	}
	TestEqual(TEXT("Three entrances (duct, blast door, drone shaft)"), Entrances.Num(), 3);
	TArray<float> Yaws;
	for (int32 i = 0; i < Entrances.Num(); ++i)
	{
		const FVector Local = InRig(Entrances[i]);
		const float Yaw = YawOf(Local);
		Yaws.Add(Yaw);
		TestTrue(FString::Printf(TEXT("Entrance %d is in front (nothing essential behind)"), i), Local.X > 500.f);
		TestTrue(FString::Printf(TEXT("Entrance %d inside the aiming envelope (yaw %.0f)"), i, Yaw), FMath::Abs(Yaw) <= 60.f);
		TestTrue(FString::Printf(TEXT("Entrance %d within 25 m"), i), Local.Size2D() < 2500.f);
	}
	if (Yaws.Num() == 3)
	{
		Yaws.Sort();
		TestTrue(TEXT("Lanes separated for independent aiming (25 degrees or more)"), Yaws[1] - Yaws[0] >= 25.f && Yaws[2] - Yaws[1] >= 25.f);
		TestTrue(TEXT("Left, centre and right"), Yaws[0] < -20.f && FMath::Abs(Yaws[1]) < 10.f && Yaws[2] > 20.f);
	}

	// Readable entrances: a clear line from the eye to each one (1 m above it) and to the objective.
	TArray<FVector> Targets;
	for (const FVector& E : Entrances) Targets.Add(E + FVector(0.f, 0.f, 100.f));
	Targets.Add(RootTransform(Objectives[0]).GetLocation() + FVector(0.f, 0.f, 100.f));
	for (int32 i = 0; i < Targets.Num(); ++i)
	{
		FString Blocker;
		for (const AStaticMeshActor* Mesh : Meshes)
		{
			if (SegmentHitsMesh(Mesh, Eye, Targets[i])) { Blocker = Mesh->GetActorLabel(); break; }
		}
		TestTrue(FString::Printf(TEXT("Clear sight line to target %d (blocked by '%s')"), i, *Blocker), Blocker.IsEmpty());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMArenaModeTest, "ETM.Level1.ArenaModePlaysTheMap", ETMLevelTestFlags)
bool FETMArenaModeTest::RunTest(const FString&)
{
	// A small arena built by hand in a test world: start, objective, director with entrances, one light.
	FTestWorldWrapper Wrapper;
	Wrapper.CreateTestWorld(EWorldType::Game);
	Wrapper.BeginPlayInTestWorld();
	UWorld* World = Wrapper.GetTestWorld();
	AETMSandboxGameMode* Mode = World->GetAuthGameMode<AETMSandboxGameMode>();
	if (!Mode) { AddError(TEXT("No sandbox game mode")); return false; }

	World->SpawnActor<APlayerStart>(FVector(0.f, 0.f, 120.f), FRotator::ZeroRotator);
	World->SpawnActor<AMissionObjective>(FVector(350.f, 0.f, 0.f), FRotator::ZeroRotator);
	AEncounterDirector* Director = World->SpawnActor<AEncounterDirector>();
	for (const float Yaw : { -35.f, 0.f, 35.f })
	{
		Director->Entrances.Add(World->SpawnActor<ATargetPoint>(FRotator(0.f, Yaw, 0.f).RotateVector(FVector(1800.f, 0.f, 0.f)), FRotator(0.f, Yaw + 180.f, 0.f)));
	}
	ADirectionalLight* Light = World->SpawnActor<ADirectionalLight>();
	Light->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Light->GetLightComponent()->SetIntensity(4.f);
	AXRSeatedRigPawn* Rig = World->SpawnActor<AXRSeatedRigPawn>(FVector(500.f, 500.f, 0.f), FRotator(0.f, 90.f, 0.f));
	World->GetSubsystem<UMissionStateSubsystem>()->RegisterRig(Rig->RigIntegrity, Rig->Defense);

	// The flow is in Level 1, as after START CAMPAIGN.
	UETMCampaignFlow* Flow = NewObject<UETMCampaignFlow>();
	TestEqual(TEXT("Level 1 is the greybox map"), Flow->LevelMaps[0], FString(TEXT("/Game/ETM/Maps/L1_Breach")));
	Flow->Loader = [Flow](const FString&) { Flow->NotifyWorldReady(nullptr); };
	Flow->StartCampaign();
	for (float T = 0.f; T < 2.f; T += Frame72) Flow->Tick(Frame72);
	TestEqual(TEXT("In Level 1"), Flow->GetStage(), EETMFlowStage::Level);

	Mode->SetFlow(Flow);
	TestTrue(TEXT("The map is an arena"), Mode->HasArena());
	TestFalse(TEXT("Not the Entry map: no generated range"), Mode->ShouldBuildSandbox());
	Mode->SetupArena();
	TestTrue(TEXT("Arena mode"), Mode->IsArena());
	TestTrue(TEXT("Rig placed on the map's start"), Rig->GetActorLocation().Equals(FVector(0.f, 0.f, 120.f), 0.1f) && Rig->GetActorRotation().IsNearlyZero(0.01f));
	TestTrue(TEXT("The map's director is used"), Mode->GetSandboxDirector() == Director);
	TestEqual(TEXT("Stand-in waves while the map has no schedule"), Director->Phases.Num(), AETMSandboxGameMode::MakeLevelPhases(0).Num());
	TestEqual(TEXT("Encounter running"), Director->GetState(), EEncounterState::Running);
	TestTrue(TEXT("Fades use the map's light at its own intensity"), FMath::IsNearlyEqual(Mode->GetCurrentLightIntensity(), 4.f));
	TestTrue(TEXT("Nothing generated: the map is used as it is"), Mode->GetSandboxActors().Num() == 0);

	for (float T = 0.f; T < 5.f; T += Frame72) Wrapper.TickTestWorld(Frame72);
	TestTrue(TEXT("Enemies arrive"), Director->GetLiveCount() > 0);
	Director->OnEncounterComplete.Broadcast();
	TestEqual(TEXT("Completing the arena reports to the flow"), Flow->GetStage(), EETMFlowStage::LevelResults);
	TestNotNull(TEXT("Results panel in the arena"), Mode->GetPanel());
	Wrapper.DestroyTestWorld(false);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
