#include "Platform/InterruptionSubsystem.h"
#include "Input/RigPlayerController.h"
#include "Player/XRSeatedRigPawn.h"
#include "ETM.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "HeadMountedDisplayFunctionLibrary.h"
#include "Misc/App.h"
#include "Misc/CoreDelegates.h"
#include "MotionControllerComponent.h"

void UETMInterruptionSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	BindPlatformEvents();
}

void UETMInterruptionSubsystem::Deinitialize()
{
	UnbindPlatformEvents();
	Super::Deinitialize();
}

void UETMInterruptionSubsystem::BindPlatformEvents()
{
	// Events raised by the engine's OpenXR plugin and the platform layer. They can arrive outside the
	// game thread's normal flow but always on the game thread, so they only flip flags here.
	const auto Add = [this](EETMPauseReason Reason) { return [this, Reason] { AddReason(Reason); }; };
	const auto Remove = [this](EETMPauseReason Reason) { return [this, Reason] { RemoveReason(Reason); }; };
	PlatformHandles.Add(FCoreDelegates::VRHeadsetRemovedFromHead.AddWeakLambda(this, Add(EETMPauseReason::HeadsetRemoved)));
	PlatformHandles.Add(FCoreDelegates::VRHeadsetPutOnHead.AddWeakLambda(this, Remove(EETMPauseReason::HeadsetRemoved)));
	PlatformHandles.Add(FCoreDelegates::VRHeadsetLost.AddWeakLambda(this, Add(EETMPauseReason::Background)));
	PlatformHandles.Add(FCoreDelegates::VRHeadsetReconnected.AddWeakLambda(this, Remove(EETMPauseReason::Background)));
	PlatformHandles.Add(FCoreDelegates::ApplicationWillDeactivateDelegate.AddWeakLambda(this, Add(EETMPauseReason::Background)));
	PlatformHandles.Add(FCoreDelegates::ApplicationHasReactivatedDelegate.AddWeakLambda(this, Remove(EETMPauseReason::Background)));
	PlatformHandles.Add(FCoreDelegates::ApplicationWillEnterBackgroundDelegate.AddWeakLambda(this, Add(EETMPauseReason::Background)));
	PlatformHandles.Add(FCoreDelegates::ApplicationHasEnteredForegroundDelegate.AddWeakLambda(this, Remove(EETMPauseReason::Background)));
}

void UETMInterruptionSubsystem::UnbindPlatformEvents()
{
	FCoreDelegates::VRHeadsetRemovedFromHead.RemoveAll(this);
	FCoreDelegates::VRHeadsetPutOnHead.RemoveAll(this);
	FCoreDelegates::VRHeadsetLost.RemoveAll(this);
	FCoreDelegates::VRHeadsetReconnected.RemoveAll(this);
	FCoreDelegates::ApplicationWillDeactivateDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationHasReactivatedDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationWillEnterBackgroundDelegate.RemoveAll(this);
	FCoreDelegates::ApplicationHasEnteredForegroundDelegate.RemoveAll(this);
	PlatformHandles.Reset();
}

// ---------------------------------------------------------------------------------------------
// Reasons and state
// ---------------------------------------------------------------------------------------------

void UETMInterruptionSubsystem::AddReason(EETMPauseReason Reason)
{
	if (Reason == EETMPauseReason::None) return;
	const bool bWasClear = Reasons == EETMPauseReason::None;
	Reasons |= Reason;
	// Immediate pause (bible section 11). An interruption during the countdown cancels it.
	if (bWasClear || PlayState == EETMPlayState::Countdown)
	{
		UE_LOG(LogETM, Display, TEXT("Paused: reason %d"), static_cast<int32>(Reason));
		SetPlayState(EETMPlayState::Paused);
	}
}

void UETMInterruptionSubsystem::RemoveReason(EETMPauseReason Reason)
{
	if (!EnumHasAnyFlags(Reasons, Reason)) return;
	Reasons &= ~Reason;
	if (Reasons == EETMPauseReason::None && PlayState == EETMPlayState::Paused)
	{
		// Never straight back into combat: an explicit countdown first (bible section 11).
		CountdownRemaining = CountdownSeconds;
		SetPlayState(EETMPlayState::Countdown);
	}
}

void UETMInterruptionSubsystem::ToggleMenuPause()
{
	if (HasReason(EETMPauseReason::Menu)) ResumeFromMenu(); else OpenPauseMenu();
}

void UETMInterruptionSubsystem::SetPlayState(EETMPlayState NewState)
{
	if (NewState == PlayState) return;
	PlayState = NewState;
	ApplyWorldPause(NewState != EETMPlayState::Running);
	OnPlayStateChanged.Broadcast(NewState);
}

