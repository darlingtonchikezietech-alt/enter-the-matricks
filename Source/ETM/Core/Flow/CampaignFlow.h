// UETMCampaignFlow: the campaign's state machine. Start -> Level 1 -> brief results -> relocation fade ->
// Level 2 -> ending -> campaign results -> title. Owned by T-0031.
//
// It lives on the game instance (UETMCampaignFlowSubsystem) so it survives map loads. It does not build
// anything itself: whoever owns a world (today the sandbox game mode) asks it which stage to show and tells
// it when a world is ready and when a level is won. Map loads go through Loader, so tests can run the whole
// loop without loading maps.
//
// It also tracks the run for results: shots, hits, weak-point hits, damage absorbed by the brace, retries
// and time (world time, so pauses do not count, bible section 8).
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Core/Flow/CampaignFlowTypes.h"
#include "Objectives/MissionTypes.h"
#include "Weapons/CannonComponent.h"
#include "CampaignFlow.generated.h"

class AXRSeatedRigPawn;
class UETMSaveStore;
class UMissionStateSubsystem;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnFlowStageChanged, EETMFlowStage, NewStage, int32, LevelIndex);

UCLASS(BlueprintType)
class ETM_API UETMCampaignFlow : public UObject
{
	GENERATED_BODY()

public:
	// --- Player choices (menu buttons). Each returns false when it does not apply to the current stage. ---
	UFUNCTION(BlueprintCallable, Category = "ETM|Flow") bool StartCampaign();
	UFUNCTION(BlueprintCallable, Category = "ETM|Flow") bool StartPractice();
	/** From the Level 1 results: relocate to Level 2. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Flow") bool Continue();
	/** Ends the ending early and shows the campaign results. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Flow") bool SkipEnding();
	/** From the campaign results or the practice range. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Flow") bool ReturnToTitle();

	// --- World owner calls ---
	/** A world has begun play. Ends the load part of a transition and starts the fade-in. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Flow") void NotifyWorldReady(UWorld* World);
	/** A level's encounter has started with this rig: start tracking the run. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Flow") void BeginLevel(UWorld* World, AXRSeatedRigPawn* Rig);
	/** The level's encounter is complete: record the result and move to results (L1) or the ending (L2). */
	UFUNCTION(BlueprintCallable, Category = "ETM|Flow") bool NotifyLevelComplete(UWorld* World);

	/** Advances fades and the ending timer. The subsystem calls it every frame in real time. */
	void Tick(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "ETM|Flow") EETMFlowStage GetStage() const { return Stage; }
	UFUNCTION(BlueprintPure, Category = "ETM|Flow") int32 GetLevelIndex() const { return LevelIndex; }
	UFUNCTION(BlueprintPure, Category = "ETM|Flow") EETMFlowStage GetTransitionTarget() const { return TransitionTarget; }
	/** 0 = clear view, 1 = fully black. */
	UFUNCTION(BlueprintPure, Category = "ETM|Flow") float GetFadeAlpha() const { return FadeAlpha; }
	UFUNCTION(BlueprintPure, Category = "ETM|Flow") bool IsTransitioning() const { return Stage == EETMFlowStage::Transition; }
	/** The stage a newly loaded world should build: the transition target while loading, else the stage. */
	UFUNCTION(BlueprintPure, Category = "ETM|Flow") EETMFlowStage GetStageToBuild() const { return Stage == EETMFlowStage::Transition ? TransitionTarget : Stage; }
	UFUNCTION(BlueprintPure, Category = "ETM|Flow") float GetEndingRemaining() const { return EndingRemaining; }

	/** Result of a level in the current campaign run (empty if not played yet). */
	UFUNCTION(BlueprintPure, Category = "ETM|Flow") FETMMissionResult GetLevelResult(int32 Index) const { return Results.IsValidIndex(Index) ? Results[Index] : FETMMissionResult(); }
	/** The run in progress (shots, hits... so far). */
	UFUNCTION(BlueprintPure, Category = "ETM|Flow") FETMMissionResult GetCurrentRun() const;
	UFUNCTION(BlueprintPure, Category = "ETM|Flow") int32 GetCampaignScore() const;

	/** Map loaded for each campaign level. Until the real arenas exist (T-0020, T-0027) both are the sandbox. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Flow") TArray<FString> LevelMaps = { TEXT("/Engine/Maps/Entry"), TEXT("/Engine/Maps/Entry") };
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Flow") FString TitleMap = TEXT("/Engine/Maps/Entry");
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Flow") FString PracticeMap = TEXT("/Engine/Maps/Entry");
	/** Length of the fade-out and of the fade-in (bible section 11: relocation through fade-out, load, fade-in). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Flow", meta = (ClampMin = "0.05")) float FadeSeconds = 0.75f;
	/** How long the ending holds before the campaign results appear by themselves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Flow", meta = (ClampMin = "0")) float EndingSeconds = 12.f;

	/** Where results go (the save subsystem's store). Optional. */
	UPROPERTY(Transient, BlueprintReadWrite, Category = "ETM|Flow") TObjectPtr<UETMSaveStore> Store;
	/** Loads a map. Unset: the load is skipped and the fade-in starts at once (tests). */
	TFunction<void(const FString& Map)> Loader;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Flow") FOnFlowStageChanged OnStageChanged;

private:
	void SetStage(EETMFlowStage NewStage);
	void BeginTransition(EETMFlowStage Target, int32 TargetLevel);
	FString MapFor(EETMFlowStage Target, int32 TargetLevel) const;
	void StopTracking();

	UFUNCTION() void HandleShot(const FCannonShot& Shot);
	UFUNCTION() void HandleBraceAbsorbed(float Absorbed, float Passed);
	UFUNCTION() void HandleRetry(const FMissionCheckpoint& Checkpoint);

	enum class EFade : uint8 { None, Out, Loading, In };

	EETMFlowStage Stage = EETMFlowStage::Title;
	EETMFlowStage TransitionTarget = EETMFlowStage::Title;
	int32 LevelIndex = 0;
	int32 TransitionLevel = 0;
	EFade Fade = EFade::None;
	float FadeAlpha = 0.f;
	float EndingRemaining = 0.f;

	UPROPERTY(Transient) TArray<FETMMissionResult> Results;
	UPROPERTY(Transient) FETMMissionResult Run;
	UPROPERTY(Transient) TWeakObjectPtr<UWorld> RunWorld;
	UPROPERTY(Transient) TWeakObjectPtr<AXRSeatedRigPawn> RunRig;
	UPROPERTY(Transient) TWeakObjectPtr<UMissionStateSubsystem> RunMission;
	double RunStartTime = 0.0;
	bool bTracking = false;
};
