#include "Enemies/Base/EnemyBase.h"
#include "Enemies/Base/AttackTokenSubsystem.h"
#include "ETM.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/DamageEvents.h"
#include "Kismet/GameplayStatics.h"

AEnemyBase::AEnemyBase()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false; // parked enemies cost nothing

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	RootComponent = SceneRoot;
}

void AEnemyBase::PostInitializeComponents()
{
	Super::PostInitializeComponents();
	// Enemies start parked, before BeginPlay, so one placed in a level is never live by accident.
	// The pool or the encounter director brings them into play with Activate.
	if (State == EEnemyState::Inactive)
	{
		SetInPlay(false);
	}
}

void AEnemyBase::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AdvanceState(DeltaSeconds);
}

// ---------------------------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------------------------

void AEnemyBase::SetInPlay(bool bInPlay)
{
	SetActorHiddenInGame(!bInPlay);
	SetActorEnableCollision(bInPlay);
	SetActorTickEnabled(bInPlay);
}

bool AEnemyBase::SetState(EEnemyState NewState)
{
	if (NewState == State || !ETMEnemy::CanTransition(State, NewState))
	{
		return false;
	}

	const EEnemyState OldState = State;

	if (OldState == EEnemyState::Attacking)
	{
		ReleaseAttackToken(); // whatever ends the attack state returns the token
	}

	// Leaving an attack that has not resolved means it was stopped: nothing is dealt, and the
	// damage it would have done is reported as prevented.
	if (OldState == EEnemyState::Attacking && bAttackPending)
	{
		const float Prevented = PendingAttackDamage;
		bAttackPending = false;
		PendingAttackDamage = 0.f;
		AttackTimeRemaining = 0.f;
		PendingAttackTarget.Reset();
		OnAttackInterrupted.Broadcast(this, Prevented);
	}

	State = NewState;
	TimeInState = 0.f;

	switch (NewState)
	{
	case EEnemyState::Spawning:
		SetInPlay(true);
		break;
	case EEnemyState::Dying:
		SetActorEnableCollision(false); // shots pass through wreckage; debris has no gameplay collision
		break;
	case EEnemyState::Inactive:
		SetInPlay(false);
		break;
	default:
		break;
	}

	HandleStateChanged(NewState, OldState);
	ReceiveStateEntered(NewState, OldState);
	OnStateChanged.Broadcast(this, NewState, OldState);
	if (NewState == EEnemyState::Dying)
	{
		OnKilled.Broadcast(this);
	}
	return true;
}

void AEnemyBase::Activate(const FTransform& SpawnTransform)
{
	if (State != EEnemyState::Inactive)
	{
		UE_LOG(LogETM, Warning, TEXT("%s: Activate ignored, enemy is already in play."), *GetName());
		return;
	}

	SetActorTransform(SpawnTransform, false, nullptr, ETeleportType::ResetPhysics);

	Health = MaxHealth;
	StaggerDamage = 0.f;
	TimeSinceBodyDamage = 0.f;
	bAttackPending = false;
	PendingAttackDamage = 0.f;
	AttackTimeRemaining = 0.f;
	PendingAttackTarget.Reset();
	for (FEnemyPart& Part : Parts)
	{
		if (Part.bBroken)
		{
			SetPartCollisionEnabled(Part.PartName, true);
		}
		Part.Health = Part.MaxHealth;
		Part.bBroken = false;
	}

	SetState(EEnemyState::Spawning);
	if (SpawnDuration <= 0.f)
	{
		SetState(EEnemyState::Approaching);
	}
}

void AEnemyBase::Deactivate()
{
	if (State == EEnemyState::Inactive) return;

	// Not a kill: go straight to the pool without a death sequence or a kill event.
	const EEnemyState OldState = State;
	ReleaseAttackToken();
	bAttackPending = false;
	PendingAttackDamage = 0.f;
	PendingAttackTarget.Reset();
	State = EEnemyState::Inactive;
	TimeInState = 0.f;
	SetInPlay(false);
	HandleStateChanged(State, OldState);
	ReceiveStateEntered(State, OldState);
	OnStateChanged.Broadcast(this, State, OldState);
}

