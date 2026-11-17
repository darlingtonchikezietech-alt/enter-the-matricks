// Automation tests for the input router (both control schemes, P-12 grip gate, vent statistics)
// and for ARigPlayerController's mapping context and scheme switching.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Input; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Input/RigInputModel.h"
#include "Input/RigInputSaveGame.h"
#include "Input/RigPlayerController.h"
#include "InputMappingContext.h"
#include "InputAction.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const EAutomationTestFlags ETMInputTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;
	const float Frame72 = 1.f / 72.f;

	/** Holds one raw state for a duration and returns the last frame's commands. */
	ETMInput::FCommands Hold(ETMInput::FRouter& Router, const ETMInput::FRawInput& Raw, float Seconds)
	{
		ETMInput::FCommands Last;
		for (float T = 0.f; T < Seconds - KINDA_SMALL_NUMBER; T += Frame72)
		{
			Last = Router.Step(Raw, Frame72);
		}
		return Last;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMGripGateTest, "ETM.Input.Router.GripGate", ETMInputTestFlags)
bool FETMGripGateTest::RunTest(const FString&)
{
	ETMInput::FRouter Router;
	TestEqual(TEXT("Spec threshold"), Router.Tuning.GripVentThreshold, 0.85f);
	TestEqual(TEXT("Spec hold time"), Router.Tuning.GripVentHoldSeconds, 0.1f);

	// A relaxed hold on the handle (0.6) never vents, however long it lasts.
	ETMInput::FRawInput Raw;
	Raw.LeftGrip = 0.6f;
	TestFalse(TEXT("Resting grip does not vent"), Hold(Router, Raw, 5.f).bVentLeft);

	// A hard squeeze shorter than 100 ms does not vent.
	Raw.LeftGrip = 1.f;
	TestFalse(TEXT("Squeeze under 100 ms does not vent"), Hold(Router, Raw, 0.08f).bVentLeft);
	Raw.LeftGrip = 0.f;
	Router.Step(Raw, Frame72);

	// The hold time restarts after a release: two short squeezes do not add up.
	Raw.LeftGrip = 1.f;
	TestFalse(TEXT("Short squeezes do not add up"), Hold(Router, Raw, 0.08f).bVentLeft);

	// A deliberate squeeze vents, and only on that hand.
	const ETMInput::FCommands Squeezed = Hold(Router, Raw, 0.1f);
	TestTrue(TEXT("Squeeze past 100 ms vents"), Squeezed.bVentLeft);
	TestFalse(TEXT("The other cannon does not vent"), Squeezed.bVentRight);

	// Easing off slightly keeps it open; letting go closes it.
	Raw.LeftGrip = 0.8f;
	TestTrue(TEXT("Small relax keeps the vent open"), Hold(Router, Raw, 0.2f).bVentLeft);
	Raw.LeftGrip = 0.5f;
	TestFalse(TEXT("Release closes the vent"), Router.Step(Raw, Frame72).bVentLeft);

	// 0.84 is below the threshold.
	Raw.LeftGrip = 0.84f;
	TestFalse(TEXT("Just under the threshold does not vent"), Hold(Router, Raw, 1.f).bVentLeft);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMRouterGripSchemeTest, "ETM.Input.Router.GripVentScheme", ETMInputTestFlags)
