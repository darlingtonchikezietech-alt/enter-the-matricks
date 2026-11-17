// Counts enemy events for the automation tests in EnemyBaseTest.cpp.
// Dynamic delegates can only bind to UFUNCTIONs, so the listener has to be a UObject with a header.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Enemies/Base/EnemyTypes.h"
#include "EnemyTestListener.generated.h"

class AEnemyBase;

UCLASS(Hidden, NotBlueprintable)
class UETMEnemyTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION() void HandleStateChanged(AEnemyBase* Enemy, EEnemyState NewState, EEnemyState OldState) { States.Add(NewState); }
	UFUNCTION() void HandleDamaged(AEnemyBase* Enemy, float Amount, FName PartName) { ++Damaged; LastDamage = Amount; LastPart = PartName; }
	UFUNCTION() void HandlePartBroken(AEnemyBase* Enemy, FName PartName) { ++PartsBroken; LastBrokenPart = PartName; }
	UFUNCTION() void HandleTelegraph(AEnemyBase* Enemy, float TelegraphSeconds, AActor* Target) { ++Telegraphs; LastTelegraph = TelegraphSeconds; }
	UFUNCTION() void HandleInterrupted(AEnemyBase* Enemy, float PreventedDamage) { ++Interrupted; PreventedTotal += PreventedDamage; }
	UFUNCTION() void HandleResolved(AEnemyBase* Enemy, AActor* Target, float DamageDealt) { ++Resolved; LastResolvedDamage = DamageDealt; }
	UFUNCTION() void HandleKilled(AEnemyBase* Enemy) { ++Killed; }

	TArray<EEnemyState> States;
	int32 Damaged = 0;
	int32 PartsBroken = 0;
	int32 Telegraphs = 0;
	int32 Interrupted = 0;
	int32 Resolved = 0;
	int32 Killed = 0;
	float LastDamage = 0.f;
	float LastTelegraph = 0.f;
	float PreventedTotal = 0.f;
	float LastResolvedDamage = 0.f;
	FName LastPart;
	FName LastBrokenPart;
};
