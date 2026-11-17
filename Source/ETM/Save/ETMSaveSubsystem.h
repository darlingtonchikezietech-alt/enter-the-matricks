// UETMSaveSubsystem: the app-wide owner of the save store. Loads everything at startup, applies settings to
// each world as it begins play, and saves that world's checkpoints automatically. Owned by T-0023.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Engine/World.h"
#include "ETMSaveSubsystem.generated.h"

class UETMSaveStore;

UCLASS()
class ETM_API UETMSaveSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "ETM|Save") UETMSaveStore* GetStore() const { return Store; }

	/** Applies settings to a world and starts saving its checkpoints. Runs by itself when a world begins play. */
	void HookWorld(UWorld* World);

private:
	void HandleWorldInitialized(UWorld* World, const UWorld::InitializationValues Values);

	UPROPERTY(Transient) TObjectPtr<UETMSaveStore> Store;
	FDelegateHandle WorldInitHandle;
};
