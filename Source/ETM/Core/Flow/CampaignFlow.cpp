#include "Core/Flow/CampaignFlow.h"
#include "Combat/DefenseComponent.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Player/XRSeatedRigPawn.h"
#include "Save/ETMSaveStore.h"
#include "ETM.h"
#include "Engine/World.h"

// ---------------------------------------------------------------------------------------------
// Player choices
// ---------------------------------------------------------------------------------------------

bool UETMCampaignFlow::StartCampaign()
{
	if (Stage != EETMFlowStage::Title) return false;
	Results.Reset();
	BeginTransition(EETMFlowStage::Level, 0);
	return true;
}

bool UETMCampaignFlow::StartPractice()
{
	if (Stage != EETMFlowStage::Title) return false;
	BeginTransition(EETMFlowStage::Practice, 0);
	return true;
}

bool UETMCampaignFlow::Continue()
{
	if (Stage != EETMFlowStage::LevelResults) return false;
	// Between missions, maintenance equipment relocates the rig during a brief fade (bible section 3).
	BeginTransition(EETMFlowStage::Level, LevelIndex + 1);
	return true;
}

bool UETMCampaignFlow::SkipEnding()
{
	if (Stage != EETMFlowStage::Ending) return false;
	EndingRemaining = 0.f;
	SetStage(EETMFlowStage::CampaignResults);
	return true;
}

