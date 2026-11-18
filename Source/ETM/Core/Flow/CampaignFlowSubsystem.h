// UETMCampaignFlowSubsystem: keeps the campaign flow alive across map loads, ticks it in real time, and
// loads maps for it. Owned by T-0031.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "CampaignFlowSubsystem.generated.h"

class UETMCampaignFlow;

UCLASS()
class ETM_API UETMCampaignFlowSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintPure, Category = "ETM|Flow") UETMCampaignFlow* GetFlow() const { return Flow; }

	/** The flow for this object's game instance, or null (editor worlds, tests without a game instance). */
	static UETMCampaignFlow* FindFlow(const UObject* WorldContext);

private:
	bool TickFlow(float DeltaSeconds);

	UPROPERTY(Transient) TObjectPtr<UETMCampaignFlow> Flow;
	FTSTicker::FDelegateHandle TickHandle;
};
