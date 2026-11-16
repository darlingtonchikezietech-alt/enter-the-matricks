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
}

float UIntegrityComponent::ApplyIntegrityDamage(float Amount)
{
	if (Amount <= 0.f || IsDepleted()) return 0.f;
	if (bIsRig)
	{
		if (UDefenseComponent* Defense = GetOwner() ? GetOwner()->FindComponentByClass<UDefenseComponent>() : nullptr)
		{
			Amount = Defense->ModifyIncomingRigDamage(Amount);
		}
	}
	const float Before = Current;
	Current = FMath::Max(0.f, Current - Amount);
	OnIntegrityChanged.Broadcast(Current, MaxIntegrity, Current - Before);
	if (Current <= 0.f)
	{
		OnDepleted.Broadcast();
	}
	return Before - Current;
}

void UIntegrityComponent::Repair(float Amount)
{
	if (Amount <= 0.f || IsDepleted()) return;
	const float Before = Current;
	Current = FMath::Min(MaxIntegrity, Current + Amount); // capped at maximum (bible section 6)
	OnIntegrityChanged.Broadcast(Current, MaxIntegrity, Current - Before);
}

void UIntegrityComponent::ResetTo(float Value)
{
	const float Before = Current;
	Current = FMath::Clamp(Value, 0.f, MaxIntegrity);
	OnIntegrityChanged.Broadcast(Current, MaxIntegrity, Current - Before);
}
