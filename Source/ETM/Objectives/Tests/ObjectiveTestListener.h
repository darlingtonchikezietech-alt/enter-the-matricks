// Counts mission and objective events for the automation tests in ObjectiveTest.cpp.
// Dynamic delegates can only bind to UFUNCTIONs, so the listener has to be a UObject with a header.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Objectives/MissionTypes.h"
#include "ObjectiveTestListener.generated.h"

class AMissionObjective;

UCLASS(Hidden, NotBlueprintable)
class UETMObjectiveTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION() void HandleFailed(EMissionFailReason Reason) { ++Failed; LastReason = Reason; }
	UFUNCTION() void HandleCheckpoint(const FMissionCheckpoint& Checkpoint) { ++Checkpoints; LastCheckpointName = Checkpoint.Name; }
	UFUNCTION() void HandleRetry(const FMissionCheckpoint& Checkpoint) { ++Retries; LastRetryPhase = Checkpoint.PhaseIndex; LastRetryName = Checkpoint.Name; }
	UFUNCTION() void HandleUnderAttack(AMissionObjective* Objective, AActor* Attacker) { ++UnderAttack; }

	int32 Failed = 0;
	int32 Checkpoints = 0;
	int32 Retries = 0;
	int32 UnderAttack = 0;
	int32 LastRetryPhase = -1;
	EMissionFailReason LastReason = EMissionFailReason::None;
	FName LastCheckpointName;
	FName LastRetryName;
};
