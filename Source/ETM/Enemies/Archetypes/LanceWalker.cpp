#include "Enemies/Archetypes/LanceWalker.h"
#include "Enemies/Archetypes/ArchetypeSupport.h"
#include "Enemies/Archetypes/EnemyPlaceholderMesh.h"
#include "Weapons/CannonComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"

const FName ALanceWalker::ChamberPart(TEXT("LanceChamber"));

ALanceWalker::ALanceWalker()
{
	MaxHealth = 320.f;
	SpawnDuration = 0.6f;
	StaggerDuration = 1.2f; // the unmistakable mechanical failure of an interrupted lance (bible section 7)
	DeathDuration = 0.8f;

	FEnemyPart ChamberData;
	ChamberData.PartName = ChamberPart;
	ChamberData.MaxHealth = 160.f;      // 2 weak-point shots
	ChamberData.bInterruptOnBreak = true;
	Parts = { ChamberData };

	using namespace ETMPlaceholder;
	// Silhouette: tall and narrow on long legs, with a long lance on top.
	Body = MakePart(this, SceneRoot, TEXT("Body"), EShape::Cube, FVector(60.f, 60.f, 140.f), FVector(0.f, 0.f, 330.f), FRotator::ZeroRotator, true);
	MakePart(this, SceneRoot, TEXT("LegL"), EShape::Cube, FVector(15.f, 15.f, 280.f), FVector(0.f, -45.f, 140.f), FRotator(0.f, 0.f, 10.f), false);
	MakePart(this, SceneRoot, TEXT("LegR"), EShape::Cube, FVector(15.f, 15.f, 280.f), FVector(0.f, 45.f, 140.f), FRotator(0.f, 0.f, -10.f), false);
	MakePart(this, SceneRoot, TEXT("LegB"), EShape::Cube, FVector(15.f, 15.f, 280.f), FVector(-45.f, 0.f, 140.f), FRotator(10.f, 0.f, 0.f), false);
	Chamber = MakePart(this, SceneRoot, TEXT("Chamber"), EShape::Sphere, FVector(45.f), FVector(35.f, 0.f, 380.f), FRotator::ZeroRotator, true);
	Chamber->ComponentTags.Add(ChamberPart);
	Chamber->ComponentTags.Add(ETMTags::WeakPoint);
	Lance = MakePart(this, SceneRoot, TEXT("Lance"), EShape::Cylinder, FVector(12.f, 12.f, 220.f), FVector(110.f, 0.f, 410.f), FRotator(-90.f, 0.f, 0.f), false);

	TelegraphAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("TelegraphAudio"));
	TelegraphAudio->SetupAttachment(SceneRoot);
	TelegraphAudio->bAutoActivate = false;
}

void ALanceWalker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

void ALanceWalker::SimulateStep(float DeltaSeconds)
{
	AdvanceState(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

void ALanceWalker::SetTarget(AActor* InTarget)
{
	Target = InTarget;
	ETMArchetype::SyncInfrastructureTag(this, InTarget);
}

AActor* ALanceWalker::ResolveTarget()
{
	if (!Target.IsValid()) SetTarget(ETMArchetype::FindRig(this));
	return Target.Get();
}

float ALanceWalker::GetTelegraphProgress() const
{
	if (GetState() != EEnemyState::Attacking || ChargeSeconds <= 0.f) return 0.f;
	return FMath::Clamp(1.f - GetAttackTimeRemaining() / ChargeSeconds, 0.f, 1.f);
}

void ALanceWalker::SetChamberExposed(bool bExposed)
{
	bChamberExposed = bExposed;
	if (bExposed)
	{
		for (FEnemyPart& Part : Parts)
		{
			if (Part.PartName == ChamberPart) { Part.Health = Part.MaxHealth; Part.bBroken = false; }
		}
	}
	if (Chamber)
	{
		Chamber->SetVisibility(bExposed);
		Chamber->SetCollisionEnabled(bExposed ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}

float ALanceWalker::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// The chamber only exists while charging; outside that a hit naming it cannot land.
	if (!bChamberExposed && DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const UPrimitiveComponent* Hit = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.GetComponent();
		if (Hit && Hit->ComponentHasTag(ChamberPart)) return 0.f;
	}
	return Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
}

void ALanceWalker::UpdateBehavior(float DeltaSeconds)
{
	if (GetState() == EEnemyState::Attacking)
	{
		const float Progress = GetTelegraphProgress();
		if (Lance) Lance->SetRelativeScale3D(FVector(0.12f + 0.18f * Progress, 0.12f + 0.18f * Progress, 2.2f + 1.3f * Progress)); // lance swells
		if (TelegraphAudio && TelegraphAudio->Sound) TelegraphAudio->SetPitchMultiplier(1.f + Progress);
		return;
	}
	if (GetState() != EEnemyState::Approaching) return;

	AActor* Victim = ResolveTarget();
	if (!Victim) return;

	if (!bPlanted)
	{
		if (!bPlantPointSet)
		{
			// Plant AdvanceDistance from the entrance toward the target. Worked out here, not at spawn,
			// because the director assigns the target just after the pool activates the walker.
			FVector Toward = Victim->GetActorLocation() - GetActorLocation();
			Toward.Z = 0.f;
			PlantPoint = GetActorLocation() + Toward.GetSafeNormal() * FMath::Min(AdvanceDistance, Toward.Size());
			bPlantPointSet = true;
		}
		if (!ETMArchetype::MoveToward(this, PlantPoint, MoveSpeed, DeltaSeconds, 5.f, true)) return;
		ETMArchetype::FaceToward(this, Victim->GetActorLocation());
		PlantTimer += DeltaSeconds; // legs plant before the first charge
		if (PlantTimer < PlantSeconds) return;
		bPlanted = true;
	}

	ETMArchetype::FaceToward(this, Victim->GetActorLocation());
	Cooldown -= DeltaSeconds;
	if (Cooldown <= 0.f && BeginAttack(Victim, StrikeDamage, ChargeSeconds))
	{
		Cooldown = StrikeCooldown;
	}
}

void ALanceWalker::ResolveAttack(AActor* AttackTarget, float Damage)
{
	Super::ResolveAttack(AttackTarget, Damage);
}

void ALanceWalker::HandleStateChanged(EEnemyState NewState, EEnemyState OldState)
{
	if (NewState == EEnemyState::Spawning)
	{
		bPlanted = false;
		bPlantPointSet = false;
		PlantTimer = 0.f;
		Cooldown = 0.f;
	}

	SetChamberExposed(NewState == EEnemyState::Attacking);
	if (NewState == EEnemyState::Attacking)
	{
		if (TelegraphAudio && ChargeSound) { TelegraphAudio->SetSound(ChargeSound); TelegraphAudio->SetPitchMultiplier(1.f); TelegraphAudio->Play(); }
	}
	else
	{
		if (TelegraphAudio) TelegraphAudio->Stop();
		if (Lance) Lance->SetRelativeScale3D(FVector(0.12f, 0.12f, 2.2f));
	}
	if (NewState == EEnemyState::Staggered) Cooldown = StrikeCooldown; // an interrupted lance recharges from scratch
	if (NewState == EEnemyState::Inactive) { Target.Reset(); ETMArchetype::SyncInfrastructureTag(this, nullptr); }
}
