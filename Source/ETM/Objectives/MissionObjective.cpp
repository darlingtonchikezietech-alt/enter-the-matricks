#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Objectives/MissionTypes.h"
#include "Combat/IntegrityComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"

AMissionObjective::AMissionObjective()
{
	PrimaryActorTick.bCanEverTick = false;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;

	MarkerAnchor = CreateDefaultSubobject<USceneComponent>(TEXT("MarkerAnchor"));
	MarkerAnchor->SetupAttachment(SceneRoot);
	MarkerAnchor->SetRelativeLocation(FVector(0.f, 0.f, 250.f));

	Integrity = CreateDefaultSubobject<UIntegrityComponent>(TEXT("Integrity"));
	Integrity->bIsRig = false;                // the brace never protects the objective
	Integrity->bBindToOwnerDamage = false;    // damage is filtered in TakeDamage first
}

void AMissionObjective::BeginPlay()
{
	Super::BeginPlay();
	Integrity->MaxIntegrity = MaxIntegrity;
	Integrity->ResetToMax();

	if (bRegisterOnBeginPlay)
	{
		if (UMissionStateSubsystem* Mission = GetWorld() ? GetWorld()->GetSubsystem<UMissionStateSubsystem>() : nullptr)
		{
			Mission->RegisterObjective(this);
		}
	}
}

bool AMissionObjective::IsDesignatedAttacker(const AActor* Candidate) const
{
	if (!Candidate || Candidate == this) return false;
	if (Candidate->ActorHasTag(ETMObjectiveTags::InfrastructureAttacker)) return true;
	for (const TSubclassOf<AActor>& AllowedClass : DesignatedAttackerClasses)
	{
		if (AllowedClass && Candidate->IsA(AllowedClass)) return true;
	}
	return false;
}

float AMissionObjective::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (DamageAmount <= 0.f || Integrity->IsDepleted()) return 0.f;

	if (!IsDesignatedAttacker(DamageCauser))
	{
		++RejectedDamageCount; // cannon fire, rig damage, or damage with no source
		return 0.f;
	}

	const float Actual = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Actual <= 0.f) return 0.f;

	const UWorld* World = GetWorld();
	const double Now = World ? World->GetTimeSeconds() : 0.0;
	const bool bWasQuiet = !IsUnderAttack();
	LastDamageTime = Now;
	LastAttackerLocation = DamageCauser->GetActorLocation();

	const float Applied = Integrity->ApplyIntegrityDamage(Actual, DamageCauser);
	if (bWasQuiet)
	{
		OnUnderAttack.Broadcast(this, DamageCauser);
	}
	return Applied;
}

FVector AMissionObjective::GetMarkerLocation() const
{
	return MarkerAnchor ? MarkerAnchor->GetComponentLocation() : GetActorLocation();
}

bool AMissionObjective::IsUnderAttack() const
{
	const UWorld* World = GetWorld();
	return World && (World->GetTimeSeconds() - LastDamageTime) < UnderAttackHoldSeconds;
}

float AMissionObjective::GetIntegrityNormalized() const
{
	return Integrity ? Integrity->GetNormalized() : 0.f;
}
