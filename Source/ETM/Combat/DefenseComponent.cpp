#include "Combat/DefenseComponent.h"

UDefenseComponent::UDefenseComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Ticks only while the field is up or cooling down.
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

ETM::FBraceTuning UDefenseComponent::MakeTuning() const
{
	ETM::FBraceTuning T;
	T.Duration = Duration;
	T.Cooldown = Cooldown;
	T.DamageReduction = DamageReduction;
	return T;
}

bool UDefenseComponent::TryActivateBrace()
{
	if (!ETM::TryActivateBrace(State, MakeTuning())) return false;
	if (IsRegistered()) SetComponentTickEnabled(true);
	OnBraceStateChanged.Broadcast(true);
	return true;
}

float UDefenseComponent::ModifyIncomingRigDamage(float Damage)
{
	const float AbsorbedBefore = State.DamageAbsorbed;
	const float Passed = ETM::ModifyRigDamage(State, MakeTuning(), Damage);
	const float Absorbed = State.DamageAbsorbed - AbsorbedBefore;
	if (Absorbed > 0.f)
	{
		OnBraceAbsorbedDamage.Broadcast(Absorbed, Passed);
	}
	return Passed;
}

void UDefenseComponent::ResetBrace(bool bClearAbsorbed)
{
	const bool bWasActive = IsBraceActive();
	const bool bWasReady = IsBraceReady();
	State.ActiveRemaining = 0.f;
	State.CooldownRemaining = 0.f;
	if (bClearAbsorbed) State.DamageAbsorbed = 0.f;
	if (IsRegistered()) SetComponentTickEnabled(false);
	if (bWasActive) OnBraceStateChanged.Broadcast(false);
	if (!bWasReady) OnBraceReady.Broadcast();
}

float UDefenseComponent::GetReadiness() const
{
	if (IsBraceActive()) return 0.f;
	if (Cooldown <= 0.f) return 1.f;
	return 1.f - FMath::Clamp(State.CooldownRemaining / Cooldown, 0.f, 1.f);
}

void UDefenseComponent::AdvanceBrace(float DeltaTime)
{
	const ETM::FBraceStepResult Result = ETM::StepBrace(State, MakeTuning(), DeltaTime);
	if (Result.bEnded)
	{
		OnBraceStateChanged.Broadcast(false);
	}
	if (Result.bBecameReady)
	{
		if (IsRegistered()) SetComponentTickEnabled(false);
		OnBraceReady.Broadcast();
	}
}

void UDefenseComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdvanceBrace(DeltaTime);
}
