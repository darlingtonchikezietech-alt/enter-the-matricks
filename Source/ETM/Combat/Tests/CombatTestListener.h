// Counts brace and integrity events for the automation tests in CombatComponentsTest.cpp.
// Dynamic delegates can only bind to UFUNCTIONs, so the listener has to be a UObject with a header.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "CombatTestListener.generated.h"

UCLASS(Hidden, NotBlueprintable)
class UETMCombatTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION() void HandleBraceStateChanged(bool bActive) { bActive ? ++BraceActivated : ++BraceEnded; }
	UFUNCTION() void HandleBraceReady() { ++BraceReady; }
	UFUNCTION() void HandleBraceAbsorbed(float Absorbed, float Passed) { ++BraceAbsorbedEvents; LastAbsorbed = Absorbed; }
	UFUNCTION() void HandleIntegrityChanged(float Current, float Max, float Delta) { ++IntegrityChanged; LastDelta = Delta; }
	UFUNCTION() void HandleDamaged(float Applied, float Prevented, AActor* DamageCauser) { ++Damaged; LastApplied = Applied; LastPrevented = Prevented; }
	UFUNCTION() void HandleDepleted() { ++Depleted; }

	int32 BraceActivated = 0;
	int32 BraceEnded = 0;
	int32 BraceReady = 0;
	int32 BraceAbsorbedEvents = 0;
	int32 IntegrityChanged = 0;
	int32 Damaged = 0;
	int32 Depleted = 0;
	float LastAbsorbed = 0.f;
	float LastDelta = 0.f;
	float LastApplied = 0.f;
	float LastPrevented = 0.f;
};
