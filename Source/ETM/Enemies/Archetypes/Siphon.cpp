#include "Enemies/Archetypes/Siphon.h"
#include "Enemies/Archetypes/ArchetypeSupport.h"
#include "Enemies/Archetypes/EnemyPlaceholderMesh.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"

ASiphon::ASiphon()
{
	MaxHealth = 200.f;
	SpawnDuration = 0.5f;
	StaggerDuration = 0.6f;
	DeathDuration = 0.6f;
	Tags.Add(ETMObjectiveTags::InfrastructureAttacker);

	using namespace ETMPlaceholder;
	// Silhouette: a hunched, segmented parasite with a long cable.
	Body = MakePart(this, SceneRoot, TEXT("Body"), EShape::Sphere, FVector(90.f, 70.f, 60.f), FVector(0.f, 0.f, 35.f), FRotator::ZeroRotator, true);
	MakePart(this, SceneRoot, TEXT("Segment1"), EShape::Sphere, FVector(60.f, 50.f, 45.f), FVector(-55.f, 0.f, 28.f), FRotator::ZeroRotator, false);
	MakePart(this, SceneRoot, TEXT("Segment2"), EShape::Sphere, FVector(40.f, 35.f, 30.f), FVector(-95.f, 0.f, 20.f), FRotator::ZeroRotator, false);
	Tether = MakePart(this, SceneRoot, TEXT("Tether"), EShape::Cylinder, FVector(6.f, 6.f, 100.f), FVector::ZeroVector, FRotator::ZeroRotator, false);
	Tether->SetVisibility(false);

	DrainAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("DrainAudio"));
	DrainAudio->SetupAttachment(SceneRoot);
	DrainAudio->bAutoActivate = false;
}

void ASiphon::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

void ASiphon::SimulateStep(float DeltaSeconds)
{
	AdvanceState(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

bool ASiphon::IsTetherVisible() const
{
	return Tether && Tether->IsVisible();
}

void ASiphon::UpdateTether()
{
	const AMissionObjective* Objective = Target.Get();
	if (!Tether || !Objective) return;
	const FVector From = GetActorLocation() + FVector(40.f, 0.f, 40.f);
	const FVector To = Objective->GetActorLocation();
	const float Length = FVector::Dist(From, To);
	Tether->SetWorldLocationAndRotation((From + To) * 0.5f, (To - From).Rotation() + FRotator(-90.f, 0.f, 0.f));
	Tether->SetWorldScale3D(FVector(0.06f, 0.06f, Length / 100.f));
}

void ASiphon::UpdateBehavior(float DeltaSeconds)
{
	const EEnemyState Now = GetState();
	if (Now == EEnemyState::Attacking)
	{
		UpdateTether(); // latch warning: the cable is out before any drain
		return;
	}
	if (Now != EEnemyState::Approaching) return;

	if (!Target.IsValid()) Target = ETMArchetype::FindObjective(this);
	AMissionObjective* Objective = Target.Get();
	if (!Objective) return;

	if (bDraining)
	{
		UpdateTether();
		// Sustained drain until destroyed. DealDamageTo refuses once the Siphon is staggered or dying.
		PulseTimer += DeltaSeconds;
		while (PulseTimer >= DrainPulseSeconds)
		{
			PulseTimer -= DrainPulseSeconds;
			Drained += DealDamageTo(Objective, DrainPerSecond * DrainPulseSeconds);
		}
		return;
	}

	if (ETMArchetype::MoveToward(this, Objective->GetActorLocation(), MoveSpeed, DeltaSeconds, LatchRange, true))
	{
		BeginAttack(Objective, 0.f, LatchWarningSeconds); // the latch: a warning that deals nothing itself
	}
}

void ASiphon::ResolveAttack(AActor* AttackTarget, float Damage)
{
	Super::ResolveAttack(AttackTarget, Damage);
	bDraining = true; // latched: drain starts on the next pulse
	PulseTimer = 0.f;
	if (DrainAudio && DrainSound) { DrainAudio->SetSound(DrainSound); DrainAudio->Play(); }
}

void ASiphon::HandleStateChanged(EEnemyState NewState, EEnemyState OldState)
{
	if (NewState == EEnemyState::Spawning)
	{
		bDraining = false;
		PulseTimer = 0.f;
		Drained = 0.f;
	}
	if (NewState == EEnemyState::Staggered)
	{
		bDraining = false; // a staggered Siphon lets go and has to latch again
	}
	const bool bTether = NewState == EEnemyState::Attacking || (NewState == EEnemyState::Approaching && bDraining);
	if (Tether) Tether->SetVisibility(bTether);
	if (!bTether && DrainAudio) DrainAudio->Stop();
	if (NewState == EEnemyState::Inactive) Target.Reset();
}
