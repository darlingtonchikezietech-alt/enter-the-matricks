// Automation tests for the cannon aim maths and UCannonComponent (aim, fire, heat, vent, obstruction).
// Heat-model parity with tools/balance_sim.py is a separate task (T-0010).
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Weapons; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Weapons/CannonAimModel.h"
#include "Weapons/CannonComponent.h"
#include "Components/BoxComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
	const EAutomationTestFlags ETMWeaponTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;

	/** A throwaway game world with a physics scene, so line traces work. */
	struct FTestWorld
	{
		UWorld* World = nullptr;

		FTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false, TEXT("ETMCannonTestWorld"));
			FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
			Context.SetCurrentWorld(World);
			World->InitializeActorsForPlay(FURL());
			World->BeginPlay();
		}

		~FTestWorld()
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}

		AActor* SpawnActorAt(const FVector& Location) const
		{
			AActor* Actor = World->SpawnActor<AActor>();
			USceneComponent* Root = NewObject<USceneComponent>(Actor);
			Actor->SetRootComponent(Root);
			Root->RegisterComponent();
			Actor->SetActorLocation(Location);
			return Actor;
		}

		/** Adds a blocking box to an actor. Location is relative to the actor. */
		UBoxComponent* AddBox(AActor* Actor, const FVector& RelativeLocation, const FVector& Extent, FName Tag = NAME_None) const
		{
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Box->SetupAttachment(Actor->GetRootComponent());
			Box->SetRelativeLocation(RelativeLocation);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionProfileName(TEXT("BlockAll"));
			if (!Tag.IsNone()) Box->ComponentTags.Add(Tag);
			Box->RegisterComponent();
			return Box;
		}

		/** A rig stand-in at the origin with one cannon at RelativeLocation, facing +X. */
		UCannonComponent* AddCannon(AActor* Rig, const FVector& RelativeLocation) const
		{
			UCannonComponent* Cannon = NewObject<UCannonComponent>(Rig);
			Cannon->SetupAttachment(Rig->GetRootComponent());
			Cannon->SetRelativeLocation(RelativeLocation);
			Cannon->RegisterComponent();
			return Cannon;
		}
	};

	void Advance(UCannonComponent* Cannon, float Seconds)
	{
		for (float T = 0.f; T < Seconds - KINDA_SMALL_NUMBER; T += Frame72)
		{
			Cannon->AdvanceCannon(FMath::Min(Frame72, Seconds - T));
		}
	}

	/** Fires exactly one shot from a cold, ready cannon. */
	void FireOnce(UCannonComponent* Cannon)
	{
		Cannon->SetTriggerHeld(true);
		Cannon->AdvanceCannon(Frame72);
		Cannon->SetTriggerHeld(false);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMCannonAimEnvelopeTest, "ETM.Weapons.Cannon.AimEnvelope", ETMWeaponTestFlags)
