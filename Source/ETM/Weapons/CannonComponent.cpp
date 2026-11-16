#include "Weapons/CannonComponent.h"
#include "ETM.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "CollisionQueryParams.h"

UCannonComponent::UCannonComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UCannonComponent::BeginPlay()
{
	Super::BeginPlay();
	HeatTuning = Tuning.ToHeatTuning();
	DesiredAimWorld = GetForwardVector();
}

void UCannonComponent::SetAimDirection(const FVector& WorldDirection)
{
	if (!WorldDirection.IsNearlyZero())
	{
		DesiredAimWorld = WorldDirection.GetSafeNormal();
	}
}

void UCannonComponent::UpdateServo(float DeltaTime)
{
	// Controller direction drives a servo target; the barrel obeys mechanical limits (bible section 4).
	const USceneComponent* Mount = GetAttachParent();
	const FTransform MountXf = Mount ? Mount->GetComponentTransform() : FTransform::Identity;
	const FVector LocalDir = MountXf.InverseTransformVectorNoScale(DesiredAimWorld);
	FRotator Desired = LocalDir.Rotation();
	Desired.Yaw = FMath::Clamp(FRotator::NormalizeAxis(Desired.Yaw), -Tuning.YawLimitDeg, Tuning.YawLimitDeg);
	Desired.Pitch = FMath::Clamp(FRotator::NormalizeAxis(Desired.Pitch), Tuning.PitchMinDeg, Tuning.PitchMaxDeg);
	Desired.Roll = 0.f;

	const FRotator Current = GetRelativeRotation();
	SetRelativeRotation(FMath::RInterpConstantTo(Current, Desired, DeltaTime, Tuning.ServoSpeedDegPerSec));
}

void UCannonComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	UpdateServo(DeltaTime);

	// Sub-step so cadence is frame-rate independent (72 Hz frame = 13.9 ms vs 250 ms shot interval).
	const float MaxStep = 1.0f / 240.0f;
	float Remaining = DeltaTime;
	const float HeatBefore = Heat.Heat;
	while (Remaining > KINDA_SMALL_NUMBER)
	{
		const float Dt = FMath::Min(Remaining, MaxStep);
		if (ETM::StepHeat(Heat, HeatTuning, Dt, bTriggerHeld, bVentHeld))
		{
			FireShot();
		}
		Remaining -= Dt;
	}

	if (!FMath::IsNearlyEqual(HeatBefore, Heat.Heat) || bWasOverheated != Heat.bOverheated)
	{
		bWasOverheated = Heat.bOverheated;
		OnHeatChanged.Broadcast(GetHeatNormalized(), Heat.bOverheated);
	}
}

float UCannonComponent::ResolveMultiplier(const FHitResult& Hit) const
{
	const UPrimitiveComponent* C = Hit.GetComponent();
	if (!C) return 1.0f;
	if (C->ComponentHasTag(ETMTags::Immune)) return 0.0f;
	if (C->ComponentHasTag(ETMTags::WeakPoint)) return Tuning.WeakPointMultiplier;
	if (C->ComponentHasTag(ETMTags::Armor)) return Tuning.ArmorMultiplier;
	return 1.0f;
}

void UCannonComponent::FireShot()
{
	UWorld* World = GetWorld();
	if (!World) return;

	const FVector Dir = GetForwardVector();
	const FVector Muzzle = GetComponentTransform().TransformPosition(MuzzleOffset);
	const FVector End = Muzzle + Dir * Tuning.MaxRangeCm;

	// Obstruction check from the barrel root to the muzzle: never shoot through the frame or walls.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(ETMCannonShot), /*bTraceComplex*/ false, GetOwner());
	FHitResult Block;
	if (World->LineTraceSingleByChannel(Block, GetComponentLocation(), Muzzle, TraceChannel, Params))
	{
		OnFired.Broadcast(Muzzle, Block.ImpactPoint, false);
		return;
	}

	// Muzzle-origin hitscan; the tracer is cosmetic and handled by listeners (bible section 5).
	FHitResult Hit;
	const bool bHit = World->LineTraceSingleByChannel(Hit, Muzzle, End, TraceChannel, Params);
	if (bHit && Hit.GetActor())
	{
		const float Mult = ResolveMultiplier(Hit);
		if (Mult > 0.f)
		{
			UGameplayStatics::ApplyPointDamage(Hit.GetActor(), Tuning.Damage * Mult, Dir, Hit,
				GetOwner() ? GetOwner()->GetInstigatorController() : nullptr, GetOwner(), nullptr);
		}
	}
	OnFired.Broadcast(Muzzle, bHit ? Hit.ImpactPoint : End, bHit);
}
