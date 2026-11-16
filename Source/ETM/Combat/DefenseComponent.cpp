#include "Combat/DefenseComponent.h"

UDefenseComponent::UDefenseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

bool UDefenseComponent::TryActivateBrace()
{
	if (!IsBraceReady()) return false;
	ActiveRemaining = Duration;
	OnBraceStateChanged.Broadcast(true);
	return true;
}

float UDefenseComponent::ModifyIncomingRigDamage(float Damage)
{
	if (!IsBraceActive() || Damage <= 0.f) return Damage;
	const float Reduced = Damage * (1.f - DamageReduction);
	DamageAbsorbed += Damage - Reduced;
	return Reduced;
}

void UDefenseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (ActiveRemaining > 0.f)
	{
		ActiveRemaining -= DeltaTime;
		if (ActiveRemaining <= 0.f)
		{
			ActiveRemaining = 0.f;
			CooldownRemaining = Cooldown; // cooldown begins when the field ends
			OnBraceStateChanged.Broadcast(false);
		}
	}
	else if (CooldownRemaining > 0.f)
	{
		CooldownRemaining = FMath::Max(0.f, CooldownRemaining - DeltaTime);
	}
}
