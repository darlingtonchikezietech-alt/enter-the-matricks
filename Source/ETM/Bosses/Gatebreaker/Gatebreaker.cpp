#include "Bosses/Gatebreaker/Gatebreaker.h"
#include "Enemies/Archetypes/EnemyPlaceholderMesh.h"
#include "Encounters/EncounterDirector.h"
#include "Weapons/CannonComponent.h"
#include "ETM.h"
#include "Components/AudioComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

const FName AGatebreaker::NodeLeftPart(TEXT("NodeLeft"));
const FName AGatebreaker::NodeRightPart(TEXT("NodeRight"));
const FName AGatebreaker::ChamberPart(TEXT("Chamber"));
const FName AGatebreaker::CorePart(TEXT("Core"));

namespace
{
	FEnemyPart MakeBossPart(FName Name, float Health, bool bInterrupts)
	{
		FEnemyPart Part;
		Part.PartName = Name;
		Part.MaxHealth = Health;
		Part.bInterruptOnBreak = bInterrupts;
		return Part;
	}
}

AGatebreaker::AGatebreaker()
{
	MaxHealth = 100000.f;       // unused: the hull is immune and the boss dies when its core breaks
	SpawnDuration = 3.f;        // tears open the blast door and locks in; no attacks meanwhile
	StaggerDuration = 1.f;      // the interrupted-furnace recoil
	DeathDuration = 4.f;        // collapse
	bExplodeOnDeath = false;    // the collapse is its own death sequence
	StaggerDamageThreshold = 0.f;
	bUsesAttackToken = false;   // the boss always gets its attack; the boss phase sets adds to 1 token (see header)
	// Keep the actor after death (parked, not destroyed): a failure after the boss dies retries the boss
	// checkpoint, and the director's rebuilt boss phase must find a boss to reactivate.
	MarkPooled();

	// P-02/P-09: nodes, charge chamber and core are designated weak points; breaking the chamber interrupts.
	Parts = {
		MakeBossPart(NodeLeftPart, 600.f, false),
		MakeBossPart(NodeRightPart, 600.f, false),
		MakeBossPart(ChamberPart, 240.f, true),
		MakeBossPart(CorePart, 6500.f, false), // P-34
	};

	using namespace ETMPlaceholder;
	// Wide, low siege platform on six legs. Everything not listed as a target is immune armour.
	Hull = MakePart(this, SceneRoot, TEXT("Hull"), EShape::Cube, FVector(500.f, 420.f, 220.f), FVector(0.f, 0.f, 300.f), FRotator::ZeroRotator, true);
	Hull->ComponentTags.Add(ETMTags::Immune);
	for (int32 Leg = 0; Leg < 6; ++Leg)
	{
		const float X = -180.f + 180.f * (Leg % 3);
		const float Y = Leg < 3 ? -260.f : 260.f;
		MakePart(this, SceneRoot, *FString::Printf(TEXT("Leg%d"), Leg), EShape::Cube, FVector(40.f, 40.f, 300.f), FVector(X, Y, 150.f), FRotator(0.f, 0.f, Leg < 3 ? 15.f : -15.f), false);
	}

	const auto MakeTarget = [this](const TCHAR* Name, FName Tag, EShape Shape, const FVector& Size, const FVector& Location)
	{
		UStaticMeshComponent* Mesh = MakePart(this, SceneRoot, Name, Shape, Size, Location, FRotator::ZeroRotator, true);
		Mesh->ComponentTags.Add(Tag);
		Mesh->ComponentTags.Add(ETMTags::WeakPoint);
		return Mesh;
	};
	NodeLeft = MakeTarget(TEXT("NodeLeft"), NodeLeftPart, EShape::Sphere, FVector(90.f), FVector(80.f, -230.f, 450.f));
	NodeRight = MakeTarget(TEXT("NodeRight"), NodeRightPart, EShape::Sphere, FVector(90.f), FVector(80.f, 230.f, 450.f));
	Chamber = MakeTarget(TEXT("Chamber"), ChamberPart, EShape::Cylinder, FVector(80.f, 80.f, 60.f), FVector(260.f, 0.f, 210.f));
	Core = MakeTarget(TEXT("Core"), CorePart, EShape::Sphere, FVector(150.f), FVector(255.f, 0.f, 330.f));

	// Shutters cover a closed target: the open/closed state reads by shape, not colour.
	NodeLeftShutter = MakePart(this, SceneRoot, TEXT("NodeLeftShutter"), EShape::Cube, FVector(110.f, 110.f, 110.f), FVector(80.f, -230.f, 450.f), FRotator::ZeroRotator, false);
	NodeRightShutter = MakePart(this, SceneRoot, TEXT("NodeRightShutter"), EShape::Cube, FVector(110.f, 110.f, 110.f), FVector(80.f, 230.f, 450.f), FRotator::ZeroRotator, false);
	ChamberShutter = MakePart(this, SceneRoot, TEXT("ChamberShutter"), EShape::Cube, FVector(20.f, 100.f, 80.f), FVector(305.f, 0.f, 210.f), FRotator::ZeroRotator, false);
	CoreShutter = MakePart(this, SceneRoot, TEXT("CoreShutter"), EShape::Cube, FVector(20.f, 180.f, 180.f), FVector(335.f, 0.f, 330.f), FRotator::ZeroRotator, false);

	ArtilleryPivot = CreateDefaultSubobject<USceneComponent>(TEXT("ArtilleryPivot"));
	ArtilleryPivot->SetupAttachment(SceneRoot);
	ArtilleryPivot->SetRelativeLocation(FVector(-150.f, 0.f, 420.f));
	MakePart(this, ArtilleryPivot, TEXT("ArtilleryBarrel"), EShape::Cylinder, FVector(40.f, 40.f, 260.f), FVector(130.f, 0.f, 0.f), FRotator(-90.f, 0.f, 0.f), false);

	ChargeAudio = CreateDefaultSubobject<UAudioComponent>(TEXT("ChargeAudio"));
	ChargeAudio->SetupAttachment(SceneRoot);
	ChargeAudio->bAutoActivate = false;
}

