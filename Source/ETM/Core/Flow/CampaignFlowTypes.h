// Campaign flow stages and the provisional results score.
// Spec: bible section 3 (story synopsis: relocation by fade between missions; ending), section 6 (mission loop
// ends in results; replay progression P-30), section 8 (menus and loading excluded from timing), section 11
// (relocation through fade-out, load, fade-in; results on a stable world-space panel). Owned by T-0031.
#pragma once

#include "CoreMinimal.h"
#include "Save/SaveTypes.h"
#include "CampaignFlowTypes.generated.h"

UENUM(BlueprintType)
enum class EETMFlowStage : uint8
{
	/** Title panel: start the campaign or the practice range. */
	Title,
	/** A campaign level is being played (UETMCampaignFlow::GetLevelIndex says which). */
	Level,
	/** Brief results after Level 1, before the relocation fade (bible section 9, 09:30-10:00). */
	LevelResults,
	/** Mara's closing line and the distant signal, after Level 2 (bible section 3). */
	Ending,
	/** Both levels' results, then back to the title. */
	CampaignResults,
	/** The open-ended practice range from T-0053. */
	Practice,
	/** Fading out, loading the next map, and fading in. GetTransitionTarget says where to. */
	Transition,
};

namespace ETMFlow
{
	constexpr int32 LevelCount = 2;

	/** Level ids stored with results ("L1", "L2"). */
	inline FName LevelId(int32 LevelIndex) { return LevelIndex == 0 ? FName(TEXT("L1")) : FName(TEXT("L2")); }

	inline const TCHAR* LevelTitle(int32 LevelIndex) { return LevelIndex == 0 ? TEXT("THE BREACH") : TEXT("THE LAST SIGNAL"); }

	/**
	 * Provisional score until a scoring pass is designed (the bible only names the inputs, P-30):
	 * 5000 for finishing, up to 3000 for accuracy, 20 per weak-point hit, 5 per point of damage prevented,
	 * minus 500 per retry. Never negative.
	 */
	inline int32 ComputeScore(const FETMMissionResult& R)
	{
		const float Raw = (R.bCompleted ? 5000.f : 0.f) + R.GetAccuracy() * 3000.f + R.WeakPointHits * 20.f
			+ R.DamagePrevented * 5.f - R.Retries * 500.f;
		return FMath::Max(0, FMath::RoundToInt(Raw));
	}

	/** "m:ss". */
	inline FString FormatTime(float Seconds)
	{
		const int32 Whole = FMath::Max(0, FMath::FloorToInt(Seconds));
		return FString::Printf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
	}
}