bool FETMRouterGripSchemeTest::RunTest(const FString&)
{
	ETMInput::FRouter Router;
	TestEqual(TEXT("Grip scheme is the default"), Router.GetScheme(), ERigControlScheme::GripVent);

	ETMInput::FRawInput Raw;
	Raw.RightTrigger = 1.f;
	ETMInput::FCommands C = Router.Step(Raw, Frame72);
	TestTrue(TEXT("Right trigger fires the right cannon"), C.bFireRight);
	TestFalse(TEXT("Right trigger does not fire the left cannon"), C.bFireLeft);

	// Trigger hysteresis: 0.45 keeps firing, 0.3 stops, 0.45 does not restart.
	Raw.RightTrigger = 0.45f;
	TestTrue(TEXT("Trigger stays held above the release point"), Router.Step(Raw, Frame72).bFireRight);
	Raw.RightTrigger = 0.3f;
	TestFalse(TEXT("Trigger releases"), Router.Step(Raw, Frame72).bFireRight);
	Raw.RightTrigger = 0.45f;
	TestFalse(TEXT("Trigger does not re-press below the press point"), Router.Step(Raw, Frame72).bFireRight);
	Raw.RightTrigger = 0.f;

	// A activates the brace once per press.
	Raw.bRightPrimary = true;
	TestTrue(TEXT("A activates the brace"), Router.Step(Raw, Frame72).bBracePressed);
	TestFalse(TEXT("Holding A does not repeat"), Router.Step(Raw, Frame72).bBracePressed);
	Raw.bRightPrimary = false;
	Router.Step(Raw, Frame72);
	Raw.bLeftPrimary = true;
	C = Router.Step(Raw, Frame72);
	TestTrue(TEXT("X also activates the brace"), C.bBracePressed);
	TestFalse(TEXT("X does not vent in the grip scheme"), C.bVentLeft);
	Raw.bLeftPrimary = false;
	Router.Step(Raw, Frame72);

	// Stick click does nothing in this scheme.
	Raw.bLeftStickClick = true;
	TestFalse(TEXT("Stick click is unassigned in the grip scheme"), Router.Step(Raw, Frame72).bBracePressed);
	Raw.bLeftStickClick = false;

	// B confirms, menu pauses, each once per press.
	Raw.bRightSecondary = true;
	Raw.bMenu = true;
	C = Router.Step(Raw, Frame72);
	TestTrue(TEXT("B confirms"), C.bConfirmPressed);
	TestTrue(TEXT("Menu pauses"), C.bPausePressed);
	C = Router.Step(Raw, Frame72);
	TestFalse(TEXT("Confirm does not repeat"), C.bConfirmPressed);
	TestFalse(TEXT("Pause does not repeat"), C.bPausePressed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMRouterButtonSchemeTest, "ETM.Input.Router.ButtonVentScheme", ETMInputTestFlags)
bool FETMRouterButtonSchemeTest::RunTest(const FString&)
{
	ETMInput::FRouter Router;
	Router.SetScheme(ERigControlScheme::ButtonVent);

	// Grip does nothing at all in this scheme, however hard it is squeezed.
	ETMInput::FRawInput Raw;
	Raw.LeftGrip = Raw.RightGrip = 1.f;
	ETMInput::FCommands C = Hold(Router, Raw, 1.f);
	TestFalse(TEXT("Grip does not vent left"), C.bVentLeft);
	TestFalse(TEXT("Grip does not vent right"), C.bVentRight);

	// X vents left, A vents right, while held; neither is the brace.
	Raw.bLeftPrimary = true;
	C = Router.Step(Raw, Frame72);
	TestTrue(TEXT("X vents the left cannon"), C.bVentLeft);
	TestFalse(TEXT("X does not vent the right cannon"), C.bVentRight);
	TestFalse(TEXT("X is not the brace"), C.bBracePressed);
	Raw.bLeftPrimary = false;
	Raw.bRightPrimary = true;
	C = Router.Step(Raw, Frame72);
	TestTrue(TEXT("A vents the right cannon"), C.bVentRight);
	TestFalse(TEXT("Releasing X stops the left vent"), C.bVentLeft);
	Raw.bRightPrimary = false;

	// Stick click is the brace.
	Raw.bRightStickClick = true;
	TestTrue(TEXT("Stick click activates the brace"), Router.Step(Raw, Frame72).bBracePressed);
	TestFalse(TEXT("Holding the stick does not repeat"), Router.Step(Raw, Frame72).bBracePressed);
	Raw.bRightStickClick = false;

	// Fire is unchanged.
	Raw.LeftTrigger = 1.f;
	TestTrue(TEXT("Trigger still fires"), Router.Step(Raw, Frame72).bFireLeft);

	// Switching scheme with a button held must not fire that button's new meaning.
	ETMInput::FRouter Switching;
	ETMInput::FRawInput Held;
	Held.bRightPrimary = true;
	Switching.Step(Held, Frame72); // brace under the grip scheme
	Switching.SetScheme(ERigControlScheme::ButtonVent);
	Held.bRightStickClick = true;
	TestFalse(TEXT("A press held through a scheme switch is ignored"), Switching.Step(Held, Frame72).bBracePressed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMVentStatsTest, "ETM.Input.Router.VentStats", ETMInputTestFlags)
bool FETMVentStatsTest::RunTest(const FString&)
{
	ETMInput::FRouter Router;
	ETMInput::FRawInput Idle;

	// 1. A deliberate vent: trigger released, grip held for a second. Not suspect.
	ETMInput::FRawInput Deliberate;
	Deliberate.LeftGrip = 1.f;
	Hold(Router, Deliberate, 1.f);
	Hold(Router, Idle, 0.2f);

	// 2. The P-12 failure: squeezing the grip while firing. The vent blocks the shot.
	ETMInput::FRawInput Clench;
	Clench.RightTrigger = 1.f;
	Clench.RightGrip = 1.f;
	Hold(Router, Clench, 1.f);
	Hold(Router, Idle, 0.2f);

	// 3. A twitch: the grip crosses the gate for a moment with no trigger.
	ETMInput::FRawInput Twitch;
	Twitch.LeftGrip = 1.f;
	Hold(Router, Twitch, 0.2f);
	Hold(Router, Idle, 0.2f);

	const FRigVentStats& Grip = Router.GetStats(ERigControlScheme::GripVent);
	TestEqual(TEXT("Three vents opened"), Grip.VentActivations, 3);
	TestEqual(TEXT("One opened while firing"), Grip.VentsWhileFiring, 1);
	TestEqual(TEXT("One was too short to be deliberate"), Grip.ShortVents, 1);
	TestEqual(TEXT("Two are suspect"), Grip.SuspectVents, 2);
	TestTrue(TEXT("Play time is counted"), FMath::IsNearlyEqual(Grip.PlaySeconds, 2.8f, 0.1f));
	TestTrue(TEXT("Rate per minute follows from the counters"), FMath::IsNearlyEqual(Grip.GetSuspectVentsPerMinute(), 2.f * 60.f / Grip.PlaySeconds, 0.01f));
	TestTrue(TEXT("Suspect fraction"), FMath::IsNearlyEqual(Grip.GetSuspectFraction(), 2.f / 3.f, 0.001f));

	// The two schemes keep separate counters, and a vent open at the switch is closed out.
	Hold(Router, Deliberate, 1.f);
	Router.SetScheme(ERigControlScheme::ButtonVent);
	TestEqual(TEXT("Vent open at the switch is counted once"), Router.GetStats(ERigControlScheme::GripVent).VentActivations, 4);
	TestEqual(TEXT("A long vent closed by the switch is not suspect"), Router.GetStats(ERigControlScheme::GripVent).SuspectVents, 2);

	ETMInput::FRawInput ButtonVent;
	ButtonVent.bLeftPrimary = true;
	Hold(Router, ButtonVent, 1.f);
	Hold(Router, Idle, 0.2f);
	const FRigVentStats& Button = Router.GetStats(ERigControlScheme::ButtonVent);
	TestEqual(TEXT("Button scheme counts its own vents"), Button.VentActivations, 1);
	TestEqual(TEXT("Button scheme has no suspect vents here"), Button.SuspectVents, 0);
	TestEqual(TEXT("Grip counters are untouched by button play"), Router.GetStats(ERigControlScheme::GripVent).VentActivations, 4);

	Router.ResetStats();
	TestEqual(TEXT("Reset clears the counters"), Router.GetStats(ERigControlScheme::GripVent).VentActivations, 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMRigControllerTest, "ETM.Input.Controller.MappingAndScheme", ETMInputTestFlags)
bool FETMRigControllerTest::RunTest(const FString&)
{
	// A controller with no world and no player: enough to check the mappings and the scheme switch.
	ARigPlayerController* Controller = NewObject<ARigPlayerController>();
	const UInputMappingContext* Context = Controller->GetOrBuildMappingContext();
	TestNotNull(TEXT("Mapping context is built"), Context);
	if (!Context) return false;
	TestTrue(TEXT("Building twice returns the same context"), Context == Controller->GetOrBuildMappingContext());

	const TArray<FEnhancedActionKeyMapping>& Mappings = Context->GetMappings();
	TestEqual(TEXT("Eleven controls are mapped"), Mappings.Num(), 11);

	const auto FindKey = [&Mappings](const FKey& Key) -> const FEnhancedActionKeyMapping*
	{
		return Mappings.FindByPredicate([&Key](const FEnhancedActionKeyMapping& M) { return M.Key == Key; });
	};
	for (const FKey& Analog : { EKeys::OculusTouch_Left_Trigger_Axis, EKeys::OculusTouch_Right_Trigger_Axis, EKeys::OculusTouch_Left_Grip_Axis, EKeys::OculusTouch_Right_Grip_Axis })
	{
		const FEnhancedActionKeyMapping* M = FindKey(Analog);
		TestTrue(FString::Printf(TEXT("%s is mapped as an analog axis"), *Analog.ToString()), M && M->Action && M->Action->ValueType == EInputActionValueType::Axis1D);
	}
	for (const FKey& Digital : { EKeys::OculusTouch_Left_X_Click, EKeys::OculusTouch_Right_A_Click, EKeys::OculusTouch_Left_Y_Click, EKeys::OculusTouch_Right_B_Click,
		EKeys::OculusTouch_Left_Thumbstick_Click, EKeys::OculusTouch_Right_Thumbstick_Click, EKeys::OculusTouch_Left_Menu_Click })
	{
		const FEnhancedActionKeyMapping* M = FindKey(Digital);
		TestTrue(FString::Printf(TEXT("%s is mapped as a button"), *Digital.ToString()), M && M->Action && M->Action->ValueType == EInputActionValueType::Boolean);
	}
	for (const FEnhancedActionKeyMapping& M : Mappings)
	{
		const FString KeyName = M.Key.ToString();
		TestFalse(FString::Printf(TEXT("%s: no thumbstick axis is mapped (P-11)"), *KeyName), KeyName.Contains(TEXT("Thumbstick_X")) || KeyName.Contains(TEXT("Thumbstick_Y")) || KeyName.Contains(TEXT("Thumbstick_2D")));
	}

	// Scheme switch without touching the player's real save slot.
	TestEqual(TEXT("Default scheme"), Controller->GetControlScheme(), ERigControlScheme::GripVent);
	Controller->SetControlScheme(ERigControlScheme::ButtonVent, /*bSave*/ false);
	TestEqual(TEXT("Scheme switches at runtime"), Controller->GetControlScheme(), ERigControlScheme::ButtonVent);

	// With no pawn the controller still processes input and keeps statistics.
	ETMInput::FRawInput Raw;
	Raw.bLeftPrimary = true;
	for (int32 Frame = 0; Frame < 72; ++Frame) Controller->ProcessRawInput(Raw, Frame72);
	Controller->ProcessRawInput(ETMInput::FRawInput(), Frame72);
	TestEqual(TEXT("Controller records vents under the active scheme"), Controller->GetVentStats(ERigControlScheme::ButtonVent).VentActivations, 1);
	TestEqual(TEXT("Nothing recorded under the other scheme"), Controller->GetVentStats(ERigControlScheme::GripVent).VentActivations, 0);

	// The saved scheme round-trips through its own test slot.
	const FString Slot = TEXT("ETM_Input_AutomationTest");
	URigInputSaveGame* Save = NewObject<URigInputSaveGame>();
	Save->ControlScheme = ERigControlScheme::ButtonVent;
	TestTrue(TEXT("Scheme save writes"), UGameplayStatics::SaveGameToSlot(Save, Slot, 0));
	const URigInputSaveGame* Loaded = Cast<URigInputSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	TestTrue(TEXT("Scheme save round-trips"), Loaded && Loaded->ControlScheme == ERigControlScheme::ButtonVent);
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