void AGatebreaker::BeginPlay()
{
	Super::BeginPlay();
	HomeTransform = GetActorTransform();
	SetDirector(Director);
}

void AGatebreaker::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	SetDirector(nullptr);
	Super::EndPlay(EndPlayReason);
}

void AGatebreaker::SetDirector(AEncounterDirector* InDirector)
{
	if (Director && Director != InDirector)
	{
		Director->OnPhaseStarted.RemoveDynamic(this, &AGatebreaker::HandleDirectorPhaseStarted);
	}
	Director = InDirector;
	if (Director)
	{
		Director->OnPhaseStarted.AddUniqueDynamic(this, &AGatebreaker::HandleDirectorPhaseStarted);
	}
}

void AGatebreaker::HandleDirectorPhaseStarted(int32 PhaseIndex, FName PhaseName)
{
	// Also fires when a retry rebuilds the boss phase: the boss comes back at full health (P-04).
	if (PhaseName == BossPhaseName && !IsActive())
	{
		Activate(HomeTransform);
	}
}

void AGatebreaker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

void AGatebreaker::SimulateStep(float DeltaSeconds)
{
	AdvanceState(DeltaSeconds);
	UpdateBehavior(DeltaSeconds);
}

AActor* AGatebreaker::ResolveTarget()
{
	if (!Target) Target = UGameplayStatics::GetPlayerPawn(this, 0);
	return Target;
}

// ---------------------------------------------------------------------------------------------
// Targets
// ---------------------------------------------------------------------------------------------

