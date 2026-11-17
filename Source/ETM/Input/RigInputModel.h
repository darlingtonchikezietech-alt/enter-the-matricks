// Turns raw controller values into rig commands under either control scheme, and keeps the
// accidental-vent counters. No engine input classes are used here, so every rule can be covered
// by automation tests (Input/Tests/RigInputTest.cpp).
// Spec: bible section 4 "Input mapping", P-11, P-12. Owned by T-0012.
#pragma once

#include "CoreMinimal.h"
#include "Input/RigInputTypes.h"

namespace ETMInput
{
	struct FTuning
	{
		float GripVentThreshold = 0.85f;  // P-12: vent only above this analog grip value...
		float GripVentHoldSeconds = 0.1f; // ...held for this long
		float GripVentRelease = 0.75f;    // closes below this, so a grip hovering at the threshold does not flicker
		float TriggerPress = 0.5f;
		float TriggerRelease = 0.4f;
		float ShortVentSeconds = 0.3f;    // a vent shorter than this counts as suspect in the statistics
	};

	/** One frame of controller state. Left stick and right stick axes are deliberately absent (P-11). */
	struct FRawInput
	{
		float LeftTrigger = 0.f;
		float RightTrigger = 0.f;
		float LeftGrip = 0.f;
		float RightGrip = 0.f;
		bool bLeftPrimary = false;     // X
		bool bRightPrimary = false;    // A
		bool bLeftSecondary = false;   // Y
		bool bRightSecondary = false;  // B
		bool bLeftStickClick = false;
		bool bRightStickClick = false;
		bool bMenu = false;
	};

	/** What the rig should do this frame. The "Pressed" fields are true for one frame only. */
	struct FCommands
	{
		bool bFireLeft = false;
		bool bFireRight = false;
		bool bVentLeft = false;
		bool bVentRight = false;
		bool bBracePressed = false;
		bool bConfirmPressed = false;
		bool bPausePressed = false;
	};

	/** P-12 grip gate: opens after the grip has stayed above the threshold for the hold time. */
	struct FGripGate
	{
		float TimeAbove = 0.f;
		bool bOpen = false;

		bool Step(float Grip, float Dt, const FTuning& T)
		{
			if (bOpen)
			{
				if (Grip < T.GripVentRelease)
				{
					bOpen = false;
					TimeAbove = 0.f;
				}
			}
			else if (Grip >= T.GripVentThreshold)
			{
				TimeAbove += Dt;
				bOpen = TimeAbove >= T.GripVentHoldSeconds;
			}
			else
			{
				TimeAbove = 0.f;
			}
			return bOpen;
		}
	};

	/** Analog trigger with a little hysteresis so a half-pulled trigger does not chatter. */
	struct FTriggerLatch
	{
		bool bHeld = false;

		bool Step(float Value, const FTuning& T)
		{
			bHeld = bHeld ? Value >= T.TriggerRelease : Value >= T.TriggerPress;
			return bHeld;
		}
	};

	class FRouter
	{
	public:
		FTuning Tuning;

		ERigControlScheme GetScheme() const { return Scheme; }

		/** Switching scheme closes both vents and forgets button history, so nothing carries across. */
		void SetScheme(ERigControlScheme NewScheme)
		{
			if (NewScheme == Scheme) return;
			CloseVent(0);
			CloseVent(1);
			Scheme = NewScheme;
			Gates[0] = Gates[1] = FGripGate();
			bPrevBrace = bPrevConfirm = bPrevMenu = true; // a button held through the switch must be released first
		}

		const FRigVentStats& GetStats(ERigControlScheme ForScheme) const { return Stats[static_cast<int32>(ForScheme)]; }
		void ResetStats() { Stats[0] = Stats[1] = FRigVentStats(); }

		/** Advances one frame and returns the commands for it. */
		FCommands Step(const FRawInput& In, float Dt)
		{
			FCommands Out;
			FRigVentStats& S = Stats[static_cast<int32>(Scheme)];
			S.PlaySeconds += Dt;

			Out.bFireLeft = Triggers[0].Step(In.LeftTrigger, Tuning);
			Out.bFireRight = Triggers[1].Step(In.RightTrigger, Tuning);

			bool bBrace = false;
			if (Scheme == ERigControlScheme::GripVent)
			{
				Out.bVentLeft = Gates[0].Step(In.LeftGrip, Dt, Tuning);
				Out.bVentRight = Gates[1].Step(In.RightGrip, Dt, Tuning);
				bBrace = In.bLeftPrimary || In.bRightPrimary;
			}
			else
			{
				Out.bVentLeft = In.bLeftPrimary;
				Out.bVentRight = In.bRightPrimary;
				bBrace = In.bLeftStickClick || In.bRightStickClick;
			}

			TrackVent(0, Out.bVentLeft, Out.bFireLeft, Dt, S);
			TrackVent(1, Out.bVentRight, Out.bFireRight, Dt, S);

			// B is the objective interaction (bible section 4); Y mirrors it for the left hand.
			const bool bConfirm = In.bRightSecondary || In.bLeftSecondary;

			Out.bBracePressed = bBrace && !bPrevBrace;
			Out.bConfirmPressed = bConfirm && !bPrevConfirm;
			Out.bPausePressed = In.bMenu && !bPrevMenu;
			bPrevBrace = bBrace;
			bPrevConfirm = bConfirm;
			bPrevMenu = In.bMenu;
			return Out;
		}

	private:
		void TrackVent(int32 Hand, bool bVenting, bool bFiring, float Dt, FRigVentStats& S)
		{
			if (bVenting && !bVentOpen[Hand])
			{
				bVentOpen[Hand] = true;
				VentTime[Hand] = 0.f;
				bVentSuspect[Hand] = bFiring;
				++S.VentActivations;
				if (bFiring) ++S.VentsWhileFiring;
			}
			else if (bVenting)
			{
				VentTime[Hand] += Dt;
			}
			else if (bVentOpen[Hand])
			{
				FinishVent(Hand, S);
			}
		}

		void FinishVent(int32 Hand, FRigVentStats& S)
		{
			bVentOpen[Hand] = false;
			if (VentTime[Hand] < Tuning.ShortVentSeconds)
			{
				++S.ShortVents;
				bVentSuspect[Hand] = true;
			}
			if (bVentSuspect[Hand]) ++S.SuspectVents;
		}

		void CloseVent(int32 Hand)
		{
			if (bVentOpen[Hand]) FinishVent(Hand, Stats[static_cast<int32>(Scheme)]);
		}

		ERigControlScheme Scheme = ERigControlScheme::GripVent;
		FGripGate Gates[2];
		FTriggerLatch Triggers[2];
		FRigVentStats Stats[2];
		float VentTime[2] = { 0.f, 0.f };
		bool bVentOpen[2] = { false, false };
		bool bVentSuspect[2] = { false, false };
		bool bPrevBrace = false;
		bool bPrevConfirm = false;
		bool bPrevMenu = false;
	};
}
