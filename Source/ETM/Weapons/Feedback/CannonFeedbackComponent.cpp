#include "Weapons/Feedback/CannonFeedbackComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

UCannonFeedbackComponent::UCannonFeedbackComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // ticks only while something is animating
	PrimaryComponentTick.TickGroup = TG_PostPhysics;     // after the cannon has fired this frame

	// Hard references so the placeholders are cooked into the package.
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Emissive(TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
	static ConstructorHelpers::FObjectFinderOptional<USoundBase> Tone(TEXT("/Engine/EngineSounds/1kSineTonePing.1kSineTonePing"));
	BarrelMesh = Cylinder.Get();
	TracerMesh = Cylinder.Get();
	FlashMesh = Cone.Get();
	DischargeMaterial = Emissive.Get();
	FireSound = Tone.Get();
	(void)Sphere; // impact flashes use FlashMesh scaled round; Sphere is kept loaded for art swaps
}

UStaticMeshComponent* UCannonFeedbackComponent::MakeVisual(const TCHAR* Name, UStaticMesh* Mesh, USceneComponent* Parent, bool bWorldSpace)
{
	UStaticMeshComponent* Visual = NewObject<UStaticMeshComponent>(GetOwner(), MakeUniqueObjectName(GetOwner(), UStaticMeshComponent::StaticClass(), Name));
	Visual->SetStaticMesh(Mesh);
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision); // cosmetic: never blocks or receives a shot
	Visual->SetCastShadow(false);
	Visual->SetCanEverAffectNavigation(false);
	Visual->SetupAttachment(Parent);
	if (bWorldSpace)
	{
		Visual->SetUsingAbsoluteLocation(true);
		Visual->SetUsingAbsoluteRotation(true);
		Visual->SetUsingAbsoluteScale(true);
	}
	Visual->RegisterComponent();
	++CreatedVisualCount;
	PooledMeshes.Add(Visual);
	return Visual;
}

void UCannonFeedbackComponent::BeginPlay()
{
	Super::BeginPlay();

	Cannon = Cast<UCannonComponent>(GetAttachParent());
	if (Cannon)
	{
		Cannon->OnFired.AddUniqueDynamic(this, &UCannonFeedbackComponent::HandleShot);
		Cannon->OnOverheatChanged.AddUniqueDynamic(this, &UCannonFeedbackComponent::HandleOverheatChanged);
	}

	if (DischargeMaterial)
	{
		DischargeInstance = UMaterialInstanceDynamic::Create(DischargeMaterial, this);
		DischargeInstance->SetVectorParameterValue(TEXT("Color"), DischargeColor * 4.f); // over 1 so it reads as light
	}

	// Everything is built once here; shots only reuse it.
	RecoilPivot = NewObject<USceneComponent>(GetOwner(), MakeUniqueObjectName(GetOwner(), USceneComponent::StaticClass(), TEXT("RecoilPivot")));
	RecoilPivot->SetupAttachment(this);
	RecoilPivot->RegisterComponent();

	const FVector Muzzle = Cannon ? Cannon->MuzzleOffset : FVector(80.f, 0.f, 0.f);
	Barrel = MakeVisual(TEXT("BarrelVisual"), BarrelMesh, RecoilPivot, false);
	Barrel->SetRelativeLocation(Muzzle * 0.5f);
	Barrel->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f));
	Barrel->SetRelativeScale3D(FVector(0.16f, 0.16f, FMath::Max(Muzzle.X, 1.f) / 100.f));

	MuzzleFlash = MakeVisual(TEXT("MuzzleFlash"), FlashMesh, RecoilPivot, false);
	MuzzleFlash->SetRelativeLocation(Muzzle + FVector(15.f, 0.f, 0.f));
	MuzzleFlash->SetRelativeRotation(FRotator(-90.f, 0.f, 0.f)); // directional: points down the barrel
	MuzzleFlash->SetVisibility(false);
	if (DischargeInstance) MuzzleFlash->SetMaterial(0, DischargeInstance);

	Tracers.SetNum(TracerPoolSize);
	for (int32 Index = 0; Index < Tracers.Num(); ++Index)
	{
		Tracers[Index].Mesh = MakeVisual(TEXT("Tracer"), TracerMesh, this, true);
		Tracers[Index].Mesh->SetVisibility(false);
		if (DischargeInstance) Tracers[Index].Mesh->SetMaterial(0, DischargeInstance);
	}
	Impacts.SetNum(ImpactPoolSize);
	for (int32 Index = 0; Index < Impacts.Num(); ++Index)
	{
		Impacts[Index].Mesh = MakeVisual(TEXT("ImpactFlash"), FlashMesh, this, true);
		Impacts[Index].Mesh->SetVisibility(false);
		if (DischargeInstance) Impacts[Index].Mesh->SetMaterial(0, DischargeInstance);
	}
}

void UCannonFeedbackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopHaptic();
	if (Cannon)
	{
		Cannon->OnFired.RemoveDynamic(this, &UCannonFeedbackComponent::HandleShot);
		Cannon->OnOverheatChanged.RemoveDynamic(this, &UCannonFeedbackComponent::HandleOverheatChanged);
	}
	Super::EndPlay(EndPlayReason);
}

// ---------------------------------------------------------------------------------------------
// Shots
// ---------------------------------------------------------------------------------------------

void UCannonFeedbackComponent::HandleShot(const FCannonShot& Shot)
{
	// 1 + 5: barrel and mount kick. Only this component's pivot moves; the camera is never touched.
	RecoilTime = 0.f;

	// 2: muzzle discharge.
	FlashAge = 0.f;
	if (MuzzleFlash)
	{
		const float Size = bReducedFlash ? 0.35f : 0.7f;
		MuzzleFlash->SetRelativeScale3D(FVector(Size, Size, Size * 0.6f));
		MuzzleFlash->SetVisibility(true);
	}

	// 3: tracer, kept even on reduced flash because it shows where the shot went.
	if (Tracers.Num() > 0)
	{
		FTracer& Tracer = Tracers[NextTracer];
		NextTracer = (NextTracer + 1) % Tracers.Num();
		Tracer.Start = Shot.MuzzleLocation;
		Tracer.End = Shot.EndLocation;
		Tracer.Age = 0.f;
		UpdateTracer(Tracer);
	}

	// 4: impact flash on anything solid (a blocked shot sparks on the frame, smaller). Debris via OnImpact.
	if ((Shot.bHit || Shot.bBlocked) && Impacts.Num() > 0)
	{
		FImpact& Impact = Impacts[NextImpact];
		NextImpact = (NextImpact + 1) % Impacts.Num();
		Impact.Age = 0.f;
		Impact.Size = (Shot.bBlocked ? 0.25f : (Shot.bWeakPoint ? 0.8f : 0.6f)) * (bReducedFlash ? 0.5f : 1.f);
		Impact.Mesh->SetWorldLocation(Shot.EndLocation);
		Impact.Mesh->SetWorldScale3D(FVector(Impact.Size));
		Impact.Mesh->SetVisibility(true);
		OnImpact.Broadcast(Shot.EndLocation, Shot.bWeakPoint, Shot.bBlocked);
	}

	// 6: haptic pulse on this hand.
	PlayHaptic(ShotHapticAmplitude, ShotHapticSeconds);

	// 7: report.
	if (FireSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FireSound, Shot.MuzzleLocation, 1.f, FireSoundPitch);
		++ReportsPlayed;
	}

	SetComponentTickEnabled(true);
}

void UCannonFeedbackComponent::HandleOverheatChanged(bool bOverheated)
{
	if (!bOverheated) return;
	PlayHaptic(1.f, OverheatHapticSeconds); // a long, strong buzz: this cannon has locked
	if (OverheatSound && Cannon)
	{
		UGameplayStatics::PlaySoundAtLocation(this, OverheatSound, Cannon->GetComponentLocation());
	}
	SetComponentTickEnabled(true);
}

// ---------------------------------------------------------------------------------------------
// Haptics
// ---------------------------------------------------------------------------------------------

void UCannonFeedbackComponent::PlayHaptic(float Amplitude, float Duration)
{
	LastHaptic.Amplitude = FMath::Clamp(Amplitude * HapticStrength, 0.f, 1.f);
	LastHaptic.Frequency = ShotHapticFrequency;
	LastHaptic.Duration = Duration;
	LastHaptic.bLeftHand = bLeftHand;
	if (LastHaptic.Amplitude <= 0.f) return; // haptics off

	HapticRemaining = FMath::Max(HapticRemaining, Duration);
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr)
	{
		Controller->SetHapticsByValue(LastHaptic.Frequency, LastHaptic.Amplitude, bLeftHand ? EControllerHand::Left : EControllerHand::Right);
	}
}

void UCannonFeedbackComponent::StopHaptic()
{
	HapticRemaining = 0.f;
	const APawn* Pawn = Cast<APawn>(GetOwner());
	if (APlayerController* Controller = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr)
	{
		Controller->SetHapticsByValue(0.f, 0.f, bLeftHand ? EControllerHand::Left : EControllerHand::Right);
	}
}

// ---------------------------------------------------------------------------------------------
// Animation
// ---------------------------------------------------------------------------------------------