UStaticMeshComponent* AGatebreaker::MeshFor(FName TargetName) const
{
	if (TargetName == NodeLeftPart) return NodeLeft;
	if (TargetName == NodeRightPart) return NodeRight;
	if (TargetName == ChamberPart) return Chamber;
	if (TargetName == CorePart) return Core;
	return nullptr;
}

UStaticMeshComponent* AGatebreaker::ShutterFor(FName TargetName) const
{
	if (TargetName == NodeLeftPart) return NodeLeftShutter;
	if (TargetName == NodeRightPart) return NodeRightShutter;
	if (TargetName == ChamberPart) return ChamberShutter;
	if (TargetName == CorePart) return CoreShutter;
	return nullptr;
}

bool AGatebreaker::IsTargetExposed(FName TargetName) const
{
	return Exposed.Contains(TargetName);
}

void AGatebreaker::SetExposed(FName TargetName, bool bExposedNow, float Seconds)
{
	if (bExposedNow && IsPartBroken(TargetName)) return;
	const bool bWas = Exposed.Contains(TargetName);
	if (bExposedNow) Exposed.Add(TargetName); else Exposed.Remove(TargetName);

	if (UStaticMeshComponent* Mesh = MeshFor(TargetName))
	{
		Mesh->SetCollisionEnabled(bExposedNow ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
	if (UStaticMeshComponent* Shutter = ShutterFor(TargetName))
	{
		Shutter->SetVisibility(!bExposedNow);
	}
	if (bWas != bExposedNow)
	{
		OnTargetExposed.Broadcast(TargetName, bExposedNow, Seconds);
	}
}

void AGatebreaker::CloseEverything()
{
	for (const FName TargetName : { NodeLeftPart, NodeRightPart, ChamberPart, CorePart })
	{
		SetExposed(TargetName, false);
	}
}

void AGatebreaker::ResetPart(FName PartName)
{
	// The charge chamber is rebuilt for every furnace attack; Parts is the base class's public part list.
	for (FEnemyPart& Part : Parts)
	{
		if (Part.PartName == PartName)
		{
			Part.Health = Part.MaxHealth;
			Part.bBroken = false;
		}
	}
}

float AGatebreaker::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	const UPrimitiveComponent* HitComponent = nullptr;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		HitComponent = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.GetComponent();
	}

	// Only an exposed target takes damage. Everything else is immune armour with deflection feedback,
	// including damage that names no component, so nothing can wear the boss down outside its windows.
	bool bOnExposedTarget = false;
	for (const FName TargetName : Exposed)
	{
		if (HitComponent && HitComponent->ComponentHasTag(TargetName)) bOnExposedTarget = true;
	}
	if (!bOnExposedTarget)
	{
		if (IsActive())
		{
			++DeflectedCount;
			const FVector Where = DamageEvent.IsOfType(FPointDamageEvent::ClassID)
				? FVector(static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.ImpactPoint) : GetActorLocation();
			OnDeflected.Broadcast(Where);
		}
		return 0.f;
	}

	const float Applied = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	// P-10: phase 3 starts once the core is at or below 30%.
	if (BossPhase == EGatebreakerPhase::Breach && !IsPartBroken(CorePart))
	{
		const FEnemyPart* CoreData = Parts.FindByPredicate([](const FEnemyPart& Part) { return Part.PartName == CorePart; });
		if (CoreData && CoreData->Health <= CoreData->MaxHealth * CollapseThreshold)
		{
			SetBossPhase(EGatebreakerPhase::Collapse);
			bArtilleryNext = true;
		}
	}
	return Applied;
}

void AGatebreaker::HandlePartBroken(const FEnemyPart& Part)
{
	SetExposed(Part.PartName, false);

	if (Part.PartName == NodeLeftPart || Part.PartName == NodeRightPart)
	{
		bNodeWindowOpen = false;
		NodeTimer = 0.f;
		if (IsPartBroken(NodeLeftPart) && IsPartBroken(NodeRightPart))
		{
			SetBossPhase(EGatebreakerPhase::Breach); // phase 1 ends when both nodes are destroyed
		}
	}
	else if (Part.PartName == CorePart)
	{
		Kill();
	}
	// The chamber: the base class staggers the boss right after this, which ends the furnace as interrupted.
}

