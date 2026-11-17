// What the game persists: settings, the current checkpoint, and mission results.
// Spec: bible section 6 ("Store phase, health preset, difficulty, and encounter seed"; replay progression,
// P-30), section 11 (settings), section 13 (USaveGame: settings, checkpoints, mission results),
// section 17 ("Settings, checkpoint, and mission results survive application restart"). Owned by T-0023.
//
// Calibration offsets and the control scheme keep their own slots for now (ETM_Calibration, ETM_Input);
// the settings task (T-0037) may fold them in.
#pragma once

#include "CoreMinimal.h"
#include "Objectives/MissionTypes.h"
#include "SaveTypes.generated.h"

UENUM(BlueprintType)
enum class EETMDifficulty : uint8
{
	Assisted,
	Standard,
	Veteran,
};

/** Bible section 11 options. The settings task (T-0037) gives them UI and behaviour; this is what is stored. */
USTRUCT(BlueprintType)
struct FETMSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings") EETMDifficulty Difficulty = EETMDifficulty::Standard;
	/** 0 = off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ClampMin = "0", ClampMax = "1")) float HapticStrength = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings") bool bReducedFlash = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ClampMin = "0", ClampMax = "1")) float DialogueVolume = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ClampMin = "0", ClampMax = "1")) float EffectsVolume = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ClampMin = "0", ClampMax = "1")) float MusicVolume = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings") bool bSubtitles = true;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ClampMin = "0.5", ClampMax = "2")) float SubtitleScale = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings", meta = (ClampMin = "0", ClampMax = "1")) float AimSmoothing = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings") bool bTargetAssistance = false;
	/** Toggle-to-vent alternative to holding the vent input (bible section 11). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings") bool bToggleVent = false;
	/** Swap which hand drives which cannon, where the runtime allows (bible section 11). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Settings") bool bSwapHands = false;

	/** Pulls every value back into its valid range (a hand-edited or damaged save cannot break the game). */
	void Sanitize()
	{
		HapticStrength = FMath::Clamp(HapticStrength, 0.f, 1.f);
		DialogueVolume = FMath::Clamp(DialogueVolume, 0.f, 1.f);
		EffectsVolume = FMath::Clamp(EffectsVolume, 0.f, 1.f);
		MusicVolume = FMath::Clamp(MusicVolume, 0.f, 1.f);
		SubtitleScale = FMath::Clamp(SubtitleScale, 0.5f, 2.f);
		AimSmoothing = FMath::Clamp(AimSmoothing, 0.f, 1.f);
		if (static_cast<uint8>(Difficulty) > static_cast<uint8>(EETMDifficulty::Veteran)) Difficulty = EETMDifficulty::Standard;
	}
};

/** A checkpoint plus what is needed to come back to it after the app closes. */
USTRUCT(BlueprintType)
struct FETMSavedCheckpoint
{
	GENERATED_BODY()

	/** The map to load before resuming (the campaign flow, T-0031, loads it). */
	UPROPERTY(BlueprintReadOnly, Category = "Save") FString MapName;
	UPROPERTY(BlueprintReadOnly, Category = "Save") FMissionCheckpoint Checkpoint;
	/** Difficulty when the checkpoint was taken (bible section 6: checkpoints store difficulty). */
	UPROPERTY(BlueprintReadOnly, Category = "Save") EETMDifficulty Difficulty = EETMDifficulty::Standard;
	UPROPERTY(BlueprintReadOnly, Category = "Save") FDateTime SavedAt;
};

/** One finished (or failed) run of a level, for results and replay scoring (bible section 6, P-30). */
USTRUCT(BlueprintType)
struct FETMMissionResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result") FName Level;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result") bool bCompleted = false;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result") EETMDifficulty Difficulty = EETMDifficulty::Standard;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result") float DurationSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result") int32 ShotsFired = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result") int32 ShotsHit = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result") int32 WeakPointHits = 0;
	/** P-30: brace-absorbed damage plus damage from attacks interrupted before they resolved. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result") float DamagePrevented = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result") int32 Retries = 0;
	/** Computed by the results screen (T-0031); only compared here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result") int32 Score = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Result") FDateTime When;

	float GetAccuracy() const { return ShotsFired > 0 ? static_cast<float>(ShotsHit) / ShotsFired : 0.f; }
};

/** Per level: the best completed run and the most recent run. */
USTRUCT(BlueprintType)
struct FETMLevelRecord
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Result") bool bHasBest = false;
	UPROPERTY(BlueprintReadOnly, Category = "Result") FETMMissionResult Best;
	UPROPERTY(BlueprintReadOnly, Category = "Result") FETMMissionResult Last;
	UPROPERTY(BlueprintReadOnly, Category = "Result") int32 Completions = 0;
};
