#include "Enemies/Archetypes/CradleBomber.h"
#include "Enemies/Archetypes/ArchetypeSupport.h"
#include "Enemies/Archetypes/EnemyPlaceholderMesh.h"
#include "Weapons/CannonComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"

const FName ACradleBomber::PodPart(TEXT("CradlePod"));

ACradleBomber::ACradleBomber()
{
	MaxHealth = 160.f;
	SpawnDuration = 0.6f;
	StaggerDuration = 0.6f;
	DeathDuration = 0.7f;

	FEnemyPart PodData;
	PodData.PartName = PodPart;
	PodData.MaxHealth = 80.f;             // 1 weak-point shot
	PodData.bInterruptOnBreak = false;
	PodData.BodyDamageOnBreak = 160.f;    // the pod goes off in the cradle and takes the carrier with it
	Parts = { PodData };

	using namespace ETMPlaceholder;
	// Silhouette: a flat ring hanging over a single round pod.
	Carrier = MakePart(this, SceneRoot, TEXT("Carrier"), EShape::Cylinder, FVector(180.f, 180.f, 25.f), FVector::ZeroVector, FRotator::ZeroRotator, true);
	for (int32 Strut = 0; Strut < 3; ++Strut)
	{
		const float Angle = Strut * 120.f;
		const FVector At = FRotator(0.f, Angle, 0.f).RotateVector(FVector(55.f, 0.f, -45.f));
		MakePart(this, SceneRoot, *FString::Printf(TEXT("Strut%d"), Strut), EShape::Cube, FVector(8.f, 8.f, 80.f), At, FRotator(0.f, Angle, 25.f), false);
	}
	Pod = MakePart(this, SceneRoot, TEXT("Pod"), EShape::Sphere, FVector(70.f), FVector(0.f, 0.f, -90.f), FRotator::ZeroRotator, true);
	Pod->ComponentTags.Add(PodPart);
	Pod->ComponentTags.Add(ETMTags::WeakPoint);

	TelegraphAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("TelegraphAudio"));
	TelegraphAudio->SetupAttachment(SceneRoot);
	TelegraphAudio->bAutoActivate = false;
}

void ACradleBomber::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

void ACradleBomber::SimulateStep(float DeltaSeconds)
{
	AdvanceState(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

void ACradleBomber::SetTarget(AActor* InTarget)
{
	Target = InTarget;
	ETMArchetype::SyncInfrastructureTag(this, InTarget);
}

AActor* ACradleBomber::ResolveTarget()
{
	if (!Target.IsValid()) SetTarget(ETMArchetype::FindRig(this));
	return Target.Get();
}

bool ACradleBomber::HasPod() const
{
	return bPodPresent && !IsPartBroken(PodPart);
}

float ACradleBomber::GetTelegraphProgress() const
{
	if (GetState() != EEnemyState::Attacking || WarningSeconds <= 0.f) return 0.f;
	return FMath::Clamp(1.f - GetAttackTimeRemaining() / WarningSeconds, 0.f, 1.f);
}

void ACradleBomber::SetPodPresent(bool bPresent)
{
	bPodPresent = bPresent;
	if (bPresent)
	{
		for (FEnemyPart& Part : Parts)
		{
			if (Part.PartName == PodPart) { Part.Health = Part.MaxHealth; Part.bBroken = false; }
		}
	}
	if (Pod)
	{
		Pod->SetVisibility(bPresent);
		Pod->SetCollisionEnabled(bPresent ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		Pod->SetRelativeScale3D(FVector(0.7f));
	}
}

void ACradleBomber::UpdateBehavior(float DeltaSeconds)
{
	if (GetState() == EEnemyState::Attacking)
	{
		const float Progress = GetTelegraphProgress();
		if (Pod) Pod->SetRelativeScale3D(FVector(0.7f + 0.25f * Progress)); // the pod swells as it glows
		if (TelegraphAudio && TelegraphAudio->Sound) TelegraphAudio->SetPitchMultiplier(GetTelegraphTonePitch());
		return;
	}
	if (GetState() != EEnemyState::Approaching) return;

	AActor* Victim = ResolveTarget();
	if (!Victim) return;

	// Hover directly above the target.
	const FVector Hover = Victim->GetActorLocation() + FVector(0.f, 0.f, HoverHeight);
	const bool bArrived = ETMArchetype::MoveToward(this, Hover, MoveSpeed, DeltaSeconds, 20.f, false);

	if (!bPodPresent)
	{
		RearmTimer -= DeltaSeconds;
		if (RearmTimer <= 0.f) SetPodPresent(true);
		return;
	}
	if (bArrived)
	{
		BeginAttack(Victim, ImpactDamage, WarningSeconds);
	}
}

void ACradleBomber::ResolveAttack(AActor* AttackTarget, float Damage)
{
	Super::ResolveAttack(AttackTarget, Damage); // released: the pod hits its target at once
	SetPodPresent(false);
	RearmTimer = RearmSeconds;
}

void ACradleBomber::HandleStateChanged(EEnemyState NewState, EEnemyState OldState)
{
	if (NewState == EEnemyState::Spawning)
	{
		SetPodPresent(true);
		RearmTimer = 0.f;
	}
	if (NewState == EEnemyState::Attacking)
	{
		if (TelegraphAudio && WarningSound) { TelegraphAudio->SetSound(WarningSound); TelegraphAudio->SetPitchMultiplier(1.5f); TelegraphAudio->Play(); }
	}
	else
	{
		if (TelegraphAudio) TelegraphAudio->Stop();
		if (Pod && bPodPresent) Pod->SetRelativeScale3D(FVector(0.7f));
	}
	if (NewState == EEnemyState::Inactive) { Target.Reset(); ETMArchetype::SyncInfrastructureTag(this, nullptr); }
}
