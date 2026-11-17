#include "Player/XRSeatedRigPawn.h"
#include "Player/SeatedCalibration.h"
#include "Player/CalibrationSaveGame.h"
#include "ETM.h"
#include "Camera/CameraComponent.h"
#include "MotionControllerComponent.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CoreDelegates.h"
#include "Weapons/CannonComponent.h"
#include "Weapons/Feedback/CannonFeedbackComponent.h"
#include "Combat/DefenseComponent.h"
#include "Combat/IntegrityComponent.h"

AXRSeatedRigPawn::AXRSeatedRigPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics; // aim before cannons tick

	// The view comes from the headset only: no controller rotation is ever applied to the pawn.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	VROrigin = CreateDefaultSubobject<USceneComponent>(TEXT("VROrigin"));
	VROrigin->SetupAttachment(RootComponent);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(VROrigin);
	Camera->bLockToHmd = true; // headset controls the viewpoint directly; no code moves it
	Camera->bUsePawnControlRotation = false;

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

	// Shot feedback rides on each cannon, so recoil moves the weapon and never the camera (bible section 11).
	LeftFeedback = CreateDefaultSubobject<UCannonFeedbackComponent>(TEXT("LeftFeedback"));
	LeftFeedback->SetupAttachment(LeftCannon);
	LeftFeedback->bLeftHand = true;
	RightFeedback = CreateDefaultSubobject<UCannonFeedbackComponent>(TEXT("RightFeedback"));
	RightFeedback->SetupAttachment(RightCannon);
	RightFeedback->bLeftHand = false;

	Defense = CreateDefaultSubobject<UDefenseComponent>(TEXT("Defense"));
	RigIntegrity = CreateDefaultSubobject<UIntegrityComponent>(TEXT("RigIntegrity"));
	RigIntegrity->MaxIntegrity = 300.f;
	RigIntegrity->bIsRig = true;
}

void AXRSeatedRigPawn::BeginPlay()
{
	Super::BeginPlay();

	LeftAimOffset = ETMSeated::ClampAimOffset(LeftAimOffset);
	RightAimOffset = ETMSeated::ClampAimOffset(RightAimOffset);
	LoadCalibration();

	if (UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
	{
		// Local is the seated tracking origin and is supported by every OpenXR runtime.
		UHeadMountedDisplayFunctionLibrary::SetTrackingOrigin(EHMDTrackingOrigin::Local);
	}

	// A system-level recenter (headset menu) shifts tracking space under us; re-seat the player when it happens.
	SystemRecenterHandle = FCoreDelegates::VRHeadsetRecenter.AddUObject(this, &AXRSeatedRigPawn::HandleSystemRecenter);
}

void AXRSeatedRigPawn::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FCoreDelegates::VRHeadsetRecenter.Remove(SystemRecenterHandle);
	SystemRecenterHandle.Reset();
	Super::EndPlay(EndPlayReason);
}

void AXRSeatedRigPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bAutoCalibrateOnStart && !bSeatCalibrated)
	{
		Recenter(); // no effect until the headset reports a pose
	}

	// Looking at a target never aims a cannon; only the controllers do (bible section 4).
	UpdateCannonAim(LeftController, LeftCannon, LeftAimOffset);
	UpdateCannonAim(RightController, RightCannon, RightAimOffset);
}

void AXRSeatedRigPawn::UpdateCannonAim(const UMotionControllerComponent* Hand, UCannonComponent* Cannon, const FRotator& Offset) const
{
	// When a controller loses tracking the cannon holds its last aim instead of snapping to a stale pose.
	if (!Hand || !Cannon || !Hand->IsTracked()) return;
	const FQuat Aim = Hand->GetComponentQuat() * Offset.Quaternion();
	Cannon->SetAimDirection(Aim.GetForwardVector());
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

bool AXRSeatedRigPawn::GetHeadPoseInTracking(FTransform& OutHead) const
{
	if (!UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled())
	{
		return false;
	}
	FRotator Rotation;
	FVector Position;
	UHeadMountedDisplayFunctionLibrary::GetOrientationAndPosition(Rotation, Position);
	if (Rotation.IsNearlyZero() && Position.IsNearlyZero())
	{
		return false; // the runtime has not delivered a pose yet
	}
	OutHead = FTransform(Rotation, Position);
	return true;
}

bool AXRSeatedRigPawn::Recenter()
{
	FTransform Head;
	if (!VROrigin || !GetHeadPoseInTracking(Head)) return false;
	VROrigin->SetRelativeTransform(ETMSeated::SolveSeatRecenter(Head, DesignEyeLocation, VROrigin->GetRelativeTransform()));
	bSeatCalibrated = true;
	OnCalibrationChanged.Broadcast();
	return true;
}

bool AXRSeatedRigPawn::ForwardReAnchor()
{
	FTransform Head;
	if (!VROrigin || !GetHeadPoseInTracking(Head)) return false;
	VROrigin->SetRelativeTransform(ETMSeated::SolveForwardReAnchor(Head, VROrigin->GetRelativeTransform()));
	OnCalibrationChanged.Broadcast();
	return true;
}

void AXRSeatedRigPawn::HandleSystemRecenter()
{
	bSeatCalibrated = false; // Tick re-seats the player on the next tracked frame
}

void AXRSeatedRigPawn::SetAimOffset(bool bLeft, FRotator Offset)
{
	(bLeft ? LeftAimOffset : RightAimOffset) = ETMSeated::ClampAimOffset(Offset);
	OnCalibrationChanged.Broadcast();
}

void AXRSeatedRigPawn::ResetAimOffsets()
{
	const AXRSeatedRigPawn* Defaults = GetClass()->GetDefaultObject<AXRSeatedRigPawn>();
	LeftAimOffset = ETMSeated::ClampAimOffset(Defaults->LeftAimOffset);
	RightAimOffset = ETMSeated::ClampAimOffset(Defaults->RightAimOffset);
	OnCalibrationChanged.Broadcast();
}

bool AXRSeatedRigPawn::SaveCalibration() const
{
	UETMCalibrationSaveGame* Save = NewObject<UETMCalibrationSaveGame>();
	Save->LeftAimOffset = LeftAimOffset;
	Save->RightAimOffset = RightAimOffset;
	const bool bSaved = UGameplayStatics::SaveGameToSlot(Save, UETMCalibrationSaveGame::SlotName(), UETMCalibrationSaveGame::UserIndex);
	if (!bSaved)
	{
		UE_LOG(LogETM, Warning, TEXT("Could not write calibration save slot '%s'."), UETMCalibrationSaveGame::SlotName());
	}
	return bSaved;
}

bool AXRSeatedRigPawn::LoadCalibration()
{
	if (!UGameplayStatics::DoesSaveGameExist(UETMCalibrationSaveGame::SlotName(), UETMCalibrationSaveGame::UserIndex))
	{
		return false;
	}
	const UETMCalibrationSaveGame* Save = Cast<UETMCalibrationSaveGame>(
		UGameplayStatics::LoadGameFromSlot(UETMCalibrationSaveGame::SlotName(), UETMCalibrationSaveGame::UserIndex));
	if (!Save || Save->Version != UETMCalibrationSaveGame::CurrentVersion)
	{
		UE_LOG(LogETM, Warning, TEXT("Calibration save slot '%s' is unreadable or from another version; using defaults."), UETMCalibrationSaveGame::SlotName());
		return false;
	}
	LeftAimOffset = ETMSeated::ClampAimOffset(Save->LeftAimOffset);
	RightAimOffset = ETMSeated::ClampAimOffset(Save->RightAimOffset);
	OnCalibrationChanged.Broadcast();
	return true;
}