// ---------------------------------------------------------------------------------------------
// Phases and attacks
// ---------------------------------------------------------------------------------------------

void AGatebreaker::SetBossPhase(EGatebreakerPhase NewPhase)
{
	if (NewPhase == BossPhase) return;
	BossPhase = NewPhase;
	if (NewPhase != EGatebreakerPhase::Anchor)
	{
		SetExposed(NodeLeftPart, false);
		SetExposed(NodeRightPart, false);
	}
	UE_LOG(LogETM, Display, TEXT("Gatebreaker phase %d"), static_cast<int32>(NewPhase));
	OnBossPhaseChanged.Broadcast(NewPhase);
}

void AGatebreaker::HandleStateChanged(EEnemyState NewState, EEnemyState OldState)
{
	switch (NewState)
	{
	case EEnemyState::Spawning:
		CloseEverything();
		Exposed.Reset();
		CurrentAttack = EGatebreakerAttack::None;
		CoreOpenRemaining = 0.f;
		CoreExposureCount = 0;
		NodeTimer = 0.f;
		NodeTurn = 0;
		bNodeWindowOpen = false;
		bArtilleryNext = false;
		AttackCooldown = AttackInterval;
		BossPhase = EGatebreakerPhase::Dormant;
		break;

	case EEnemyState::Approaching:
		if (BossPhase == EGatebreakerPhase::Dormant) SetBossPhase(EGatebreakerPhase::Anchor);
		break;

	case EEnemyState::Staggered:
		if (OldState == EEnemyState::Attacking && CurrentAttack == EGatebreakerAttack::Furnace)
		{
			EndFurnace(/*bInterrupted*/ true);
		}
		break;

	case EEnemyState::Dying:
		CloseEverything();
		CurrentAttack = EGatebreakerAttack::None;
		SetBossPhase(EGatebreakerPhase::Destroyed);
		if (Director) Director->CompletePhase(); // boss completion is gameplay-driven (bible section 8)
		break;

	case EEnemyState::Inactive:
		CloseEverything();
		CurrentAttack = EGatebreakerAttack::None;
		BossPhase = EGatebreakerPhase::Dormant;
		break;

	default:
		break;
	}

	if (NewState != EEnemyState::Attacking)
	{
		if (ChargeAudio) ChargeAudio->Stop();
		if (ArtilleryPivot) ArtilleryPivot->SetRelativeRotation(FRotator::ZeroRotator);
	}
}

void AGatebreaker::StartAttack()
{
	AActor* Rig = ResolveTarget();
	if (!Rig) return;

	const bool bArtillery = BossPhase == EGatebreakerPhase::Collapse && bArtilleryNext;
	if (bArtillery)
	{
		if (!BeginAttack(Rig, ArtilleryDamage, ArtilleryTelegraphSeconds)) return;
		CurrentAttack = EGatebreakerAttack::Artillery;
		bArtilleryNext = false;
		if (ChargeAudio && ArtilleryChargeSound) { ChargeAudio->SetSound(ArtilleryChargeSound); ChargeAudio->Play(); }
		return;
	}

	if (!BeginAttack(Rig, FurnaceDamage, FurnaceChargeSeconds)) return;
	CurrentAttack = EGatebreakerAttack::Furnace;
	bArtilleryNext = BossPhase == EGatebreakerPhase::Collapse; // phase 3 alternates artillery and furnace
	ResetPart(ChamberPart);
	SetExposed(ChamberPart, true, FurnaceChargeSeconds); // the chamber is the furnace's shape cue and its weak point
	if (ChargeAudio && FurnaceChargeSound) { ChargeAudio->SetSound(FurnaceChargeSound); ChargeAudio->Play(); }
}

