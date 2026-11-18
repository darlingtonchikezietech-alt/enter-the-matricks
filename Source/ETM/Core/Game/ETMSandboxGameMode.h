// AETMSandboxGameMode: the project's default game mode until the campaign flow (T-0031) and real arenas
// (T-0020, T-0027) exist. Owned by T-0053.
//
// Always: the player is RIG-6 (AXRSeatedRigPawn) driven by ARigPlayerController, and the rig is registered
// with the mission so failure and retry work.
// On an empty map (the engine's Entry map, which the project boots into): it also builds a test range in
// code, so a packaged build or VR Preview over Link has something to stand in and shoot at:
//   - a lit 60 m floor with distance posts at 5, 15, 30 and 45 m down three lanes (judge scale and tracking);
//   - the objective in front of and below the rig (P-22);
//   - three lane entrances 25 m out and a long practice encounter of Shardlings and Needle Drones;
//   - automatic retry 3 s after a failure, since there is no menu yet.
// No .umap is needed. Other maps and automated tests never get the range unless BuildSandbox is called.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Objectives/MissionTypes.h"
#include "ETMSandboxGameMode.generated.h"

class AEncounterDirector;
class AMissionObjective;

UCLASS()
class ETM_API AETMSandboxGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AETMSandboxGameMode();

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;

	/** Builds the test range in the current world. Runs by itself on the maps in SandboxMaps. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Sandbox") void BuildSandbox();
	/** True if this world should get the range automatically. */
	UFUNCTION(BlueprintPure, Category = "ETM|Sandbox") bool ShouldBuildSandbox() const;

	UFUNCTION(BlueprintPure, Category = "ETM|Sandbox") AEncounterDirector* GetSandboxDirector() const { return Director; }
	UFUNCTION(BlueprintPure, Category = "ETM|Sandbox") AMissionObjective* GetSandboxObjective() const { return Objective; }
	/** Everything the range spawned, for tests and for cleaning up. */
	const TArray<TObjectPtr<AActor>>& GetSandboxActors() const { return SandboxActors; }

	/** Map names (without path) that get the range automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Sandbox") TArray<FString> SandboxMaps = { TEXT("Entry") };
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Sandbox") bool bBuildSandbox = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Sandbox") float AutoRetrySeconds = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Sandbox") int32 EncounterSeed = 2026;

protected:
	/** Registers a player's rig health and brace with the mission state. */
	void RegisterRig(AController* Controller);

private:
	template <typename T>
	T* SpawnSandboxActor(const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator);
	AActor* SpawnMesh(const TCHAR* Shape, const FVector& Location, const FVector& SizeCm, const FRotator& Rotation = FRotator::ZeroRotator);

	UFUNCTION() void HandleMissionFailed(EMissionFailReason Reason);
	void DoRetry();

	UPROPERTY(Transient) TObjectPtr<AEncounterDirector> Director;
	UPROPERTY(Transient) TObjectPtr<AMissionObjective> Objective;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> SandboxActors;
	FTimerHandle RetryTimer;
	bool bBuilt = false;
};
