// The three save slots. Each carries a version; an unreadable or different-version slot loads as defaults.
// Owned by T-0023.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "Save/SaveTypes.h"
#include "ETMSaveGames.generated.h"

UCLASS()
class ETM_API UETMSettingsSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	static constexpr int32 CurrentVersion = 1;
	UPROPERTY() int32 Version = CurrentVersion;
	UPROPERTY() FETMSettings Settings;
};

UCLASS()
class ETM_API UETMProgressSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	static constexpr int32 CurrentVersion = 1;
	UPROPERTY() int32 Version = CurrentVersion;
	UPROPERTY() bool bHasCheckpoint = false;
	UPROPERTY() FETMSavedCheckpoint Saved;
};

UCLASS()
class ETM_API UETMResultsSaveGame : public USaveGame
{
	GENERATED_BODY()
public:
	static constexpr int32 CurrentVersion = 1;
	UPROPERTY() int32 Version = CurrentVersion;
	UPROPERTY() TMap<FName, FETMLevelRecord> Levels;
};
