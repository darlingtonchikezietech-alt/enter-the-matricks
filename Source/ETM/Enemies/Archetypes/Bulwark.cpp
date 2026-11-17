#include "Enemies/Archetypes/Bulwark.h"
#include "Enemies/Archetypes/ArchetypeSupport.h"
#include "Enemies/Archetypes/EnemyPlaceholderMesh.h"
#include "Weapons/CannonComponent.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"

const FName ABulwark::ShieldPart(TEXT("BulwarkShield"));
const FName ABulwark::LeftNodePart(TEXT("BulwarkNodeL"));
const FName ABulwark::RightNodePart(TEXT("BulwarkNodeR"));

ABulwark::ABulwark()
{
	MaxHealth = 480.f;
	SpawnDuration = 0.8f;
	StaggerDuration = 0.8f;
	DeathDuration = 0.8f;
	Tags.Add(ETMObjectiveTags::InfrastructureAttacker); // it slams the objective

	FEnemyPart ShieldData;
	ShieldData.PartName = ShieldPart;
	ShieldData.MaxHealth = 1.0e6f;     // never breaks by damage: it drops when a node breaks
	ShieldData.DamageScale = 0.1f;     // P-02: shield takes x0.1 ...
	ShieldData.bPassDamageToBody = true; // ... of which all reaches the body
	ShieldData.bInterruptOnBreak = false;
	FEnemyPart NodeData;
	NodeData.MaxHealth = 80.f;
	NodeData.bInterruptOnBreak = true; // a broken node also staggers it, cancelling a slam wind-up
	NodeData.PartName = LeftNodePart;
	FEnemyPart RightNodeData = NodeData;
	RightNodeData.PartName = RightNodePart;
	Parts = { ShieldData, NodeData, RightNodeData };

	using namespace ETMPlaceholder;
	// Silhouette: a wide, tall slab of shield in front of a narrow upright body, a node on each side.
	Body = MakePart(this, SceneRoot, TEXT("Body"), EShape::Cylinder, FVector(60.f, 60.f, 200.f), FVector(0.f, 0.f, 110.f), FRotator::ZeroRotator, true);
	ShieldPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ShieldPivot"));
	ShieldPivot->SetupAttachment(SceneRoot);
	ShieldPivot->SetRelativeLocation(FVector(70.f, 0.f, 20.f));
	Shield = MakePart(this, ShieldPivot, TEXT("Shield"), EShape::Cube, FVector(25.f, 260.f, 220.f), FVector(0.f, 0.f, 110.f), FRotator::ZeroRotator, true);
	Shield->ComponentTags.Add(ShieldPart);
	LeftNode = MakePart(this, SceneRoot, TEXT("LeftNode"), EShape::Sphere, FVector(40.f), FVector(40.f, -160.f, 140.f), FRotator::ZeroRotator, true);
	LeftNode->ComponentTags.Add(LeftNodePart);
	LeftNode->ComponentTags.Add(ETMTags::WeakPoint);
	RightNode = MakePart(this, SceneRoot, TEXT("RightNode"), EShape::Sphere, FVector(40.f), FVector(40.f, 160.f, 140.f), FRotator::ZeroRotator, true);
	RightNode->ComponentTags.Add(RightNodePart);
	RightNode->ComponentTags.Add(ETMTags::WeakPoint);

	TelegraphAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("TelegraphAudio"));
	TelegraphAudio->SetupAttachment(SceneRoot);
	TelegraphAudio->bAutoActivate = false;
}

void ABulwark::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

void ABulwark::SimulateStep(float DeltaSeconds)
{
	AdvanceState(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

float ABulwark::GetTelegraphProgress() const
{
	if (GetState() != EEnemyState::Attacking || SlamWindUpSeconds <= 0.f) return 0.f;
	return FMath::Clamp(1.f - GetAttackTimeRemaining() / SlamWindUpSeconds, 0.f, 1.f);
}

void ABulwark::SetShieldUp(bool bUp)
{
	bShieldDropped = !bUp;
	for (FEnemyPart& Part : Parts)
	{
		if (Part.PartName == ShieldPart) Part.bBroken = !bUp; // a dropped shield no longer takes hits
	}
	if (Shield)
	{
		Shield->SetVisibility(bUp);
		Shield->SetCollisionEnabled(bUp ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
	// While the shield is up only the shield and the nodes can be hit; the body opens when it drops.
	if (Body) Body->SetCollisionEnabled(bUp ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryOnly);
}

void ABulwark::HandlePartBroken(const FEnemyPart& Part)
{
	if (Part.PartName == LeftNodePart || Part.PartName == RightNodePart)
	{
		SetShieldUp(false); // either node drops the shield (bible section 7)
	}
}

void ABulwark::UpdateBehavior(float DeltaSeconds)
{
	if (GetState() == EEnemyState::Attacking)
	{
		if (ShieldPivot) ShieldPivot->SetRelativeRotation(FRotator(35.f * GetTelegraphProgress(), 0.f, 0.f)); // shield heaves up
		return;
	}
	if (GetState() != EEnemyState::Approaching) return;

	if (!Target.IsValid()) Target = ETMArchetype::FindObjective(this);
	AMissionObjective* Objective = Target.Get();
	if (!Objective) return;

	if (!ETMArchetype::MoveToward(this, Objective->GetActorLocation(), MoveSpeed, DeltaSeconds, ContactRange, true)) return;

	// In contact: slam every SlamCycleSeconds (wind-up included).
	SlamCooldown -= DeltaSeconds;
	if (SlamCooldown <= 0.f && BeginAttack(Objective, SlamDamage, SlamWindUpSeconds))
	{
		SlamCooldown = SlamCycleSeconds - SlamWindUpSeconds;
	}
}

void ABulwark::ResolveAttack(AActor* AttackTarget, float Damage)
{
	Super::ResolveAttack(AttackTarget, Damage);
}

void ABulwark::HandleStateChanged(EEnemyState NewState, EEnemyState OldState)
{
	if (NewState == EEnemyState::Spawning)
	{
		SetShieldUp(true);
		SlamCooldown = 0.f;
	}
	if (NewState == EEnemyState::Attacking)
	{
		if (TelegraphAudio && SlamWindUpSound) { TelegraphAudio->SetSound(SlamWindUpSound); TelegraphAudio->Play(); }
	}
	else
	{
		if (TelegraphAudio) TelegraphAudio->Stop();
		if (ShieldPivot && NewState != EEnemyState::Dying) ShieldPivot->SetRelativeRotation(FRotator::ZeroRotator);
	}
	if (NewState == EEnemyState::Inactive) Target.Reset();
}
