// UHUDPresenterComponent: the diegetic, event-driven HUD on RIG-6.
// Spec: bible section 11 "HUD hierarchy", section 13 (UHUDPresenterComponent), section 12 (weak points
// stay visible), section 11 accessibility (colour-independent cues). Owned by T-0019.
//
//   Left/right heat       -> a bar on each cannon housing near the muzzle; a lock bracket when overheated
//   Rig integrity         -> bar and number on the central lower frame
//   Brace readiness       -> small frame-mounted bar; full when ready
//   Objective integrity   -> world-space marker above the protected equipment; pulses while under attack
//   Enemy warning         -> marker with a countdown above the source; an edge arrow if the source is off-view
//   Boss vulnerability    -> bracket and countdown on the exposed weak point
//   Current objective     -> short frame-mounted line
//   Secondary text        -> one frame line, hidden while any critical warning is up
// Every cue changes shape or text, not only colour. Placeholder meshes and engine text until UI art (T-0034).
// Subtitles (T-0032) and the results panel (T-0031) are not here.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "Enemies/Base/EnemyTypes.h"
#include "UI/HudModel.h"
#include "HUDPresenterComponent.generated.h"

class AEnemyBase;
class AGatebreaker;
class AMissionObjective;
class AXRSeatedRigPawn;
class UStaticMesh;
class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS(ClassGroup = (ETM), meta = (BlueprintSpawnableComponent))
class ETM_API UHUDPresenterComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UHUDPresenterComponent();
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** The short objective line on the frame ("Keep the transfer system running"). */
	UFUNCTION(BlueprintCallable, Category = "ETM|HUD") void SetObjectiveLine(const FText& Line);
	/** Score popups and secondary narrative. Hidden while a critical warning is up (bible section 11). */
	UFUNCTION(BlueprintCallable, Category = "ETM|HUD") void ShowSecondaryMessage(const FText& Message, float Seconds = 3.f);
	/** Re-scans the world for enemies, bosses and the objective to listen to. Runs on its own every ScanSeconds. */
	UFUNCTION(BlueprintCallable, Category = "ETM|HUD") void RefreshSources();

	/** Advances timers and moves world-space markers. TickComponent calls this; public so tests can step time. */
	void AdvanceHud(float DeltaTime);

	// ---- Queries (tests, debugging, UI art hook-up) ----
	UFUNCTION(BlueprintPure, Category = "ETM|HUD") float GetShownHeat(bool bLeft) const { return bLeft ? LeftHeat : RightHeat; }
	UFUNCTION(BlueprintPure, Category = "ETM|HUD") bool IsHeatLockShown(bool bLeft) const;
	UFUNCTION(BlueprintPure, Category = "ETM|HUD") FText GetRigText() const;
	UFUNCTION(BlueprintPure, Category = "ETM|HUD") float GetBraceShown() const { return BraceShown; }
	UFUNCTION(BlueprintPure, Category = "ETM|HUD") FText GetObjectiveMarkerText() const;
	UFUNCTION(BlueprintPure, Category = "ETM|HUD") bool IsObjectiveAlarmShown() const { return bObjectiveAlarm; }
	UFUNCTION(BlueprintPure, Category = "ETM|HUD") int32 GetShownWarningCount() const;
	UFUNCTION(BlueprintPure, Category = "ETM|HUD") bool IsSecondaryMessageShown() const;
	UFUNCTION(BlueprintPure, Category = "ETM|HUD") int32 GetShownBossMarkerCount() const;
	/** World location of warning marker Index and whether its edge arrow is on. False if the slot is idle. */
	bool GetWarningMarker(int32 Index, FVector& OutLocation, bool& bOutEdgeCue, FString& OutText) const;
	bool GetBossMarker(int32 Index, FVector& OutLocation, FString& OutText) const;
	/** The frame-mounted elements, for the placement checks: rig bar, brace bar, objective line, secondary line. */
	TArray<USceneComponent*> GetFrameElements() const;

	// ---- Tuning ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|HUD", meta = (ClampMin = "1", ClampMax = "6")) int32 MaxShownWarnings = 3;
	/** Sources further than this from the view direction get a directional edge arrow. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|HUD") float EdgeCueHalfAngle = 50.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|HUD") float ScanSeconds = 0.5f;
	/** Frame placement, in rig space. Chosen for 1-2 m from the design eye and below the central sightline. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|HUD|Placement") FVector RigBarLocation = FVector(115.f, 0.f, 65.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|HUD|Placement") FVector BraceBarLocation = FVector(110.f, -38.f, 72.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|HUD|Placement") FVector ObjectiveLineLocation = FVector(125.f, 0.f, 82.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|HUD|Placement") FVector SecondaryLineLocation = FVector(125.f, 0.f, 50.f);
	/** Heat bar on the housing, in cannon space: near the muzzle, where the player is already looking. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|HUD|Placement") FVector HeatBarOnCannon = FVector(95.f, 0.f, 14.f);
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|HUD|Placement") float MarkerHeightAboveSource = 120.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|HUD|Assets") TObjectPtr<UStaticMesh> BarMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|HUD|Assets") TObjectPtr<UStaticMesh> ArrowMesh;

protected:
	virtual void BeginPlay() override;

private:
	struct FActiveWarning
	{
		TWeakObjectPtr<AEnemyBase> Source;
		float Remaining = 0.f;
		bool bTargetsRig = false;
	};
	struct FWarningSlot
	{
		TObjectPtr<UTextRenderComponent> Label = nullptr;
		TObjectPtr<UStaticMeshComponent> Edge = nullptr;
		TWeakObjectPtr<AEnemyBase> Source;
		bool bEdge = false;
	};
	struct FBossWindow
	{
		TWeakObjectPtr<AGatebreaker> Boss;
		FName Target;
		float Remaining = 0.f;
	};
	struct FBossSlot
	{
		TObjectPtr<UTextRenderComponent> Label = nullptr;
		bool bActive = false;
	};

	UTextRenderComponent* MakeText(const TCHAR* Name, USceneComponent* Parent, const FVector& Location, float Size, bool bWorldSpace);
	UStaticMeshComponent* MakeBar(const TCHAR* Name, USceneComponent* Parent, const FVector& Location, const FVector& Size, bool bWorldSpace = false);
	void SetBarFill(UStaticMeshComponent* Bar, const FVector& FullSize, float Fill) const;
	USceneComponent* BoxFor(const AGatebreaker* Boss, FName Target) const;

	UFUNCTION() void HandleLeftHeat(float HeatNormalized, bool bOverheated);
	UFUNCTION() void HandleRightHeat(float HeatNormalized, bool bOverheated);
	UFUNCTION() void HandleRigIntegrity(float Current, float Max, float Delta);
	UFUNCTION() void HandleObjectiveIntegrity(float Current, float Max, float Delta);
	UFUNCTION() void HandleAttackTelegraph(AEnemyBase* Enemy, float TelegraphSeconds, AActor* Target);
	UFUNCTION() void HandleEnemyStateChanged(AEnemyBase* Enemy, EEnemyState NewState, EEnemyState OldState);
	UFUNCTION() void HandleBossTargetExposed(FName Target, bool bExposed, float Seconds);

	UPROPERTY(Transient) TObjectPtr<AXRSeatedRigPawn> Rig;
	UPROPERTY(Transient) TWeakObjectPtr<AMissionObjective> Objective;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LeftHeatBar;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RightHeatBar;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> LeftLock;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RightLock;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RigBar;
	UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> RigLabel;
	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> BraceBar;
	UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> ObjectiveLine;
	UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> SecondaryLine;
	UPROPERTY(Transient) TObjectPtr<UTextRenderComponent> ObjectiveMarker;
	UPROPERTY(Transient) TArray<TObjectPtr<USceneComponent>> Owned;

	TSet<TWeakObjectPtr<AEnemyBase>> BoundEnemies;
	TSet<TWeakObjectPtr<AGatebreaker>> BoundBosses;
	TArray<FActiveWarning> Warnings;
	TArray<FWarningSlot> WarningSlots;
	TArray<FBossWindow> BossWindows;
	TArray<FBossSlot> BossSlots;
	ETMHud::FSecondaryLine Secondary;
	float LeftHeat = 0.f;
	float RightHeat = 0.f;
	float BraceShown = 1.f;
	float RigCurrent = 0.f;
	float RigMax = 0.f;
	float ObjectiveCurrent = 0.f;
	float ObjectiveMax = 0.f;
	float ScanTimer = 0.f;
	float PulseTime = 0.f;
	bool bObjectiveAlarm = false;
};
