#include "Player/XRSeatedRigPawn.h"
#include "Camera/CameraComponent.h"
#include "MotionControllerComponent.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Weapons/CannonComponent.h"
#include "Combat/DefenseComponent.h"
#include "Combat/IntegrityComponent.h"

AXRSeatedRigPawn::AXRSeatedRigPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics; // aim before cannons tick

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	VROrigin->SetupAttachment(RootComponent);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(VROrigin);
	Camera->bLockToHmd = true; // headset controls the viewpoint directly; no code moves it

	LeftController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("LeftController"));
	LeftController->SetupAttachment(VROrigin);
	LeftController->SetTrackingMotionSource(FName(TEXT("Left")));

	RightController = CreateDefaultSubobject<UMotionControllerComponent>(TEXT("RightController"));
	RightController->SetupAttachment(VROrigin);
	RightController->SetTrackingMotionSource(FName(TEXT("Right")));

	RigFrame = CreateDefaultSubobject<USceneComponent>(TEXT("RigFrame"));
	RigFrame->SetupAttachment(RootComponent);

	// Prototype mount positions (cm, relative to rig root at seat height). Tune in T-0008 blockout.
	LeftMount = CreateDefaultSubobject<USceneComponent>(TEXT("LeftMount"));
	LeftMount->SetupAttachment(RigFrame);
	LeftMount->SetRelativeLocation(FVector(20.f, -45.f, 95.f));

	RightMount = CreateDefaultSubobject<USceneComponent>(TEXT("RightMount"));
	RightMount->SetupAttachment(RigFrame);
	RightMount->SetRelativeLocation(FVector(20.f, 45.f, 95.f));

	LeftCannon = CreateDefaultSubobject<UCannonComponent>(TEXT("LeftCannon"));
	LeftCannon->SetupAttachment(LeftMount);

	RightCannon = CreateDefaultSubobject<UCannonComponent>(TEXT("RightCannon"));
	RightCannon->SetupAttachment(RightMount);

	Defense = CreateDefaultSubobject<UDefenseComponent>(TEXT("Defense"));
	RigIntegrity = CreateDefaultSubobject<UIntegrityComponent>(TEXT("RigIntegrity"));
	RigIntegrity->MaxIntegrity = 300.f;
	RigIntegrity->bIsRig = true;
}

void AXRSeatedRigPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// Looking at a target never aims a cannon; only the controllers do (bible section 4).
	if (LeftController && LeftCannon)
	{
		const FQuat Aim = LeftController->GetComponentQuat() * LeftAimOffset.Quaternion();
		LeftCannon->SetAimDirection(Aim.GetForwardVector());
	}
	if (RightController && RightCannon)
	{
		const FQuat Aim = RightController->GetComponentQuat() * RightAimOffset.Quaternion();
		RightCannon->SetAimDirection(Aim.GetForwardVector());
	}
}

void AXRSeatedRigPawn::SetTrigger(bool bLeft, bool bHeld)
{
	if (UCannonComponent* C = bLeft ? LeftCannon.Get() : RightCannon.Get()) C->SetTriggerHeld(bHeld);
}

void AXRSeatedRigPawn::SetVent(bool bLeft, bool bHeld)
{
	if (UCannonComponent* C = bLeft ? LeftCannon.Get() : RightCannon.Get()) C->SetVentHeld(bHeld);
}

bool AXRSeatedRigPawn::ActivateBrace()
{
	return Defense ? Defense->TryActivateBrace() : false;
}

void AXRSeatedRigPawn::Recenter()
{
	UHeadMountedDisplayFunctionLibrary::ResetOrientationAndPosition(0.f, EOrientPositionSelector::OrientationAndPosition);
}

void AXRSeatedRigPawn::ForwardReAnchor()
{
	if (!Camera || !VROrigin) return;
	// Yaw difference between where the head faces and where the rig faces.
	const float HeadYaw = Camera->GetComponentRotation().Yaw;
	const float RigYaw = GetActorRotation().Yaw;
	const float Delta = FRotator::NormalizeAxis(RigYaw - HeadYaw);

	// Rotate the XR origin around the head's position so the player doesn't slide sideways.
	const FVector Pivot = Camera->GetComponentLocation();
	const FQuat Rot(FVector::UpVector, FMath::DegreesToRadians(Delta));
	const FVector NewOriginLoc = Pivot + Rot.RotateVector(VROrigin->GetComponentLocation() - Pivot);
	VROrigin->SetWorldLocationAndRotation(NewOriginLoc, Rot * VROrigin->GetComponentQuat());
}