void UETMInterruptionSubsystem::ApplyWorldPause(bool bPause)
{
	UWorld* World = GetWorld();
	AWorldSettings* Settings = World ? World->GetWorldSettings() : nullptr;
	if (!Settings) return;

	// The engine's world pause: actors and components stop ticking unless they opt in, so all gameplay
	// timers and attack countdowns freeze together. A dedicated player state is the "pauser", exactly as
	// the game mode does for a pausing player, so it works with or without a game mode.
	if (bPause)
	{
		if (!Pauser)
		{
			FActorSpawnParameters Params;
			Params.ObjectFlags |= RF_Transient;
			Pauser = World->SpawnActor<APlayerState>(Params);
		}
		Settings->SetPauserPlayerState(Pauser);
	}
	else if (Settings->GetPauserPlayerState() == Pauser)
	{
		Settings->SetPauserPlayerState(nullptr);
	}
}

// ---------------------------------------------------------------------------------------------
// Per-frame watch (runs while paused too)
// ---------------------------------------------------------------------------------------------

AXRSeatedRigPawn* UETMInterruptionSubsystem::FindRig() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	for (TActorIterator<AXRSeatedRigPawn> It(World); It; ++It) return *It;
	return nullptr;
}

void UETMInterruptionSubsystem::BindMenuButton()
{
	if (BoundController.IsValid() || !GetWorld()) return;
	for (TActorIterator<ARigPlayerController> It(GetWorld()); It; ++It)
	{
		It->OnPauseRequested.AddUniqueDynamic(this, &UETMInterruptionSubsystem::HandleMenuButton);
		BoundController = *It;
		return;
	}
}

void UETMInterruptionSubsystem::HandleMenuButton()
{
	ToggleMenuPause();
}

void UETMInterruptionSubsystem::UpdateTrackingWatch(bool bHeadsetActive, bool bLeftTracked, bool bRightTracked, float DeltaTime)
{
	if (!bHeadsetActive)
	{
		// No headset (editor, desktop preview): nothing to watch, and nothing left raised by an old loss.
		TrackingLostFor = 0.f;
		RemoveReason(EETMPauseReason::TrackingLost);
		return;
	}
	const bool bBoth = bLeftTracked && bRightTracked;
	if (!bTrackingEstablished)
	{
		bTrackingEstablished = bBoth; // ignore the start-up frames before the controllers are found
		return;
	}
	if (bBoth)
	{
		TrackingLostFor = 0.f;
		RemoveReason(EETMPauseReason::TrackingLost);
		return;
	}
	// Losing either controller loses a cannon, so either one counts.
	TrackingLostFor += DeltaTime;
	if (TrackingLostFor >= TrackingLossGraceSeconds) AddReason(EETMPauseReason::TrackingLost);
}

void UETMInterruptionSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	BindTimer -= DeltaTime;
	if (BindTimer <= 0.f)
	{
		BindTimer = 0.5f;
		BindMenuButton();
	}

	// Live platform state.
	const bool bHeadsetActive = UHeadMountedDisplayFunctionLibrary::IsHeadMountedDisplayEnabled();
	if (bHeadsetActive)
	{
		// The OpenXR session drops focus for the system menu, the guardian, or the headset going idle.
		if (FApp::HasVRFocus()) RemoveReason(EETMPauseReason::SystemFocus); else AddReason(EETMPauseReason::SystemFocus);
	}
	const AXRSeatedRigPawn* Rig = FindRig();
	UpdateTrackingWatch(bHeadsetActive && Rig,
		Rig && Rig->LeftController && Rig->LeftController->IsTracked(),
		Rig && Rig->RightController && Rig->RightController->IsTracked(), DeltaTime);

	if (PlayState == EETMPlayState::Countdown)
	{
		CountdownRemaining -= DeltaTime;
		if (CountdownRemaining <= 0.f)
		{
			CountdownRemaining = 0.f;
			SetPlayState(EETMPlayState::Running);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Pause-menu actions
// ---------------------------------------------------------------------------------------------

bool UETMInterruptionSubsystem::RequestRecenter()
{
	if (!IsGameplayPaused()) return false;
	AXRSeatedRigPawn* Rig = FindRig();
	return Rig && Rig->Recenter();
}

bool UETMInterruptionSubsystem::RequestForwardReAnchor()
{
	if (!IsGameplayPaused()) return false;
	AXRSeatedRigPawn* Rig = FindRig();
	return Rig && Rig->ForwardReAnchor();
}
