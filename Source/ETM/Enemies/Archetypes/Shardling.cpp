#include "Enemies/Archetypes/Shardling.h"
#include "Enemies/Archetypes/ArchetypeSupport.h"
#include "Enemies/Archetypes/EnemyPlaceholderMesh.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Objectives/MissionTypes.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"

AShardling::AShardling()
{
	MaxHealth = 80.f;
	SpawnDuration = 0.5f;
	StaggerDuration = 0.6f;
	DeathDuration = 0.5f;
	Tags.Add(ETMObjectiveTags::InfrastructureAttacker);

	using namespace ETMPlaceholder;
	// Low triangular profile: a flattened cone pointing forward, four short legs, two cutting limbs.
	Body = MakePart(this, SceneRoot, TEXT("Body"), EShape::Cone, FVector(60.f, 60.f, 90.f), FVector(0.f, 0.f, 30.f), FRotator(-90.f, 0.f, 0.f), true);
	const FVector LegSize(8.f, 8.f, 30.f);
	MakePart(this, SceneRoot, TEXT("LegFL"), EShape::Cube, LegSize, FVector(25.f, -25.f, 12.f), FRotator(0.f, 0.f, 20.f), false);
	MakePart(this, SceneRoot, TEXT("LegFR"), EShape::Cube, LegSize, FVector(25.f, 25.f, 12.f), FRotator(0.f, 0.f, -20.f), false);
	MakePart(this, SceneRoot, TEXT("LegBL"), EShape::Cube, LegSize, FVector(-25.f, -25.f, 12.f), FRotator(0.f, 0.f, 20.f), false);
	MakePart(this, SceneRoot, TEXT("LegBR"), EShape::Cube, LegSize, FVector(-25.f, 25.f, 12.f), FRotator(0.f, 0.f, -20.f), false);

	LeftLimbPivot = CreateDefaultSubobject<USceneComponent>(TEXT("LeftLimbPivot"));
	LeftLimbPivot->SetupAttachment(SceneRoot);
	LeftLimbPivot->SetRelativeLocation(FVector(35.f, -18.f, 35.f));
	RightLimbPivot = CreateDefaultSubobject<USceneComponent>(TEXT("RightLimbPivot"));
	RightLimbPivot->SetupAttachment(SceneRoot);
	RightLimbPivot->SetRelativeLocation(FVector(35.f, 18.f, 35.f));
	const FVector BladeSize(45.f, 4.f, 10.f);
	MakePart(this, LeftLimbPivot, TEXT("LeftBlade"), EShape::Cube, BladeSize, FVector(22.f, 0.f, 0.f), FRotator::ZeroRotator, false);
	MakePart(this, RightLimbPivot, TEXT("RightBlade"), EShape::Cube, BladeSize, FVector(22.f, 0.f, 0.f), FRotator::ZeroRotator, false);

	TelegraphAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("TelegraphAudio"));
	TelegraphAudio->SetupAttachment(SceneRoot);
	TelegraphAudio->bAutoActivate = false;
}

void AShardling::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

void AShardling::SimulateStep(float DeltaSeconds)
{
	AdvanceState(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

AMissionObjective* AShardling::ResolveTarget()
{
	if (!Target.IsValid())
	{
		if (const UMissionStateSubsystem* Mission = GetWorld() ? GetWorld()->GetSubsystem<UMissionStateSubsystem>() : nullptr)
		{
			Target = Mission->GetObjective();
		}
	}
	return Target.Get();
}

float AShardling::GetTelegraphProgress() const
{
	if (GetState() != EEnemyState::Attacking || LimbRaiseSeconds <= 0.f) return 0.f;
	return FMath::Clamp(1.f - GetAttackTimeRemaining() / LimbRaiseSeconds, 0.f, 1.f);
}

void AShardling::SetLimbPose(float Progress)
{
	const FRotator Pose(LimbRaisedPitch * Progress, 0.f, 0.f);
	if (LeftLimbPivot) LeftLimbPivot->SetRelativeRotation(Pose);
	if (RightLimbPivot) RightLimbPivot->SetRelativeRotation(Pose);
}

void AShardling::UpdateBehavior(float DeltaSeconds)
{
	switch (GetState())
	{
	case EEnemyState::Approaching:
	{
		AMissionObjective* Objective = ResolveTarget();
		if (!Objective) return;

		// Scrape forward along the ground, over low cover rather than through it (T-0020).
		const FVector Here = GetActorLocation();
		FVector ToTarget = Objective->GetActorLocation() - Here;
		ToTarget.Z = 0.f;
		const float Distance = ToTarget.Size();
		if (Distance <= AttackRange)
		{
			BeginAttack(Objective, ObjectiveDamage, LimbRaiseSeconds);
			return;
		}
		const FVector Direction = ToTarget / Distance;
		const float Step = FMath::Min(MoveSpeed * DeltaSeconds, Distance - AttackRange);
		SetActorLocationAndRotation(ETMArchetype::FollowGround(this, Here, Here + Direction * Step), Direction.Rotation());
		break;
	}
	case EEnemyState::Attacking:
		SetLimbPose(GetTelegraphProgress());
		if (TelegraphAudio && TelegraphAudio->Sound)
		{
			TelegraphAudio->SetPitchMultiplier(1.f + 0.5f * GetTelegraphProgress());
		}
		break;
	default:
		break;
	}
}

void AShardling::HandleStateChanged(EEnemyState NewState, EEnemyState OldState)
{
	if (NewState == EEnemyState::Attacking)
	{
		SetLimbPose(0.f);
		if (TelegraphAudio && TelegraphSound)
		{
			TelegraphAudio->SetSound(TelegraphSound);
			TelegraphAudio->Play();
		}
	}
	else
	{
		if (TelegraphAudio) TelegraphAudio->Stop();
		if (NewState != EEnemyState::Dying) SetLimbPose(0.f); // a dying Shardling keeps its pose for the wreck
	}
	if (NewState == EEnemyState::Inactive)
	{
		Target.Reset(); // the director assigns a fresh target on reuse
	}
}

void AShardling::ResolveAttack(AActor* AttackTarget, float Damage)
{
	Super::ResolveAttack(AttackTarget, Damage);
	Kill(); // cuts once, then destroys itself (bible section 7)
}
