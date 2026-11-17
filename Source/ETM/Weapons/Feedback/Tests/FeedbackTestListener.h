// Counts impact feedback events for CannonFeedbackTest.cpp.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "FeedbackTestListener.generated.h"

UCLASS(Hidden, NotBlueprintable)
class UETMFeedbackTestListener : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION() void HandleImpact(FVector Location, bool bWeakPoint, bool bBlocked) { ++Impacts; LastLocation = Location; bLastWeakPoint = bWeakPoint; }

	int32 Impacts = 0;
	FVector LastLocation = FVector::ZeroVector;
	bool bLastWeakPoint = false;
};