bool UETMCampaignFlow::ReturnToTitle()
{
	if (Stage != EETMFlowStage::CampaignResults && Stage != EETMFlowStage::Practice) return false;
	BeginTransition(EETMFlowStage::Title, 0);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Transitions
// ---------------------------------------------------------------------------------------------

void UETMCampaignFlow::SetStage(EETMFlowStage NewStage)
{
	if (NewStage == Stage) return;
	Stage = NewStage;
	if (Stage == EETMFlowStage::Ending) EndingRemaining = EndingSeconds;
	UE_LOG(LogETM, Display, TEXT("Campaign flow: stage %d, level %d"), static_cast<int32>(Stage), LevelIndex);
	OnStageChanged.Broadcast(Stage, LevelIndex);
}

void UETMCampaignFlow::BeginTransition(EETMFlowStage Target, int32 TargetLevel)
{
	StopTracking();
	TransitionTarget = Target;
	TransitionLevel = FMath::Clamp(TargetLevel, 0, ETMFlow::LevelCount - 1);
	Fade = EFade::Out;
	SetStage(EETMFlowStage::Transition);
}

FString UETMCampaignFlow::MapFor(EETMFlowStage Target, int32 TargetLevel) const
{
	switch (Target)
	{
	case EETMFlowStage::Level: return LevelMaps.IsValidIndex(TargetLevel) ? LevelMaps[TargetLevel] : TitleMap;
	case EETMFlowStage::Practice: return PracticeMap;
	default: return TitleMap;
	}
}

void UETMCampaignFlow::Tick(float DeltaSeconds)
{
	const float Step = DeltaSeconds / FMath::Max(0.05f, FadeSeconds);
	switch (Fade)
	{
	case EFade::Out:
		FadeAlpha = FMath::Min(1.f, FadeAlpha + Step);
		if (FadeAlpha >= 1.f)
		{
			// Fully black: switch to the target so the next world builds it, then load.
			Fade = EFade::Loading;
			LevelIndex = TransitionLevel;
			const FString Map = MapFor(TransitionTarget, TransitionLevel);
			UE_LOG(LogETM, Display, TEXT("Campaign flow: loading %s"), *Map);
			if (Loader) Loader(Map); else NotifyWorldReady(nullptr);
		}
		break;
	case EFade::In:
		FadeAlpha = FMath::Max(0.f, FadeAlpha - Step);
		if (FadeAlpha <= 0.f) Fade = EFade::None;
		break;
	default:
		break;
	}

	if (Stage == EETMFlowStage::Ending && EndingRemaining > 0.f)
	{
		EndingRemaining = FMath::Max(0.f, EndingRemaining - DeltaSeconds);
		if (EndingRemaining <= 0.f) SetStage(EETMFlowStage::CampaignResults);
	}
}

void UETMCampaignFlow::NotifyWorldReady(UWorld* World)
{
	if (Fade != EFade::Loading) return;
	Fade = EFade::In;
	SetStage(TransitionTarget);
}

// ---------------------------------------------------------------------------------------------
// Levels and results
// ---------------------------------------------------------------------------------------------

void UETMCampaignFlow::BeginLevel(UWorld* World, AXRSeatedRigPawn* Rig)
{
	StopTracking();
	Run = FETMMissionResult();
	Run.Level = ETMFlow::LevelId(LevelIndex);
	if (Store) Run.Difficulty = Store->GetSettings().Difficulty;
	RunWorld = World;
	RunRig = Rig;
	RunStartTime = World ? World->GetTimeSeconds() : 0.0;
	bTracking = true;
	if (Rig)
	{
		if (Rig->LeftCannon) Rig->LeftCannon->OnFired.AddUniqueDynamic(this, &UETMCampaignFlow::HandleShot);
		if (Rig->RightCannon) Rig->RightCannon->OnFired.AddUniqueDynamic(this, &UETMCampaignFlow::HandleShot);
		if (Rig->Defense) Rig->Defense->OnBraceAbsorbedDamage.AddUniqueDynamic(this, &UETMCampaignFlow::HandleBraceAbsorbed);
	}
	if (UMissionStateSubsystem* Mission = World ? World->GetSubsystem<UMissionStateSubsystem>() : nullptr)
	{
		Mission->OnRetryStarted.AddUniqueDynamic(this, &UETMCampaignFlow::HandleRetry);
		RunMission = Mission;
	}
}

FETMMissionResult UETMCampaignFlow::GetCurrentRun() const
{
	FETMMissionResult Out = Run;
	if (bTracking && RunWorld.IsValid()) Out.DurationSeconds = static_cast<float>(RunWorld->GetTimeSeconds() - RunStartTime);
	return Out;
}

bool UETMCampaignFlow::NotifyLevelComplete(UWorld* World)
{
	if (Stage != EETMFlowStage::Level) return false;
	FETMMissionResult Result = GetCurrentRun();
	Result.bCompleted = true;
	Result.When = FDateTime::Now();
	Result.Score = ETMFlow::ComputeScore(Result);
	StopTracking();
	if (Results.Num() <= LevelIndex) Results.SetNum(LevelIndex + 1);
	Results[LevelIndex] = Result;
	if (Store) Store->RecordResult(Result);
	UE_LOG(LogETM, Display, TEXT("Level %d complete in %s, accuracy %.0f%%, score %d"), LevelIndex + 1,
		*ETMFlow::FormatTime(Result.DurationSeconds), Result.GetAccuracy() * 100.f, Result.Score);

	// Level 1: brief results, then the relocation fade. Level 2: the ending, then the campaign results.
	SetStage(LevelIndex + 1 < ETMFlow::LevelCount ? EETMFlowStage::LevelResults : EETMFlowStage::Ending);
	return true;
}

int32 UETMCampaignFlow::GetCampaignScore() const
{
	int32 Total = 0;
	for (const FETMMissionResult& R : Results) Total += R.Score;
	return Total;
}

void UETMCampaignFlow::StopTracking()
{
	if (!bTracking) return;
	bTracking = false;
	if (AXRSeatedRigPawn* Rig = RunRig.Get())
	{
		if (Rig->LeftCannon) Rig->LeftCannon->OnFired.RemoveAll(this);
		if (Rig->RightCannon) Rig->RightCannon->OnFired.RemoveAll(this);
		if (Rig->Defense) Rig->Defense->OnBraceAbsorbedDamage.RemoveAll(this);
	}
	if (UMissionStateSubsystem* Mission = RunMission.Get()) Mission->OnRetryStarted.RemoveAll(this);
	RunRig.Reset();
	RunMission.Reset();
}

void UETMCampaignFlow::HandleShot(const FCannonShot& Shot)
{
	++Run.ShotsFired;
	if (Shot.bHit && Shot.Damage > 0.f) ++Run.ShotsHit;
	if (Shot.bHit && Shot.bWeakPoint && Shot.Damage > 0.f) ++Run.WeakPointHits;
}

void UETMCampaignFlow::HandleBraceAbsorbed(float Absorbed, float Passed)
{
	// P-30 also counts damage from attacks interrupted before they resolved; no event carries that yet.
	Run.DamagePrevented += FMath::Max(0.f, Absorbed);
}

void UETMCampaignFlow::HandleRetry(const FMissionCheckpoint& Checkpoint)
{
	++Run.Retries;
}