void AEnemyBase::Kill()
{
	if (!SetState(EEnemyState::Dying)) return;
	if (DeathDuration <= 0.f)
	{
		AdvanceState(0.f);
	}
}

bool AEnemyBase::Interrupt()
{
	return SetState(EEnemyState::Staggered);
}

void AEnemyBase::AdvanceState(float DeltaTime)
{
	if (State == EEnemyState::Inactive) return;

	TimeInState += DeltaTime;
	TimeSinceBodyDamage += DeltaTime;
	if (TimeSinceBodyDamage > StaggerWindow)
	{
		StaggerDamage = 0.f;
	}

	switch (State)
	{
	case EEnemyState::Spawning:
		if (TimeInState >= SpawnDuration)
		{
			SetState(EEnemyState::Approaching);
		}
		break;

	case EEnemyState::Attacking:
		if (bAttackPending)
		{
			AttackTimeRemaining -= DeltaTime;
			if (AttackTimeRemaining <= 0.f)
			{
				AActor* Target = PendingAttackTarget.Get();
				const float Damage = PendingAttackDamage;
				bAttackPending = false; // resolved, so leaving the state is not an interruption
				ReleaseAttackToken();
				PendingAttackDamage = 0.f;
				PendingAttackTarget.Reset();
				ResolveAttack(Target, Damage);
				if (State == EEnemyState::Attacking && !bAttackPending)
				{
					SetState(EEnemyState::Approaching);
				}
			}
		}
		break;

	case EEnemyState::Staggered:
		if (TimeInState >= StaggerDuration)
		{
			SetState(EEnemyState::Approaching);
		}
		break;

	case EEnemyState::Dying:
		if (TimeInState >= DeathDuration)
		{
			SetState(EEnemyState::Inactive);
			if (!bPooled)
			{
				Destroy();
			}
		}
		break;

	default:
		break;
	}
}

// ---------------------------------------------------------------------------------------------
// Attacks
// ---------------------------------------------------------------------------------------------

void AEnemyBase::ReleaseAttackToken()
{
	if (UAttackTokenSubsystem* Tokens = GetWorld() ? GetWorld()->GetSubsystem<UAttackTokenSubsystem>() : nullptr)
	{
		Tokens->Release(this);
	}
}

void AEnemyBase::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseAttackToken();
	Super::EndPlay(EndPlayReason);
}

bool AEnemyBase::BeginAttack(AActor* Target, float Damage, float TelegraphSeconds)
{
	if (bAttackPending) return false; // one attack at a time
	if (!ETMEnemy::CanDealDamage(State)) return false;

	// Attack tokens (bible section 14: at most 2 dangerous attacks at once). Refused: try again later.
	UAttackTokenSubsystem* Tokens = bUsesAttackToken && GetWorld() ? GetWorld()->GetSubsystem<UAttackTokenSubsystem>() : nullptr;
	if (Tokens && !Tokens->TryAcquire(this)) return false;

	// An enemy already in the attack state with nothing pending may chain a new attack.
	if (State != EEnemyState::Attacking && !SetState(EEnemyState::Attacking))
	{
		if (Tokens) Tokens->Release(this);
		return false;
	}

	PendingAttackTarget = Target;
	PendingAttackDamage = FMath::Max(0.f, Damage);
	AttackTimeRemaining = FMath::Max(0.f, TelegraphSeconds);
	bAttackPending = true;
	OnAttackTelegraph.Broadcast(this, AttackTimeRemaining, Target);
	return true;
}

void AEnemyBase::ResolveAttack(AActor* Target, float Damage)
{
	const float Dealt = DealDamageTo(Target, Damage);
	OnAttackResolved.Broadcast(this, Target, Dealt);
}

float AEnemyBase::DealDamageTo(AActor* Target, float Amount)
{
	if (!CanDealDamage() || !Target || Amount <= 0.f)
	{
		return 0.f;
	}
	return UGameplayStatics::ApplyDamage(Target, Amount, nullptr, this, nullptr);
}