void AGatebreaker::ResolveAttack(AActor* AttackTarget, float Damage)
{
	Super::ResolveAttack(AttackTarget, Damage);
	if (CurrentAttack == EGatebreakerAttack::Furnace)
	{
		EndFurnace(/*bInterrupted*/ false);
	}
	else
	{
		CurrentAttack = EGatebreakerAttack::None;
		AttackCooldown = AttackInterval; // artillery does not open the core; the furnace comes next
	}
}

void AGatebreaker::EndFurnace(bool bInterrupted)
{
	CurrentAttack = EGatebreakerAttack::None;
	SetExposed(ChamberPart, false);
	if (BossPhase == EGatebreakerPhase::Breach || BossPhase == EGatebreakerPhase::Collapse)
	{
		OpenCore(bInterrupted ? CoreWindowInterrupted : CoreWindowFired); // P-10: after every furnace attack
	}
	else
	{
		AttackCooldown = AttackInterval;
	}
}

void AGatebreaker::OpenCore(float Seconds)
{
	CoreOpenRemaining = Seconds;
	++CoreExposureCount;
	SetExposed(CorePart, true, Seconds);
}

void AGatebreaker::UpdateNodeWindows(float DeltaSeconds)
{
	// Alternating windows: one node open for NodeWindowSeconds, then a gap, then the other (or the same one
	// again if its partner is already destroyed). A missed window just comes round again.
	NodeTimer += DeltaSeconds;
	if (bNodeWindowOpen)
	{
		if (NodeTimer >= NodeWindowSeconds)
		{
			SetExposed(NodeLeftPart, false);
			SetExposed(NodeRightPart, false);
			bNodeWindowOpen = false;
			NodeTimer = 0.f;
			++NodeTurn;
		}
		return;
	}
	if (NodeTimer < NodeGapSeconds) return;

	FName Next = (NodeTurn % 2 == 0) ? NodeLeftPart : NodeRightPart;
	if (IsPartBroken(Next)) Next = (Next == NodeLeftPart) ? NodeRightPart : NodeLeftPart;
	if (IsPartBroken(Next)) return;
	SetExposed(Next, true, NodeWindowSeconds);
	bNodeWindowOpen = true;
	NodeTimer = 0.f;
}

void AGatebreaker::UpdateBehavior(float DeltaSeconds)
{
	const EEnemyState Now = GetState();
	if (Now == EEnemyState::Inactive || Now == EEnemyState::Spawning || Now == EEnemyState::Dying) return;

	if (BossPhase == EGatebreakerPhase::Anchor)
	{
		UpdateNodeWindows(DeltaSeconds);
	}

	// Core window: counts down whatever the boss is doing; the next attack waits for it to close plus a vent gap.
	if (CoreOpenRemaining > 0.f)
	{
		CoreOpenRemaining -= DeltaSeconds;
		if (CoreOpenRemaining <= 0.f)
		{
			CoreOpenRemaining = 0.f;
			SetExposed(CorePart, false);
			AttackCooldown = AttackInterval;
		}
		return;
	}

	if (Now == EEnemyState::Attacking)
	{
		if (CurrentAttack == EGatebreakerAttack::Artillery && ArtilleryPivot && ArtilleryTelegraphSeconds > 0.f)
		{
			const float Progress = FMath::Clamp(1.f - GetAttackTimeRemaining() / ArtilleryTelegraphSeconds, 0.f, 1.f);
			ArtilleryPivot->SetRelativeRotation(FRotator(55.f * Progress, 0.f, 0.f)); // barrel rises: shape cue
		}
		return;
	}

	if (Now == EEnemyState::Approaching && CurrentAttack == EGatebreakerAttack::None)
	{
		AttackCooldown -= DeltaSeconds;
		if (AttackCooldown <= 0.f)
		{
			StartAttack();
		}
	}
}