bool FETMCannonAimEnvelopeTest::RunTest(const FString&)
{
	const ETMCannon::FAimLimits Limits; // +/-65 yaw, -30..+45 pitch
	bool bClamped = false;

	FRotator Aim = ETMCannon::ClampAim(FRotator(20.f, -40.f, 0.f).Vector(), Limits, bClamped);
	TestTrue(TEXT("Inside the envelope the barrel follows exactly"), Aim.Equals(FRotator(20.f, -40.f, 0.f), 0.01f));
	TestFalse(TEXT("Inside the envelope is not clamped"), bClamped);

	Aim = ETMCannon::ClampAim(FRotator(0.f, 90.f, 0.f).Vector(), Limits, bClamped);
	TestTrue(TEXT("Yaw stops at +65"), FMath::IsNearlyEqual(Aim.Yaw, 65.0, 0.01));
	TestTrue(TEXT("Outside the envelope is reported"), bClamped);

	Aim = ETMCannon::ClampAim(FRotator(0.f, -170.f, 0.f).Vector(), Limits, bClamped);
	TestTrue(TEXT("Aiming behind stops at -65"), FMath::IsNearlyEqual(Aim.Yaw, -65.0, 0.01));

	Aim = ETMCannon::ClampAim(FRotator(-60.f, 0.f, 0.f).Vector(), Limits, bClamped);
	TestTrue(TEXT("Pitch stops at -30"), FMath::IsNearlyEqual(Aim.Pitch, -30.0, 0.01));
	Aim = ETMCannon::ClampAim(FRotator(80.f, 0.f, 0.f).Vector(), Limits, bClamped);
	TestTrue(TEXT("Pitch stops at +45"), FMath::IsNearlyEqual(Aim.Pitch, 45.0, 0.01));
	TestTrue(TEXT("Roll is always zero"), FMath::IsNearlyZero(Aim.Roll));

	// Servo: 360 deg/s for a quarter second covers 90 degrees but must not overshoot a 40 degree target.
	const FRotator Half = ETMCannon::StepServo(FRotator::ZeroRotator, FRotator(0.f, 40.f, 0.f), 0.05f, 360.f);
	TestTrue(TEXT("Servo moves at its rated speed"), FMath::IsNearlyEqual(Half.Yaw, 18.0, 0.01));
	const FRotator Done = ETMCannon::StepServo(FRotator::ZeroRotator, FRotator(0.f, 40.f, 0.f), 0.25f, 360.f);
	TestTrue(TEXT("Servo does not overshoot"), FMath::IsNearlyEqual(Done.Yaw, 40.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMCannonIndependenceTest, "ETM.Weapons.Cannon.IndependentCannons", ETMWeaponTestFlags)
bool FETMCannonIndependenceTest::RunTest(const FString&)
{
	FTestWorld W;
	AActor* Rig = W.SpawnActorAt(FVector::ZeroVector);
	UCannonComponent* Left = W.AddCannon(Rig, FVector(20.f, -45.f, 95.f));
	UCannonComponent* Right = W.AddCannon(Rig, FVector(20.f, 45.f, 95.f));

	// Left aims left and fires until it locks; right aims right and stays idle.
	Left->SetAimDirection(FRotator(0.f, -30.f, 0.f).Vector());
	Right->SetAimDirection(FRotator(10.f, 30.f, 0.f).Vector());
	Left->SetTriggerHeld(true);
	for (float T = 0.f; T < 11.f; T += Frame72)
	{
		Left->AdvanceCannon(Frame72);
		Right->AdvanceCannon(Frame72);
	}

	TestTrue(TEXT("Left barrel reached its own aim"), Left->GetRelativeRotation().Equals(FRotator(0.f, -30.f, 0.f), 0.1f));
	TestTrue(TEXT("Right barrel reached its own aim"), Right->GetRelativeRotation().Equals(FRotator(10.f, 30.f, 0.f), 0.1f));
	TestEqual(TEXT("Left fired 100 shots before locking (P-01, P-34)"), Left->GetShotsFired(), 100);
	TestTrue(TEXT("Left is overheated"), Left->IsOverheated());
	TestEqual(TEXT("Right never fired"), Right->GetShotsFired(), 0);
	TestEqual(TEXT("Right is cold"), Right->GetHeatNormalized(), 0.f);
	TestFalse(TEXT("Right is not overheated"), Right->IsOverheated());

	// Right can still fire and vent while left is locked.
	FireOnce(Right);
	TestEqual(TEXT("Right fires while left is locked"), Right->GetShotsFired(), 1);
	Right->SetVentHeld(true);
	Right->AdvanceCannon(Frame72);
	TestTrue(TEXT("Right vents while left is locked"), Right->IsVenting());
	TestFalse(TEXT("Left is not venting"), Left->IsVenting());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMCannonVentLockTest, "ETM.Weapons.Cannon.NoVentDuringLock", ETMWeaponTestFlags)
bool FETMCannonVentLockTest::RunTest(const FString&)
{
	FTestWorld W;
	AActor* Rig = W.SpawnActorAt(FVector::ZeroVector);
	UCannonComponent* Vented = W.AddCannon(Rig, FVector(0.f, -45.f, 95.f));
	UCannonComponent* Passive = W.AddCannon(Rig, FVector(0.f, 45.f, 95.f));

	for (UCannonComponent* Cannon : { Vented, Passive })
	{
		Cannon->SetTriggerHeld(true);
		Advance(Cannon, 11.f); // 100 shots to lock (P-34)
		Cannon->SetTriggerHeld(false);
	}
	TestTrue(TEXT("Both cannons are locked"), Vented->IsOverheated() && Passive->IsOverheated());

	// Holding vent during the lock must change nothing: both cool at the passive rate.
	Vented->SetVentHeld(true);
	for (float T = 0.f; T < 2.f; T += Frame72)
	{
		Vented->AdvanceCannon(Frame72);
		Passive->AdvanceCannon(Frame72);
	}
	TestFalse(TEXT("Vent does not open during a lock"), Vented->IsVenting());
	TestTrue(TEXT("Still locked after 2 s"), Vented->IsOverheated());
	TestTrue(TEXT("Holding vent gave no extra cooling"), FMath::IsNearlyEqual(Vented->GetHeatNormalized(), Passive->GetHeatNormalized(), 0.001f));

	// A locked cannon does not fire.
	const int32 ShotsBefore = Vented->GetShotsFired();
	Vented->SetVentHeld(false);
	Vented->SetTriggerHeld(true);
	Vented->AdvanceCannon(Frame72);
	TestEqual(TEXT("No shot while locked"), Vented->GetShotsFired(), ShotsBefore);
	Vented->SetTriggerHeld(false);

	// The lock clears at 35 heat; from then on the vent works and blocks firing on that cannon.
	Advance(Vented, 2.5f);
	TestFalse(TEXT("Lock clears once heat reaches the threshold"), Vented->IsOverheated());
	Vented->SetVentHeld(true);
	Vented->SetTriggerHeld(true);
	Vented->AdvanceCannon(Frame72);
	TestTrue(TEXT("Vent opens after the lock"), Vented->IsVenting());
	TestEqual(TEXT("A venting cannon does not fire"), Vented->GetShotsFired(), ShotsBefore);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMCannonDamageTest, "ETM.Weapons.Cannon.HitscanAndMultipliers", ETMWeaponTestFlags)
bool FETMCannonDamageTest::RunTest(const FString&)
{
	FTestWorld W;
	AActor* Rig = W.SpawnActorAt(FVector::ZeroVector);
	UCannonComponent* Cannon = W.AddCannon(Rig, FVector(0.f, 0.f, 100.f));

	// Nothing in front: a miss that ends 60 m out (P-18).
	FireOnce(Cannon);
	TestFalse(TEXT("Empty lane is a miss"), Cannon->GetLastShot().bHit);
	TestTrue(TEXT("Trace ends 60 m from the muzzle"), FMath::IsNearlyEqual(FVector::Dist(Cannon->GetLastShot().MuzzleLocation, Cannon->GetLastShot().EndLocation), 6000.0, 0.1));
	TestTrue(TEXT("Shot starts at the muzzle, not the barrel root"), Cannon->GetLastShot().MuzzleLocation.Equals(FVector(80.f, 0.f, 100.f), 0.1f));

	struct FCase { FName Tag; float ExpectedDamage; bool bWeakPoint; const TCHAR* Name; };
	const FCase Cases[] = {
		{ NAME_None,           25.f, false, TEXT("plain hull x1") },
		{ ETMTags::WeakPoint,  50.f, true,  TEXT("weak point x2") },
		{ ETMTags::Armor,     6.25f, false, TEXT("armor x0.25") },
		{ ETMTags::Immune,      0.f, false, TEXT("immune x0") },
	};
	for (const FCase& Case : Cases)
	{
		AActor* Target = W.SpawnActorAt(FVector(2000.f, 0.f, 100.f));
		W.AddBox(Target, FVector::ZeroVector, FVector(50.f), Case.Tag);

		Cannon->ResetHeat();
		FireOnce(Cannon);
		const FCannonShot& Shot = Cannon->GetLastShot();
		TestTrue(FString::Printf(TEXT("%s: hit registered"), Case.Name), Shot.bHit && Shot.HitActor == Target);
		TestEqual(FString::Printf(TEXT("%s: damage"), Case.Name), Shot.Damage, Case.ExpectedDamage);
		TestEqual(FString::Printf(TEXT("%s: weak-point flag"), Case.Name), Shot.bWeakPoint, Case.bWeakPoint);

		FVector AimPoint;
		TestTrue(FString::Printf(TEXT("%s: aim marker finds the target"), Case.Name), Cannon->GetBarrelImpactPoint(AimPoint));
		TestTrue(FString::Printf(TEXT("%s: aim marker is on the near face"), Case.Name), FMath::IsNearlyEqual(AimPoint.X, 1950.0, 1.0));
		Target->Destroy();
	}

	// A target past 60 m is out of range.
	AActor* Far = W.SpawnActorAt(FVector(6500.f, 0.f, 100.f));
	W.AddBox(Far, FVector::ZeroVector, FVector(50.f));
	Cannon->ResetHeat();
	FireOnce(Cannon);
	TestFalse(TEXT("Beyond 60 m is a miss"), Cannon->GetLastShot().bHit);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMCannonObstructionTest, "ETM.Weapons.Cannon.NeverThroughFrame", ETMWeaponTestFlags)
bool FETMCannonObstructionTest::RunTest(const FString&)
{
	FTestWorld W;
	AActor* Rig = W.SpawnActorAt(FVector::ZeroVector);
	UCannonComponent* Cannon = W.AddCannon(Rig, FVector(0.f, 0.f, 100.f));
	AActor* Target = W.SpawnActorAt(FVector(2000.f, 0.f, 100.f));
	W.AddBox(Target, FVector::ZeroVector, FVector(50.f));

	// The cannon's own barrel collision must not block it.
	UBoxComponent* Barrel = NewObject<UBoxComponent>(Rig);
	Barrel->SetupAttachment(Cannon);
	Barrel->SetRelativeLocation(FVector(60.f, 0.f, 0.f));
	Barrel->SetBoxExtent(FVector(60.f, 8.f, 8.f));
	Barrel->SetCollisionProfileName(TEXT("BlockAll"));
	Barrel->RegisterComponent();

	FireOnce(Cannon);
	TestTrue(TEXT("Own barrel does not block the shot"), Cannon->GetLastShot().bHit && Cannon->GetLastShot().HitActor == Target);

	// Frame plate ahead of the muzzle (muzzle at X=120, plate at X=200): the shot stops on the rig.
	UBoxComponent* Plate = W.AddBox(Rig, FVector(200.f, 0.f, 100.f), FVector(5.f, 40.f, 40.f));
	Cannon->ResetHeat();
	FireOnce(Cannon);
	TestTrue(TEXT("Frame ahead of the muzzle blocks the shot"), Cannon->GetLastShot().bBlocked);
	TestFalse(TEXT("Blocked shot is not a hit"), Cannon->GetLastShot().bHit);
	TestEqual(TEXT("Blocked shot deals no damage"), Cannon->GetLastShot().Damage, 0.f);
	TestTrue(TEXT("Blocked shot ends on the plate"), FMath::IsNearlyEqual(Cannon->GetLastShot().EndLocation.X, 195.0, 1.0));
	Plate->DestroyComponent();

	// Frame member between the barrel root and the muzzle (X=60).
	UBoxComponent* Strut = W.AddBox(Rig, FVector(60.f, 0.f, 150.f), FVector(5.f, 40.f, 40.f));
	Cannon->ResetHeat();
	FireOnce(Cannon);
	TestFalse(TEXT("Strut above the barrel does not block"), Cannon->GetLastShot().bBlocked);
	Strut->SetRelativeLocation(FVector(60.f, 0.f, 100.f));
	Barrel->DestroyComponent(); // so the strut is the only thing on the barrel line
	Cannon->ResetHeat();
	FireOnce(Cannon);
	TestTrue(TEXT("Frame between root and muzzle blocks the shot"), Cannon->GetLastShot().bBlocked);
	TestEqual(TEXT("No damage through the frame"), Cannon->GetLastShot().Damage, 0.f);
	Strut->DestroyComponent();

	// A wall that is not part of the rig, with the muzzle poked through it: still no shot beyond.
	AActor* Wall = W.SpawnActorAt(FVector(70.f, 0.f, 100.f)); // just behind the 80 cm muzzle
	W.AddBox(Wall, FVector::ZeroVector, FVector(5.f, 200.f, 200.f));
	Cannon->ResetHeat();
	FireOnce(Cannon);
	TestTrue(TEXT("Muzzle through a wall is blocked"), Cannon->GetLastShot().bBlocked);
	TestFalse(TEXT("Target behind the wall is not hit"), Cannon->GetLastShot().bHit);
	Wall->Destroy();

	// With every obstruction gone the same shot lands again.
	Cannon->ResetHeat();
	FireOnce(Cannon);
	TestTrue(TEXT("Clear lane hits again"), Cannon->GetLastShot().bHit && Cannon->GetLastShot().Damage == 25.f);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
