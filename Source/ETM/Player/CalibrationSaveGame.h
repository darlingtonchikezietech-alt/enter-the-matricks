// Per-player seated calibration that persists between sessions: the controller-to-cannon offsets.
// The seat position itself is not saved, because the headset's tracking space changes between sessions.
// Spec: bible section 4. Owned by T-0007. The general save system (T-0023) may later absorb this slot.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "CalibrationSaveGame.generated.h"

UCLASS()
class ETM_API UETMCalibrationSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static const TCHAR* SlotName() { return TEXT("ETM_Calibration"); }
	static constexpr int32 UserIndex = 0;
	static constexpr int32 CurrentVersion = 1;

	UPROPERTY() int32 Version = CurrentVersion;
	UPROPERTY() FRotator LeftAimOffset = FRotator(-30.f, 0.f, 0.f);
	UPROPERTY() FRotator RightAimOffset = FRotator(-30.f, 0.f, 0.f);
};
