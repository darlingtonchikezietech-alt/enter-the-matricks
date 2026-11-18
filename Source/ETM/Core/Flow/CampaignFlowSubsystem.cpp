#include "Core/Flow/CampaignFlowSubsystem.h"
#include "Core/Flow/CampaignFlow.h"
#include "Save/ETMSaveSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"

void UETMCampaignFlowSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	UETMSaveSubsystem* Save = Collection.InitializeDependency<UETMSaveSubsystem>();

	Flow = NewObject<UETMCampaignFlow>(this);
	Flow->Store = Save ? Save->GetStore() : nullptr;
	Flow->Loader = [this](const FString& Map)
	{
		UWorld* World = GetGameInstance() ? GetGameInstance()->GetWorld() : nullptr;
		if (World) UGameplayStatics::OpenLevel(World, FName(*Map));
		else Flow->NotifyWorldReady(nullptr);
	};
	// Real time, not world time: fades keep running while a map loads or the world is paused.
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &UETMCampaignFlowSubsystem::TickFlow));
}

void UETMCampaignFlowSubsystem::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
	if (Flow) Flow->Loader = nullptr;
	Super::Deinitialize();
}

bool UETMCampaignFlowSubsystem::TickFlow(float DeltaSeconds)
{
	if (Flow) Flow->Tick(FMath::Min(DeltaSeconds, 0.1f)); // a long load hitch must not skip the fade-in
	return true;
}

UETMCampaignFlow* UETMCampaignFlowSubsystem::FindFlow(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* Instance = World ? World->GetGameInstance() : nullptr;
	const UETMCampaignFlowSubsystem* Subsystem = Instance ? Instance->GetSubsystem<UETMCampaignFlowSubsystem>() : nullptr;
	return Subsystem ? Subsystem->GetFlow() : nullptr;
}
