// UETMInterruptionSubsystem: pauses the game for the menu, headset removal, tracking loss and system
// interruptions, and resumes through an explicit countdown.
// Spec: bible section 11 ("Immediate pause on tracking loss, headset removal, or system interruption.
// Resume through an explicit countdown after tracking returns. Recenter accessible from pause."),
// section 6 ("Pause and headset removal stop gameplay timers and enemy damage"). Owned by T-0030.
//
// Pausing uses the engine's world pause, so every gameplay tick stops at once: enemies and their attack
// timers, the encounter director, cannons and heat, the brace, shot feedback and the HUD. Nothing is
// destroyed or reset, so resuming continues exactly where play stopped. This subsystem keeps ticking
// while paused to watch for the interruption ending and to run the countdown.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "InterruptionSubsystem.generated.h"

class APlayerState;
class AXRSeatedRigPawn;

/** Why the game is paused. Several can hold at once; play resumes only when all have cleared. */
UENUM(BlueprintType, meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EETMPauseReason : uint8
{
	None = 0,
	Menu = 1 << 0,
	HeadsetRemoved = 1 << 1,
	TrackingLost = 1 << 2,
	/** System menu, guardian, or another app took VR focus. */
	SystemFocus = 1 << 3,
	/** The app went to the background or the headset was lost. */
	Background = 1 << 4,
};
ENUM_CLASS_FLAGS(EETMPauseReason);

UENUM(BlueprintType)
enum class EETMPlayState : uint8
{
	Running,
	Paused,
	/** Every interruption has cleared; gameplay is still frozen while the countdown runs. */
	Countdown,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnETMPlayStateChanged, EETMPlayState, NewState);

UCLASS()
class ETM_API UETMInterruptionSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override { RETURN_QUICK_DECLARE_CYCLE_STAT(UETMInterruptionSubsystem, STATGROUP_Tickables); }

	// ---- Requests ----
	/** Menu button: opens the pause, or closes it (which starts the countdown) if it is already open. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Pause") void ToggleMenuPause();
	UFUNCTION(BlueprintCallable, Category = "ETM|Pause") void OpenPauseMenu() { AddReason(EETMPauseReason::Menu); }
	/** The pause menu's Resume button. Starts the countdown once nothing else is holding the pause. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Pause") void ResumeFromMenu() { RemoveReason(EETMPauseReason::Menu); }
	/** Pause-menu recenter and Forward re-anchor (P-11). Refused unless the game is paused. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Pause") bool RequestRecenter();
	UFUNCTION(BlueprintCallable, Category = "ETM|Pause") bool RequestForwardReAnchor();

	/** For platform hooks and tests: raise or clear one interruption. */
	void AddReason(EETMPauseReason Reason);
	void RemoveReason(EETMPauseReason Reason);
	/**
	 * Controller tracking watch, called every frame from Tick with live data; public so tests can drive it.
	 * Only active once the headset is on and both controllers have been seen tracked; a loss shorter than
	 * the grace time is ignored so a one-frame dropout does not pause the game.
	 */
	void UpdateTrackingWatch(bool bHeadsetActive, bool bLeftTracked, bool bRightTracked, float DeltaTime);

	// ---- State ----
	UFUNCTION(BlueprintPure, Category = "ETM|Pause") EETMPlayState GetPlayState() const { return PlayState; }
	UFUNCTION(BlueprintPure, Category = "ETM|Pause") bool IsGameplayPaused() const { return PlayState != EETMPlayState::Running; }
	UFUNCTION(BlueprintPure, Category = "ETM|Pause") bool HasReason(EETMPauseReason Reason) const { return EnumHasAnyFlags(Reasons, Reason); }
	UFUNCTION(BlueprintPure, Category = "ETM|Pause") float GetCountdownRemaining() const { return PlayState == EETMPlayState::Countdown ? CountdownRemaining : 0.f; }

	/** Length of the resume countdown. Prototype value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Pause") float CountdownSeconds = 3.f;
	/** A controller must be lost for this long before the game pauses. Prototype value. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Pause") float TrackingLossGraceSeconds = 0.3f;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Pause") FOnETMPlayStateChanged OnPlayStateChanged;

private:
	void SetPlayState(EETMPlayState NewState);
	void ApplyWorldPause(bool bPause);
	AXRSeatedRigPawn* FindRig() const;
	void BindPlatformEvents();
	void UnbindPlatformEvents();
	void BindMenuButton();
	UFUNCTION() void HandleMenuButton();

	UPROPERTY(Transient) TObjectPtr<APlayerState> Pauser;
	UPROPERTY(Transient) TWeakObjectPtr<class ARigPlayerController> BoundController;

	TArray<FDelegateHandle> PlatformHandles;
	EETMPauseReason Reasons = EETMPauseReason::None;
	EETMPlayState PlayState = EETMPlayState::Running;
	float CountdownRemaining = 0.f;
	float TrackingLostFor = 0.f;
	bool bTrackingEstablished = false;
	float BindTimer = 0.f;
};
