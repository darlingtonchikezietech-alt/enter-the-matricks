// Automation tests for the seated calibration maths and the calibration save.
// Run: UnrealEditor-Cmd ETM.uproject -ExecCmds="Automation RunTests ETM.Player; Quit" -unattended -nullrdr
#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Player/SeatedCalibration.h"
#include "Player/CalibrationSaveGame.h"
#include "Kismet/GameplayStatics.h"

namespace
{
	const EAutomationTestFlags ETMTestFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter;

	FVector HeadForwardInRig(const FTransform& Origin, const FTransform& Head)
	{
		return (Origin.GetRotation() * Head.GetRotation()).GetForwardVector();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMSeatRecenterTest, "ETM.Player.Calibration.SeatRecenter", ETMTestFlags)
bool FETMSeatRecenterTest::RunTest(const FString&)
{
	// Player sits off-centre, lower than the design eye point, turned 40 degrees and looking slightly down.
	const FTransform Head(FRotator(-10.f, 40.f, 5.f), FVector(30.f, -20.f, 95.f));
	const FVector DesignEye(0.f, 0.f, 120.f);
	const FTransform Origin = ETMSeated::SolveSeatRecenter(Head, DesignEye, FTransform::Identity);

	TestTrue(TEXT("Head lands on the design eye point"), ETMSeated::HeadLocationInRig(Origin, Head.GetLocation()).Equals(DesignEye, 0.01f));

	const FVector Forward = HeadForwardInRig(Origin, Head);
	TestTrue(TEXT("Head faces rig forward (no sideways component)"), FMath::IsNearlyZero(Forward.Y, 0.001) && Forward.X > 0.0);

	const FRotator OriginRot = Origin.Rotator();
	TestTrue(TEXT("Origin is only yawed, never pitched or rolled"), FMath::IsNearlyZero(OriginRot.Pitch, 0.001) && FMath::IsNearlyZero(OriginRot.Roll, 0.001));
	TestTrue(TEXT("Head pitch is preserved"), FMath::IsNearlyEqual((Origin.GetRotation() * Head.GetRotation()).Rotator().Pitch, -10.0, 0.01));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMForwardReAnchorTest, "ETM.Player.Calibration.ForwardReAnchor", ETMTestFlags)
bool FETMForwardReAnchorTest::RunTest(const FString&)
{
	// Start from a calibrated seat, then the player swivels the chair 70 degrees and leans.
	const FVector DesignEye(0.f, 0.f, 120.f);
	const FTransform Seated(FRotator::ZeroRotator, FVector(5.f, 5.f, 100.f));
	const FTransform Calibrated = ETMSeated::SolveSeatRecenter(Seated, DesignEye, FTransform::Identity);

	const FTransform Turned(FRotator(0.f, 70.f, 0.f), FVector(12.f, -8.f, 98.f));
	const FVector HeadBefore = ETMSeated::HeadLocationInRig(Calibrated, Turned.GetLocation());
	const FTransform ReAnchored = ETMSeated::SolveForwardReAnchor(Turned, Calibrated);

	TestTrue(TEXT("Head does not move within the rig"), ETMSeated::HeadLocationInRig(ReAnchored, Turned.GetLocation()).Equals(HeadBefore, 0.01f));
	const FVector Forward = HeadForwardInRig(ReAnchored, Turned);
	TestTrue(TEXT("Current facing becomes rig forward"), FMath::IsNearlyZero(Forward.Y, 0.001) && Forward.X > 0.0);

	// Doing it again without moving changes nothing.
	const FTransform Again = ETMSeated::SolveForwardReAnchor(Turned, ReAnchored);
	TestTrue(TEXT("Re-anchor is idempotent"), Again.Equals(ReAnchored, 0.01f));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMCalibrationVerticalGazeTest, "ETM.Player.Calibration.VerticalGazeKeepsYaw", ETMTestFlags)
bool FETMCalibrationVerticalGazeTest::RunTest(const FString&)
{
	// Looking almost straight up: the head yaw is meaningless, so the existing yaw must be kept.
	const FTransform Current(FRotator(0.f, 25.f, 0.f), FVector(1.f, 2.f, 3.f));
	const FTransform Head(FRotator(88.f, 130.f, 0.f), FVector(0.f, 0.f, 100.f));
	const FTransform Origin = ETMSeated::SolveSeatRecenter(Head, FVector(0.f, 0.f, 120.f), Current);

	TestTrue(TEXT("Yaw is unchanged"), FMath::IsNearlyEqual(Origin.Rotator().Yaw, 25.0, 0.01));
	TestTrue(TEXT("Position is still corrected"), ETMSeated::HeadLocationInRig(Origin, Head.GetLocation()).Equals(FVector(0.f, 0.f, 120.f), 0.01f));

	float Yaw = 0.f;
	TestFalse(TEXT("Vertical gaze reports no yaw"), ETMSeated::GetHeadYaw(Head.GetRotation(), Yaw));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMAimOffsetClampTest, "ETM.Player.Calibration.AimOffsetClamp", ETMTestFlags)
bool FETMAimOffsetClampTest::RunTest(const FString&)
{
	const FRotator InRange = ETMSeated::ClampAimOffset(FRotator(-30.f, 10.f, 0.f));
	TestTrue(TEXT("In-range offset is unchanged"), InRange.Equals(FRotator(-30.f, 10.f, 0.f), 0.001f));

	const FRotator Clamped = ETMSeated::ClampAimOffset(FRotator(-120.f, 200.f, 45.f));
	TestTrue(TEXT("Pitch clamps to the minimum"), FMath::IsNearlyEqual(Clamped.Pitch, (double)ETMSeated::AimOffsetPitchMin));
	TestTrue(TEXT("Yaw wraps then clamps"), FMath::IsNearlyEqual(Clamped.Yaw, (double)-ETMSeated::AimOffsetYawLimit));
	TestTrue(TEXT("Roll is dropped"), FMath::IsNearlyZero(Clamped.Roll));

	TestTrue(TEXT("Pitch clamps to the maximum"), FMath::IsNearlyEqual(ETMSeated::ClampAimOffset(FRotator(60.f, 0.f, 0.f)).Pitch, (double)ETMSeated::AimOffsetPitchMax));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FETMCalibrationSaveRoundTripTest, "ETM.Player.Calibration.SaveRoundTrip", ETMTestFlags)
bool FETMCalibrationSaveRoundTripTest::RunTest(const FString&)
{
	// Uses its own slot so a developer's real calibration is never touched.
	const FString Slot = TEXT("ETM_Calibration_AutomationTest");
	UETMCalibrationSaveGame* Save = NewObject<UETMCalibrationSaveGame>();
	Save->LeftAimOffset = FRotator(-42.f, 7.f, 0.f);
	Save->RightAimOffset = FRotator(-18.f, -11.f, 0.f);

	TestTrue(TEXT("Save writes"), UGameplayStatics::SaveGameToSlot(Save, Slot, 0));
	const UETMCalibrationSaveGame* Loaded = Cast<UETMCalibrationSaveGame>(UGameplayStatics::LoadGameFromSlot(Slot, 0));
	TestNotNull(TEXT("Save loads"), Loaded);
	if (Loaded)
	{
		TestEqual(TEXT("Version"), Loaded->Version, UETMCalibrationSaveGame::CurrentVersion);
		TestTrue(TEXT("Left offset round-trips"), Loaded->LeftAimOffset.Equals(Save->LeftAimOffset, 0.001f));
		TestTrue(TEXT("Right offset round-trips"), Loaded->RightAimOffset.Equals(Save->RightAimOffset, 0.001f));
	}
	UGameplayStatics::DeleteGameInSlot(Slot, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