void UCannonFeedbackComponent::UpdateTracer(FTracer& Tracer) const
{
	if (!Tracer.Mesh) return;
	FVector Tail, Head;
	if (!GetTracerSegment(static_cast<int32>(&Tracer - Tracers.GetData()), Tail, Head))
	{
		Tracer.Mesh->SetVisibility(false);
		return;
	}
	const float Length = FVector::Dist(Tail, Head);
	Tracer.Mesh->SetVisibility(Length > 1.f);
	// The cylinder is 100 cm tall along Z and centred: put it half way and stretch it along the shot.
	Tracer.Mesh->SetWorldLocationAndRotation((Tail + Head) * 0.5f, (Head - Tail).Rotation() + FRotator(-90.f, 0.f, 0.f));
	const float Thickness = TracerRadiusCm * 2.f / 100.f;
	Tracer.Mesh->SetWorldScale3D(FVector(Thickness, Thickness, Length / 100.f));
}

bool UCannonFeedbackComponent::GetTracerSegment(int32 Index, FVector& OutTail, FVector& OutHead) const
{
	if (!Tracers.IsValidIndex(Index)) return false;
	const FTracer& Tracer = Tracers[Index];
	if (Tracer.Age < 0.f || TracerSeconds <= 0.f) return false;
	const float T = FMath::Clamp(Tracer.Age / TracerSeconds, 0.f, 1.f);
	OutHead = FMath::Lerp(Tracer.Start, Tracer.End, FMath::Min(1.f, T * 2.f));
	OutTail = FMath::Lerp(Tracer.Start, Tracer.End, FMath::Max(0.f, T * 2.f - 1.f));
	return true;
}

void UCannonFeedbackComponent::AdvanceFeedback(float DeltaTime)
{
	bool bBusy = false;

	if (RecoilTime >= 0.f && RecoilPivot)
	{
		RecoilTime += DeltaTime;
		const float T = RecoilRecoverSeconds > 0.f ? FMath::Clamp(RecoilTime / RecoilRecoverSeconds, 0.f, 1.f) : 1.f;
		const float Kick = FMath::Square(1.f - T); // instant kick, eased return
		RecoilPivot->SetRelativeLocationAndRotation(FVector(-RecoilKickCm * Kick, 0.f, 0.f), FRotator(RecoilPitchDeg * Kick, 0.f, 0.f));
		if (T >= 1.f) RecoilTime = -1.f; else bBusy = true;
	}

	if (FlashAge >= 0.f)
	{
		FlashAge += DeltaTime;
		const float Duration = MuzzleFlashSeconds * (bReducedFlash ? 0.6f : 1.f);
		if (FlashAge >= Duration)
		{
			FlashAge = -1.f;
			if (MuzzleFlash) MuzzleFlash->SetVisibility(false);
		}
		else bBusy = true;
	}

	for (FTracer& Tracer : Tracers)
	{
		if (Tracer.Age < 0.f) continue;
		Tracer.Age += DeltaTime;
		if (Tracer.Age >= TracerSeconds)
		{
			Tracer.Age = -1.f;
			if (Tracer.Mesh) Tracer.Mesh->SetVisibility(false);
		}
		else
		{
			UpdateTracer(Tracer);
			bBusy = true;
		}
	}

	for (FImpact& Impact : Impacts)
	{
		if (Impact.Age < 0.f) continue;
		Impact.Age += DeltaTime;
		const float Duration = ImpactFlashSeconds * (bReducedFlash ? 0.6f : 1.f);
		if (Impact.Age >= Duration)
		{
			Impact.Age = -1.f;
			if (Impact.Mesh) Impact.Mesh->SetVisibility(false);
		}
		else
		{
			const float Grow = 1.f + Impact.Age / Duration; // flash swells as it fades
			Impact.Mesh->SetWorldScale3D(FVector(Impact.Size * Grow));
			bBusy = true;
		}
	}

	if (HapticRemaining > 0.f)
	{
		HapticRemaining -= DeltaTime;
		if (HapticRemaining <= 0.f) StopHaptic(); else bBusy = true;
	}

	if (!bBusy) SetComponentTickEnabled(false);
}

void UCannonFeedbackComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdvanceFeedback(DeltaTime);
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

FVector UCannonFeedbackComponent::GetRecoilOffset() const
{
	return RecoilPivot ? RecoilPivot->GetRelativeLocation() : FVector::ZeroVector;
}

bool UCannonFeedbackComponent::IsMuzzleFlashVisible() const
{
	return MuzzleFlash && MuzzleFlash->IsVisible();
}

int32 UCannonFeedbackComponent::GetActiveTracerCount() const
{
	int32 Count = 0;
	for (const FTracer& Tracer : Tracers) if (Tracer.Age >= 0.f) ++Count;
	return Count;
}

int32 UCannonFeedbackComponent::GetActiveImpactCount() const
{
	int32 Count = 0;
	for (const FImpact& Impact : Impacts) if (Impact.Age >= 0.f) ++Count;
	return Count;
}