// ---------------------------------------------------------------------------------------------
// Damage intake
// ---------------------------------------------------------------------------------------------

float AEnemyBase::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (!ETMEnemy::CanTakeDamage(State))
	{
		return 0.f;
	}

	const float Actual = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	if (Actual <= 0.f)
	{
		return 0.f;
	}

	const UPrimitiveComponent* HitComponent = nullptr;
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		HitComponent = static_cast<const FPointDamageEvent&>(DamageEvent).HitInfo.GetComponent();
	}

	if (FEnemyPart* Part = FindPartForComponent(HitComponent))
	{
		ApplyPartDamage(*Part, Actual);
	}
	else
	{
		OnDamaged.Broadcast(this, FMath::Min(Actual, Health), NAME_None);
		ApplyBodyDamage(Actual);
	}
	return Actual;
}

void AEnemyBase::ApplyPartDamage(FEnemyPart& Part, float Amount)
{
	const float Scaled = Amount * Part.DamageScale;
	const float Lost = FMath::Min(Scaled, Part.Health);
	Part.Health -= Lost;
	OnDamaged.Broadcast(this, Lost, Part.PartName);

	// Copy what is needed first: the handlers below may change state.
	const FName PartName = Part.PartName;
	const bool bBreaks = Part.Health <= 0.f;
	const bool bInterrupts = Part.bInterruptOnBreak;
	const float BreakDamage = Part.BodyDamageOnBreak;

	if (Part.bPassDamageToBody)
	{
		ApplyBodyDamage(Scaled);
	}

	if (bBreaks && ETMEnemy::CanTakeDamage(State))
	{
		Part.bBroken = true;
		SetPartCollisionEnabled(PartName, false);
		HandlePartBroken(Part);
		OnPartBroken.Broadcast(this, PartName);
		if (BreakDamage > 0.f)
		{
			ApplyBodyDamage(BreakDamage);
		}
		if (bInterrupts)
		{
			Interrupt();
		}
	}
}

void AEnemyBase::ApplyBodyDamage(float Amount)
{
	if (Amount <= 0.f || !ETMEnemy::CanTakeDamage(State)) return;

	Health = FMath::Max(0.f, Health - Amount);
	if (Health <= 0.f)
	{
		Kill();
		return;
	}

	if (TimeSinceBodyDamage > StaggerWindow)
	{
		StaggerDamage = 0.f;
	}
	TimeSinceBodyDamage = 0.f;
	StaggerDamage += Amount;
	if (StaggerDamageThreshold > 0.f && StaggerDamage >= StaggerDamageThreshold)
	{
		StaggerDamage = 0.f;
		Interrupt();
	}
}

// ---------------------------------------------------------------------------------------------
// Parts
// ---------------------------------------------------------------------------------------------

FEnemyPart* AEnemyBase::FindPartForComponent(const UPrimitiveComponent* Component)
{
	if (!Component) return nullptr;
	for (FEnemyPart& Part : Parts)
	{
		if (!Part.bBroken && !Part.PartName.IsNone() && Component->ComponentHasTag(Part.PartName))
		{
			return &Part;
		}
	}
	return nullptr;
}

const FEnemyPart* AEnemyBase::FindPart(FName PartName) const
{
	return Parts.FindByPredicate([PartName](const FEnemyPart& Part) { return Part.PartName == PartName; });
}

bool AEnemyBase::IsPartBroken(FName PartName) const
{
	const FEnemyPart* Part = FindPart(PartName);
	return Part && Part->bBroken;
}

float AEnemyBase::GetPartHealth(FName PartName) const
{
	const FEnemyPart* Part = FindPart(PartName);
	return Part ? Part->Health : 0.f;
}

void AEnemyBase::SetPartCollisionEnabled(FName PartName, bool bEnabled)
{
	TInlineComponentArray<UPrimitiveComponent*> Primitives(this);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		if (Primitive->ComponentHasTag(PartName))
		{
			// Parts are query-only targets; the cannon traces against them and nothing collides physically.
			Primitive->SetCollisionEnabled(bEnabled ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
		}
	}
}
