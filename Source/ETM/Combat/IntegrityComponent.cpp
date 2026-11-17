#include "Combat/IntegrityComponent.h"
#include "Combat/DefenseComponent.h"
#include "GameFramework/Actor.h"

UIntegrityComponent::UIntegrityComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UIntegrityComponent::BeginPlay()
{
	Super::BeginPlay();

	Current = MaxIntegrity;
	bDepleted = false;

	AActor* Owner = GetOwner();
	if (bIsRig && !Defense && Owner)
	{
		Defense = Owner->FindComponentByClass<UDefenseComponent>();
	}
	if (bBindToOwnerDamage && Owner)
	{
		Owner->OnTakeAnyDamage.AddUniqueDynamic(this, &UIntegrityComponent::HandleOwnerDamage);
	}
}

void UIntegrityComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AActor* Owner = GetOwner())
	{
		Owner->OnTakeAnyDamage.RemoveDynamic(this, &UIntegrityComponent::HandleOwnerDamage);
	}
	Super::EndPlay(EndPlayReason);
}

void UIntegrityComponent::HandleOwnerDamage(AActor* DamagedActor, float Damage, const UDamageType* DamageType, AController* InstigatedBy, AActor* DamageCauser)
{
	ApplyIntegrityDamage(Damage, DamageCauser);
}

float UIntegrityComponent::ApplyIntegrityDamage(float Amount, AActor* DamageCauser)
{
	if (Amount <= 0.f || bDepleted) return 0.f;

	float Incoming = Amount * FMath::Max(0.f, IncomingDamageScale);
	if (bIsRig && Defense)
	{
		Incoming = Defense->ModifyIncomingRigDamage(Incoming); // the objective never gets this (bible section 5)
	}

	const float Before = Current;
	Current = FMath::Max(0.f, Current - Incoming);
	const float Applied = Before - Current;

	OnDamaged.Broadcast(Applied, FMath::Max(0.f, Amount - Incoming), DamageCauser);
	if (Applied > 0.f)
	{
		OnIntegrityChanged.Broadcast(Current, MaxIntegrity, -Applied);
	}
	if (Current <= 0.f)
	{
		bDepleted = true;
		OnDepleted.Broadcast();
	}
	return Applied;
}

float UIntegrityComponent::Repair(float Amount)
{
	if (Amount <= 0.f || bDepleted) return 0.f;
	const float Before = Current;
	Current = FMath::Min(MaxIntegrity, Current + Amount); // capped at maximum (bible section 6)
	const float Added = Current - Before;
	if (Added > 0.f)
	{
		OnIntegrityChanged.Broadcast(Current, MaxIntegrity, Added);
	}
	return Added;
}

void UIntegrityComponent::ResetTo(float Value)
{
	const float Before = Current;
	Current = FMath::Clamp(Value, 0.f, MaxIntegrity);
	bDepleted = Current <= 0.f;
	OnIntegrityChanged.Broadcast(Current, MaxIntegrity, Current - Before);
}

void UIntegrityComponent::RestoreCheckpoint(float SavedValue, float MinFraction)
{
	ResetTo(FMath::Max(SavedValue, MaxIntegrity * FMath::Clamp(MinFraction, 0.f, 1.f)));
}
