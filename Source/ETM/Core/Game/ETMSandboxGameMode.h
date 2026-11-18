// AETMSandboxGameMode: the project's default game mode until real arenas (T-0020, T-0027) exist.
// Owned by T-0053; campaign stages added by T-0031.
//
// Always: the player is RIG-6 (AXRSeatedRigPawn) driven by ARigPlayerController, and the rig is registered
// with the mission so failure and retry work.
// On an empty map (the engine's Entry map, which the project boots into) it builds a range in code, so a
// packaged build or VR Preview over Link has something to stand in and shoot at: a lit 60 m floor with
// distance posts at 5, 15, 30 and 45 m down three lanes, and lane entrances 18 m out. What else it builds
// depends on the campaign flow's stage (UETMCampaignFlow, T-0031):
//   - Title: the title panel (start the campaign or the practice range). No enemies.
//   - Level 1 / Level 2: the objective in front of and below the rig (P-22) and a short stand-in encounter
//     that completes when its waves are cleared. Completion is reported to the flow, which shows the brief
//     results (Level 1) or the ending and campaign results (Level 2) on panels in the same world.
//   - Practice (or no flow at all, as in tests): the long practice encounter from T-0053.
// Fades are done by dimming the range's only light: the Entry map has no sky, so the view goes fully black.
// The rig is moved to the range's origin, on a 1 m stabilizer mount, so the player looks down over the
// objective into all three lanes (P-22; owner headset tests, T-0031). The Entry map's own player start is
// ignored: it put the rig at an arbitrary height.
// Failure retries by itself after 3 s (no failure screen yet).
// No .umap is needed. Other maps and automated tests never get the range unless BuildSandbox is called.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Core/Flow/CampaignFlowTypes.h"
#include "Encounters/EncounterTypes.h"
#include "Enemies/Base/EnemyBase.h" // FEncounterSpawnGroup holds a TSubclassOf<AEnemyBase>
#include "Objectives/MissionTypes.h"
#include "ETMSandboxGameMode.generated.h"

class ADirectionalLight;
class AEncounterDirector;
class AETMMenuPanel;
class AMissionObjective;
class UETMCampaignFlow;

UCLASS()
class ETM_API AETMSandboxGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AETMSandboxGameMode();

	virtual void StartPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Builds the range for the flow's current stage. Runs by itself on the maps in SandboxMaps. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Sandbox") void BuildSandbox();
	/** True if this world should get the range automatically. */
	UFUNCTION(BlueprintPure, Category = "ETM|Sandbox") bool ShouldBuildSandbox() const;

	/** The campaign flow this world follows. Found on the game instance at StartPlay; tests set their own. Null = practice only. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Sandbox") void SetFlow(UETMCampaignFlow* InFlow);
	UFUNCTION(BlueprintPure, Category = "ETM|Sandbox") UETMCampaignFlow* GetFlow() const { return Flow; }

	UFUNCTION(BlueprintPure, Category = "ETM|Sandbox") AEncounterDirector* GetSandboxDirector() const { return Director; }
	UFUNCTION(BlueprintPure, Category = "ETM|Sandbox") AMissionObjective* GetSandboxObjective() const { return Objective; }
	/** The menu or results panel on show, if any. */
	UFUNCTION(BlueprintPure, Category = "ETM|Sandbox") AETMMenuPanel* GetPanel() const { return Panel; }
	/** Light intensity after the fade is applied. */
	UFUNCTION(BlueprintPure, Category = "ETM|Sandbox") float GetCurrentLightIntensity() const;
	/** Everything the range spawned, for tests and for cleaning up. */
	const TArray<TObjectPtr<AActor>>& GetSandboxActors() const { return SandboxActors; }

	/** The stand-in encounter for a campaign level (0 or 1). Short, and it completes when cleared. */
	static TArray<FEncounterPhase> MakeLevelPhases(int32 LevelIndex);

	/** Map names (without path) that get the range automatically. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Sandbox") TArray<FString> SandboxMaps = { TEXT("Entry") };
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Sandbox") bool bBuildSandbox = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Sandbox") float AutoRetrySeconds = 3.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Sandbox") int32 EncounterSeed = 2026;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Sandbox") float LightIntensity = 6.f;
	/** Height of the rig's base above the range floor. The objective's bulk stays well below the eye (P-22). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Sandbox") float RigBaseHeight = 100.f;
	/** Where panels stand, in the rig's own frame: in front, at the cannons' height so they can be aimed at level (bible section 11: 1-2 m). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Sandbox") FVector PanelLocation = FVector(190.f, 0.f, 105.f);

protected:
	/** Registers a player's rig health and brace with the mission state. */
	void RegisterRig(AController* Controller);

private:
	template <typename T>
	T* SpawnSandboxActor(const FVector& Location, const FRotator& Rotation = FRotator::ZeroRotator);
	AActor* SpawnMesh(const TCHAR* Shape, const FVector& Location, const FVector& SizeCm, const FRotator& Rotation = FRotator::ZeroRotator);

	void BuildRange();
	void BuildObjective();
	void StartEncounter(const TArray<FEncounterPhase>& Phases);
	void ShowPanel(EETMFlowStage Stage);
	APawn* FindRigPawn() const;
	/** Puts the rig at the range's origin on its mount (RigBaseHeight), facing down the centre lane. */
	void PlaceRigOnRange(APawn* Rig) const;

	UFUNCTION() void HandleMissionFailed(EMissionFailReason Reason);
	UFUNCTION() void HandleEncounterComplete();
	UFUNCTION() void HandleStageChanged(EETMFlowStage NewStage, int32 LevelIndex);
	UFUNCTION() void HandlePanelButton(FName ButtonId);
	void DoRetry();

	UPROPERTY(Transient) TObjectPtr<UETMCampaignFlow> Flow;
	UPROPERTY(Transient) TObjectPtr<AEncounterDirector> Director;
	UPROPERTY(Transient) TObjectPtr<AMissionObjective> Objective;
	UPROPERTY(Transient) TObjectPtr<ADirectionalLight> Sun;
	UPROPERTY(Transient) TObjectPtr<AETMMenuPanel> Panel;
	UPROPERTY(Transient) TArray<TObjectPtr<AActor>> SandboxActors;
	FTimerHandle RetryTimer;
	bool bBuilt = false;
};
