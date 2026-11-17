#include "Input/RigPlayerController.h"
#include "Input/RigInputSaveGame.h"
#include "Save/ETMSaveStore.h"
#include "Player/XRSeatedRigPawn.h"
#include "ETM.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"

ARigPlayerController::ARigPlayerController()
{
	PrimaryActorTick.bCanEverTick = true;
}

UInputAction* ARigPlayerController::MakeAction(const TCHAR* Name, bool bAnalog)
{
	UInputAction* Action = NewObject<UInputAction>(this, FName(Name));
	Action->ValueType = bAnalog ? EInputActionValueType::Axis1D : EInputActionValueType::Boolean;
	return Action;
}

const UInputMappingContext* ARigPlayerController::GetOrBuildMappingContext()
{
	if (MappingContext) return MappingContext;

	MappingContext = NewObject<UInputMappingContext>(this, TEXT("IMC_Rig"));

	// Quest 2 Touch controllers through the OpenXR Oculus Touch profile.
	// The thumbstick axes are not mapped: both sticks are unassigned in combat (P-11).
	const auto Map = [this](TObjectPtr<UInputAction>& Slot, const TCHAR* Name, bool bAnalog, const FKey& Key)
	{
		Slot = MakeAction(Name, bAnalog);
		MappingContext->MapKey(Slot, Key);
	};
	Map(LeftTriggerAction,     TEXT("IA_LeftTrigger"),     true,  EKeys::OculusTouch_Left_Trigger_Axis);
	Map(RightTriggerAction,    TEXT("IA_RightTrigger"),    true,  EKeys::OculusTouch_Right_Trigger_Axis);
	Map(LeftGripAction,        TEXT("IA_LeftGrip"),        true,  EKeys::OculusTouch_Left_Grip_Axis);
	Map(RightGripAction,       TEXT("IA_RightGrip"),       true,  EKeys::OculusTouch_Right_Grip_Axis);
	Map(LeftPrimaryAction,     TEXT("IA_LeftPrimary"),     false, EKeys::OculusTouch_Left_X_Click);
	Map(RightPrimaryAction,    TEXT("IA_RightPrimary"),    false, EKeys::OculusTouch_Right_A_Click);
	Map(LeftSecondaryAction,   TEXT("IA_LeftSecondary"),   false, EKeys::OculusTouch_Left_Y_Click);
	Map(RightSecondaryAction,  TEXT("IA_RightSecondary"),  false, EKeys::OculusTouch_Right_B_Click);
	Map(LeftStickClickAction,  TEXT("IA_LeftStickClick"),  false, EKeys::OculusTouch_Left_Thumbstick_Click);
	Map(RightStickClickAction, TEXT("IA_RightStickClick"), false, EKeys::OculusTouch_Right_Thumbstick_Click);
	Map(MenuAction,            TEXT("IA_Menu"),            false, EKeys::OculusTouch_Left_Menu_Click);
	return MappingContext;
}

void ARigPlayerController::BeginPlay()
{
	Super::BeginPlay();

	LoadSavedScheme();

	if (const ULocalPlayer* LocalPlayer = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Subsystem->AddMappingContext(GetOrBuildMappingContext(), 0);
		}
	}
}

void ARigPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	LogVentStats();
	Super::EndPlay(EndPlayReason);
}

ETMInput::FRawInput ARigPlayerController::ReadRawInput() const
{
	ETMInput::FRawInput Raw;
	const ULocalPlayer* LocalPlayer = GetLocalPlayer();
	const UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	const UEnhancedPlayerInput* Input = Subsystem ? Subsystem->GetPlayerInput() : nullptr;
	if (!Input || !MappingContext) return Raw;

	const auto Axis = [Input](const UInputAction* Action) { return Input->GetActionValue(Action).Get<float>(); };
	const auto Button = [Input](const UInputAction* Action) { return Input->GetActionValue(Action).Get<bool>(); };

	Raw.LeftTrigger = Axis(LeftTriggerAction);
	Raw.RightTrigger = Axis(RightTriggerAction);
	Raw.LeftGrip = Axis(LeftGripAction);
	Raw.RightGrip = Axis(RightGripAction);
	Raw.bLeftPrimary = Button(LeftPrimaryAction);
	Raw.bRightPrimary = Button(RightPrimaryAction);
	Raw.bLeftSecondary = Button(LeftSecondaryAction);
	Raw.bRightSecondary = Button(RightSecondaryAction);
	Raw.bLeftStickClick = Button(LeftStickClickAction);
	Raw.bRightStickClick = Button(RightStickClickAction);
	Raw.bMenu = Button(MenuAction);
	return Raw;
}

void ARigPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	ProcessRawInput(ReadRawInput(), DeltaTime);
}

void ARigPlayerController::ProcessRawInput(const ETMInput::FRawInput& Raw, float DeltaTime)
{
	const ETMInput::FCommands Commands = Router.Step(Raw, DeltaTime);

	if (AXRSeatedRigPawn* Rig = Cast<AXRSeatedRigPawn>(GetPawn()))
	{
		Rig->SetTrigger(true, Commands.bFireLeft);
		Rig->SetTrigger(false, Commands.bFireRight);
		Rig->SetVent(true, Commands.bVentLeft);
		Rig->SetVent(false, Commands.bVentRight);
		if (Commands.bBracePressed)
		{
			Rig->ActivateBrace();
		}
	}
	if (Commands.bConfirmPressed) OnConfirmInteraction.Broadcast();
	if (Commands.bPausePressed) OnPauseRequested.Broadcast();
}

void ARigPlayerController::SetControlScheme(ERigControlScheme NewScheme, bool bSave)
{
	if (NewScheme != Router.GetScheme())
	{
		Router.SetScheme(NewScheme);
		OnControlSchemeChanged.Broadcast(NewScheme);
	}
	if (bSave)
	{
		URigInputSaveGame* Save = NewObject<URigInputSaveGame>();
		Save->ControlScheme = NewScheme;
		// Checked format with a backup copy: a damaged file can make the engine's own loader crash (T-0052).
		if (!UETMSaveStore::SaveChecked(Save, URigInputSaveGame::SlotName()))
		{
			UE_LOG(LogETM, Warning, TEXT("Could not write input save slot '%s'."), URigInputSaveGame::SlotName());
		}
	}
}

void ARigPlayerController::LoadSavedScheme()
{
	const URigInputSaveGame* Save = Cast<URigInputSaveGame>(UETMSaveStore::LoadChecked(URigInputSaveGame::SlotName()));
	if (Save && Save->Version == URigInputSaveGame::CurrentVersion)
	{
		SetControlScheme(Save->ControlScheme, /*bSave*/ false);
	}
}

void ARigPlayerController::LogVentStats() const
{
	for (const ERigControlScheme Scheme : { ERigControlScheme::GripVent, ERigControlScheme::ButtonVent })
	{
		const FRigVentStats& S = Router.GetStats(Scheme);
		if (S.PlaySeconds <= 0.f) continue;
		UE_LOG(LogETM, Display,
			TEXT("VentStats scheme=%s seconds=%.1f vents=%d whileFiring=%d short=%d suspect=%d suspectPerMinute=%.2f suspectFraction=%.2f"),
			Scheme == ERigControlScheme::GripVent ? TEXT("GripVent") : TEXT("ButtonVent"),
			S.PlaySeconds, S.VentActivations, S.VentsWhileFiring, S.ShortVents, S.SuspectVents,
			S.GetSuspectVentsPerMinute(), S.GetSuspectFraction());
	}
}
