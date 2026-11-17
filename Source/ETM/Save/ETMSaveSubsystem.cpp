#include "Save/ETMSaveSubsystem.h"
#include "Save/ETMSaveStore.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"

void UETMSaveSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Store = NewObject<UETMSaveStore>(this);
	Store->LoadAll();
	WorldInitHandle = FWorldDelegates::OnPostWorldInitialization.AddUObject(this, &UETMSaveSubsystem::HandleWorldInitialized);
}

void UETMSaveSubsystem::Deinitialize()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(WorldInitHandle);
	Super::Deinitialize();
}

void UETMSaveSubsystem::HandleWorldInitialized(UWorld* World, const UWorld::InitializationValues Values)
{
	if (!World || !World->IsGameWorld() || World->GetGameInstance() != GetGameInstance()) return;
	// Wait for actors (rig, objective) to exist before applying settings.
	World->OnWorldBeginPlay.AddWeakLambda(this, [this, WeakWorld = TWeakObjectPtr<UWorld>(World)]
	{
		if (WeakWorld.IsValid()) HookWorld(WeakWorld.Get());
	});
}

void UETMSaveSubsystem::HookWorld(UWorld* World)
{
	if (!World || !Store) return;
	Store->ApplySettings(World);
	Store->BindToMission(World->GetSubsystem<UMissionStateSubsystem>());
}
