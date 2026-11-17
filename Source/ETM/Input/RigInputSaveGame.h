// Persists the chosen control scheme between sessions.
// Owned by T-0012. The general save system (T-0023) may later absorb this slot.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Input/RigInputTypes.h"
#include "RigInputSaveGame.generated.h"

UCLASS()
class ETM_API URigInputSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	static const TCHAR* SlotName() { return TEXT("ETM_Input"); }
	static constexpr int32 UserIndex = 0;
	static constexpr int32 CurrentVersion = 1;

	UPROPERTY() int32 Version = CurrentVersion;
	UPROPERTY() ERigControlScheme ControlScheme = ERigControlScheme::GripVent;
};
