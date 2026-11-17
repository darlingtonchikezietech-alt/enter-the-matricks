#include "Weapons/CannonComponent.h"
#include "Weapons/CannonAimModel.h"
#include "ETM.h"
#include "Engine/World.h"
#include "Components/PrimitiveComponent.h"
#include "GameFramework/Actor.h"
#include "Kismet/GameplayStatics.h"
#include "CollisionQueryParams.h"

UCannonComponent::UCannonComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UCannonComponent::OnRegister()
{
	Super::OnRegister();
	DesiredAimWorld = GetForwardVector(); // hold the rest pose until a controller direction arrives
}

void UCannonComponent::SetAimDirection(const FVector& WorldDirection)
{
	if (!WorldDirection.IsNearlyZero())
	{
		DesiredAimWorld = WorldDirection.GetSafeNormal();
	}
}

void UCannonComponent::ResetHeat()
{
	Heat = ETM::FHeatState();
	if (bWasOverheated)
	{
		bWasOverheated = false;
		OnOverheatChanged.Broadcast(false);
	}
	OnHeatChanged.Broadcast(0.f, false);
}

void UCannonComponent::UpdateServo(float DeltaTime)
{
	// Controller direction drives a servo target; the barrel obeys mechanical limits (bible section 4).
	const USceneComponent* Mount = GetAttachParent();
	const FTransform MountXf = Mount ? Mount->GetComponentTransform() : FTransform::Identity;
	const FVector LocalDir = MountXf.InverseTransformVectorNoScale(DesiredAimWorld);

	ETMCannon::FAimLimits Limits;
	Limits.YawLimitDeg = Tuning.YawLimitDeg;
	Limits.PitchMinDeg = Tuning.PitchMinDeg;
	Limits.PitchMaxDeg = Tuning.PitchMaxDeg;

	const FRotator Target = ETMCannon::ClampAim(LocalDir, Limits, bAimClamped);
	SetRelativeRotation(ETMCannon::StepServo(GetRelativeRotation(), Target, DeltaTime, Tuning.ServoSpeedDegPerSec));
}

void UCannonComponent::AdvanceCannon(float DeltaTime)
{
	UpdateServo(DeltaTime);

	// Sub-step so cadence is frame-rate independent (72 Hz frame = 13.9 ms vs 250 ms shot interval).
	const ETM::FHeatTuning HeatTuning = Tuning.ToHeatTuning();
	const float MaxStep = 1.0f / 240.0f;
	const float HeatBefore = Heat.Heat;
	float Remaining = DeltaTime;
	while (Remaining > KINDA_SMALL_NUMBER)
	{
		const float Dt = FMath::Min(Remaining, MaxStep);
		if (ETM::StepHeat(Heat, HeatTuning, Dt, bTriggerHeld, bVentHeld))
		{
			FireShot();
		}
		Remaining -= Dt;
	}

	if (bWasOverheated != Heat.bOverheated)
	{
		bWasOverheated = Heat.bOverheated;
		OnOverheatChanged.Broadcast(Heat.bOverheated);
		OnHeatChanged.Broadcast(GetHeatNormalized(), Heat.bOverheated);
	}
	else if (!FMath::IsNearlyEqual(HeatBefore, Heat.Heat))
	{
		OnHeatChanged.Broadcast(GetHeatNormalized(), Heat.bOverheated);
	}

	const bool bVentingNow = IsVenting();
	if (bWasVenting != bVentingNow)
	{
		bWasVenting = bVentingNow;
		OnVentingChanged.Broadcast(bVentingNow);
	}
}

void UCannonComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdvanceCannon(DeltaTime);
}

float UCannonComponent::ResolveMultiplier(const UPrimitiveComponent* HitComponent, const FCannonTuning& InTuning)
{
	if (!HitComponent) return 1.0f;
	if (HitComponent->ComponentHasTag(ETMTags::Immune)) return 0.0f;
	if (HitComponent->ComponentHasTag(ETMTags::WeakPoint)) return InTuning.WeakPointMultiplier;
	if (HitComponent->ComponentHasTag(ETMTags::Armor)) return InTuning.ArmorMultiplier;
	return 1.0f;
}

void UCannonComponent::TraceShot(FCannonShot& OutShot, FHitResult& OutHit) const
{
	OutShot = FCannonShot();
	OutHit = FHitResult();

	const FVector Root = GetComponentLocation();
	const FVector Dir = GetForwardVector();
	OutShot.MuzzleLocation = GetMuzzleLocation();
	OutShot.EndLocation = OutShot.MuzzleLocation + Dir * Tuning.MaxRangeCm;

	const UWorld* World = GetWorld();
	if (!World) return;

	// The rig is NOT ignored: its frame must stop shots. Only this cannon's own barrel parts are ignored.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ETMCannonShot), /*bTraceComplex*/ false);
	TArray<USceneComponent*> BarrelParts;
	GetChildrenComponents(/*bIncludeAllDescendants*/ true, BarrelParts);
	for (const USceneComponent* Part : BarrelParts)
	{
		if (const UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Part))
		{
			Params.AddIgnoredComponent(Primitive);
		}
	}
	// A mount collider that surrounds the barrel root must not block every shot.
	Params.bFindInitialOverlaps = false;

	// 1. Barrel root to muzzle: the muzzle is inside or behind the frame or a wall.
	FHitResult Block;
	if (World->LineTraceSingleByChannel(Block, Root, OutShot.MuzzleLocation, TraceChannel, Params))
	{
		OutShot.bBlocked = true;
		OutShot.EndLocation = Block.ImpactPoint;
		return;
	}

	// 2. Muzzle-origin hitscan; the tracer is cosmetic and handled by listeners (bible section 5).
	if (World->LineTraceSingleByChannel(OutHit, OutShot.MuzzleLocation, OutShot.EndLocation, TraceChannel, Params))
	{
		OutShot.EndLocation = OutHit.ImpactPoint;
		if (OutHit.GetActor() == GetOwner())
		{
			OutShot.bBlocked = true; // struck our own frame or the other cannon
			return;
		}
		OutShot.bHit = true;
		OutShot.HitActor = OutHit.GetActor();
		OutShot.Multiplier = ResolveMultiplier(OutHit.GetComponent(), Tuning);
		OutShot.bWeakPoint = OutHit.GetComponent() && OutHit.GetComponent()->ComponentHasTag(ETMTags::WeakPoint);
	}
}

bool UCannonComponent::GetBarrelImpactPoint(FVector& OutPoint) const
{
	FCannonShot Shot;
	FHitResult Hit;
	TraceShot(Shot, Hit);
	OutPoint = Shot.EndLocation;
	return Shot.bHit || Shot.bBlocked;
}

void UCannonComponent::FireShot()
{
	FHitResult Hit;
	TraceShot(LastShot, Hit);
	++ShotsFired;

	if (LastShot.bHit && LastShot.HitActor && LastShot.Multiplier > 0.f)
	{
		AActor* Owner = GetOwner();
		LastShot.Damage = Tuning.Damage * LastShot.Multiplier; // raw HP convention: multipliers apply on hit (P-02)
		UGameplayStatics::ApplyPointDamage(LastShot.HitActor, LastShot.Damage, GetForwardVector(), Hit,
			Owner ? Owner->GetInstigatorController() : nullptr, Owner, nullptr);
	}
	OnFired.Broadcast(LastShot);
}
