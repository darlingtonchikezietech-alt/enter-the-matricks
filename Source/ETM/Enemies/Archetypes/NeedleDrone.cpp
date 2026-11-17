#include "Enemies/Archetypes/NeedleDrone.h"
#include "Enemies/Archetypes/EnemyPlaceholderMesh.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

ANeedleDrone::ANeedleDrone()
{
	MaxHealth = 120.f;
	SpawnDuration = 0.5f;
	StaggerDuration = 0.8f;
	DeathDuration = 0.6f;

	using namespace ETMPlaceholder;
	// Floating diamond (a cube stood on its corner) with two lateral fins.
	Body = MakePart(this, SceneRoot, TEXT("Body"), EShape::Cube, FVector(55.f, 55.f, 55.f), FVector::ZeroVector, FRotator(45.f, 0.f, 45.f), true);
	MakePart(this, SceneRoot, TEXT("LeftFin"), EShape::Cube, FVector(40.f, 50.f, 4.f), FVector(-5.f, -55.f, 0.f), FRotator(0.f, 0.f, -15.f), false);
	MakePart(this, SceneRoot, TEXT("RightFin"), EShape::Cube, FVector(40.f, 50.f, 4.f), FVector(-5.f, 55.f, 0.f), FRotator(0.f, 0.f, 15.f), false);

	// Unit-length cylinder along +X: scaled to the distance at runtime. Cosmetic only, so shots pass through it.
	AimLine = MakePart(this, SceneRoot, TEXT("AimLine"), EShape::Cylinder, FVector(2.f, 2.f, 100.f), FVector::ZeroVector, FRotator(-90.f, 0.f, 0.f), false);
	AimLine->SetVisibility(false);

	TelegraphAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("TelegraphAudio"));
	TelegraphAudio->SetupAttachment(SceneRoot);
	TelegraphAudio->bAutoActivate = false;
}

void ANeedleDrone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

void ANeedleDrone::SimulateStep(float DeltaSeconds)
{
	AdvanceState(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

AActor* ANeedleDrone::ResolveTarget()
{
	if (!Target.IsValid())
	{
		Target = UGameplayStatics::GetPlayerPawn(this, 0);
	}
	return Target.Get();
}

float ANeedleDrone::GetTelegraphProgress() const
{
	if (GetState() != EEnemyState::Attacking || TelegraphSeconds <= 0.f) return 0.f;
	return FMath::Clamp(1.f - GetAttackTimeRemaining() / TelegraphSeconds, 0.f, 1.f);
}

bool ANeedleDrone::IsAimLineVisible() const
{
	return AimLine && AimLine->IsVisible();
}

void ANeedleDrone::UpdateAimLine()
{
	const AActor* Rig = Target.Get();
	if (!AimLine || !Rig) return;
	const FVector From = GetActorLocation();
	const FVector To = Rig->GetActorLocation();
	const float Length = FVector::Dist(From, To);
	// The cylinder is centred on its origin, so place it half way and stretch it to the full length.
	AimLine->SetWorldLocationAndRotation((From + To) * 0.5f, (To - From).Rotation() + FRotator(-90.f, 0.f, 0.f));
	AimLine->SetWorldScale3D(FVector(0.02f, 0.02f, Length / 100.f));
}

void ANeedleDrone::UpdateBehavior(float DeltaSeconds)
{
	switch (GetState())
	{
	case EEnemyState::Approaching:
	{
		CooldownRemaining = FMath::Max(0.f, CooldownRemaining - DeltaSeconds);

		const FVector Here = GetActorLocation();
		const FVector ToHover = HoverLocation - Here;
		const float Distance = ToHover.Size();
		if (Distance > ArriveTolerance)
		{
			SetActorLocation(Here + ToHover.GetSafeNormal() * FMath::Min(MoveSpeed * DeltaSeconds, Distance));
		}

		AActor* Rig = ResolveTarget();
		if (!Rig) return;
		SetActorRotation((Rig->GetActorLocation() - GetActorLocation()).Rotation());
		if (Distance <= ArriveTolerance && CooldownRemaining <= 0.f)
		{
			BeginAttack(Rig, BurstDamage, TelegraphSeconds);
		}
		break;
	}
	case EEnemyState::Attacking:
		UpdateAimLine();
		if (TelegraphAudio && TelegraphAudio->Sound)
		{
			TelegraphAudio->SetPitchMultiplier(GetTelegraphTonePitch());
		}
		break;
	default:
		break;
	}
}

void ANeedleDrone::HandleStateChanged(EEnemyState NewState, EEnemyState OldState)
{
	const bool bAttacking = NewState == EEnemyState::Attacking;
	if (AimLine) AimLine->SetVisibility(bAttacking);

	if (bAttacking)
	{
		UpdateAimLine();
		if (TelegraphAudio && TelegraphSound)
		{
			TelegraphAudio->SetSound(TelegraphSound);
			TelegraphAudio->SetPitchMultiplier(1.f);
			TelegraphAudio->Play();
		}
	}
	else if (TelegraphAudio)
	{
		TelegraphAudio->Stop();
	}

	if (NewState == EEnemyState::Spawning)
	{
		CooldownRemaining = 0.f;
		if (!bHoverAssigned)
		{
			HoverLocation = GetActorLocation() + FVector(0.f, 0.f, HoverHeight);
		}
	}
	else if (NewState == EEnemyState::Staggered)
	{
		CooldownRemaining = AttackCooldown; // an interrupted drone does not re-fire the moment it recovers
	}
	else if (NewState == EEnemyState::Inactive)
	{
		Target.Reset();
		bHoverAssigned = false;
	}
}

void ANeedleDrone::ResolveAttack(AActor* AttackTarget, float Damage)
{
	Super::ResolveAttack(AttackTarget, Damage);
	CooldownRemaining = AttackCooldown;
}
