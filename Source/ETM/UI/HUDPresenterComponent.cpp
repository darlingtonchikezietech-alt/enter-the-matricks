#include "UI/HUDPresenterComponent.h"
#include "Bosses/Gatebreaker/Gatebreaker.h"
#include "Combat/DefenseComponent.h"
#include "Combat/IntegrityComponent.h"
#include "Enemies/Base/EnemyBase.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Player/XRSeatedRigPawn.h"
#include "Weapons/CannonComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FVector HeatBarSize(30.f, 3.f, 2.f);   // along the housing
	const FVector RigBarSize(3.f, 40.f, 3.f);    // across the frame
	const FVector BraceBarSize(3.f, 14.f, 2.f);
	const FRotator FaceTheSeat(0.f, 180.f, 0.f); // text and bars face back toward the seated player
}

UHUDPresenterComponent::UHUDPresenterComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostPhysics;

	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cone(TEXT("/Engine/BasicShapes/Cone.Cone"));
	BarMesh = Cube.Get();
	ArrowMesh = Cone.Get();
}

// ---------------------------------------------------------------------------------------------
// Building the HUD (once, at BeginPlay)
// ---------------------------------------------------------------------------------------------

UTextRenderComponent* UHUDPresenterComponent::MakeText(const TCHAR* Name, USceneComponent* Parent, const FVector& Location, float Size, bool bWorldSpace)
{
	UTextRenderComponent* Text = NewObject<UTextRenderComponent>(GetOwner(), MakeUniqueObjectName(GetOwner(), UTextRenderComponent::StaticClass(), Name));
	Text->SetupAttachment(Parent);
	if (bWorldSpace)
	{
		Text->SetUsingAbsoluteLocation(true);
		Text->SetUsingAbsoluteRotation(true);
	}
	else
	{
		Text->SetRelativeLocationAndRotation(Location, FaceTheSeat);
	}
	Text->SetHorizontalAlignment(EHTA_Center);
	Text->SetVerticalAlignment(EVRTA_TextCenter);
	Text->SetWorldSize(Size);
	Text->SetTextRenderColor(FColor(255, 214, 150)); // emergency amber (bible section 12)
	Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Text->SetCastShadow(false);
	Text->RegisterComponent();
	Owned.Add(Text);
	return Text;
}

UStaticMeshComponent* UHUDPresenterComponent::MakeBar(const TCHAR* Name, USceneComponent* Parent, const FVector& Location, const FVector& Size, bool bWorldSpace)
{
	UStaticMeshComponent* Bar = NewObject<UStaticMeshComponent>(GetOwner(), MakeUniqueObjectName(GetOwner(), UStaticMeshComponent::StaticClass(), Name));
	Bar->SetupAttachment(Parent);
	Bar->SetStaticMesh(BarMesh);
	Bar->SetCollisionEnabled(ECollisionEnabled::NoCollision); // HUD never blocks or receives a shot
	Bar->SetCastShadow(false);
	Bar->SetCanEverAffectNavigation(false);
	if (bWorldSpace)
	{
		Bar->SetUsingAbsoluteLocation(true);
		Bar->SetUsingAbsoluteRotation(true);
		Bar->SetUsingAbsoluteScale(true);
	}
	else
	{
		Bar->SetRelativeLocation(Location);
	}
	Bar->SetRelativeScale3D(Size / 100.f);
	Bar->RegisterComponent();
	Owned.Add(Bar);
	return Bar;
}

void UHUDPresenterComponent::BeginPlay()
{
	Super::BeginPlay();
	Rig = Cast<AXRSeatedRigPawn>(GetOwner());
	if (!Rig) return;

	// Heat on each cannon housing, plus a lock bracket that appears when it overheats (a shape, not a colour).
	LeftHeatBar = MakeBar(TEXT("HudHeatL"), Rig->LeftCannon, HeatBarOnCannon, HeatBarSize);
	RightHeatBar = MakeBar(TEXT("HudHeatR"), Rig->RightCannon, HeatBarOnCannon, HeatBarSize);
	LeftLock = MakeBar(TEXT("HudLockL"), Rig->LeftCannon, HeatBarOnCannon + FVector(0.f, 0.f, 4.f), FVector(4.f, 12.f, 12.f));
	RightLock = MakeBar(TEXT("HudLockR"), Rig->RightCannon, HeatBarOnCannon + FVector(0.f, 0.f, 4.f), FVector(4.f, 12.f, 12.f));
	LeftLock->SetVisibility(false);
	RightLock->SetVisibility(false);
	SetBarFill(LeftHeatBar, HeatBarSize, 0.f);
	SetBarFill(RightHeatBar, HeatBarSize, 0.f);
	Rig->LeftCannon->OnHeatChanged.AddUniqueDynamic(this, &UHUDPresenterComponent::HandleLeftHeat);
	Rig->RightCannon->OnHeatChanged.AddUniqueDynamic(this, &UHUDPresenterComponent::HandleRightHeat);

	// Central lower frame: rig integrity. Small frame indicator: brace readiness.
	RigBar = MakeBar(TEXT("HudRigBar"), Rig->RigFrame, RigBarLocation, RigBarSize);
	RigLabel = MakeText(TEXT("HudRigLabel"), Rig->RigFrame, RigBarLocation + FVector(0.f, 0.f, 5.f), 4.f, false);
	BraceBar = MakeBar(TEXT("HudBrace"), Rig->RigFrame, BraceBarLocation, BraceBarSize);
	ObjectiveLine = MakeText(TEXT("HudObjectiveLine"), Rig->RigFrame, ObjectiveLineLocation, 3.f, false);
	SecondaryLine = MakeText(TEXT("HudSecondary"), Rig->RigFrame, SecondaryLineLocation, 3.f, false);
	SecondaryLine->SetVisibility(false);
	if (Rig->RigIntegrity)
	{
		Rig->RigIntegrity->OnIntegrityChanged.AddUniqueDynamic(this, &UHUDPresenterComponent::HandleRigIntegrity);
		HandleRigIntegrity(Rig->RigIntegrity->GetCurrent(), Rig->RigIntegrity->GetMax(), 0.f);
	}

	// World-space: objective marker, warning markers with edge arrows, boss weak-point markers.
	ObjectiveMarker = MakeText(TEXT("HudObjectiveMarker"), this, FVector::ZeroVector, 18.f, true);
	ObjectiveMarker->SetVisibility(false);
	WarningSlots.SetNum(MaxShownWarnings);
	for (FWarningSlot& Slot : WarningSlots)
	{
		Slot.Label = MakeText(TEXT("HudWarning"), this, FVector::ZeroVector, 30.f, true);
		Slot.Label->SetTextRenderColor(FColor(255, 110, 60)); // danger red-orange, always with the "!" shape and a countdown
		Slot.Label->SetVisibility(false);
		Slot.Edge = MakeBar(TEXT("HudEdgeArrow"), this, FVector::ZeroVector, FVector(6.f, 6.f, 10.f), true);
		Slot.Edge->SetStaticMesh(ArrowMesh);
		Slot.Edge->SetVisibility(false);
	}
	BossSlots.SetNum(4);
	for (FBossSlot& Slot : BossSlots)
	{
		Slot.Label = MakeText(TEXT("HudBossWindow"), this, FVector::ZeroVector, 26.f, true);
		Slot.Label->SetVisibility(false);
	}

	RefreshSources();
}

// ---------------------------------------------------------------------------------------------
// Sources
// ---------------------------------------------------------------------------------------------

void UHUDPresenterComponent::RefreshSources()
{
	UWorld* World = GetWorld();
	if (!World) return;

	// Objective: whatever the mission currently defends.
	if (const UMissionStateSubsystem* Mission = World->GetSubsystem<UMissionStateSubsystem>())
	{
		AMissionObjective* Current = Mission->GetObjective();
		if (Current != Objective.Get())
		{
			if (AMissionObjective* Old = Objective.Get()) Old->Integrity->OnIntegrityChanged.RemoveDynamic(this, &UHUDPresenterComponent::HandleObjectiveIntegrity);
			Objective = Current;
			if (Current)
			{
				Current->Integrity->OnIntegrityChanged.AddUniqueDynamic(this, &UHUDPresenterComponent::HandleObjectiveIntegrity);
				HandleObjectiveIntegrity(Current->Integrity->GetCurrent(), Current->Integrity->GetMax(), 0.f);
			}
		}
	}

	// The rig's own BeginPlay may run after ours; keep the reading true without waiting for the first hit.
	if (Rig && Rig->RigIntegrity && (RigMax != Rig->RigIntegrity->GetMax() || RigCurrent != Rig->RigIntegrity->GetCurrent()))
	{
		HandleRigIntegrity(Rig->RigIntegrity->GetCurrent(), Rig->RigIntegrity->GetMax(), 0.f);
	}

	// Enemies and bosses: bind once each. Pooled enemies stay bound across reuse.
	for (TActorIterator<AEnemyBase> It(World); It; ++It)
	{
		AEnemyBase* Enemy = *It;
		if (BoundEnemies.Contains(Enemy)) continue;
		BoundEnemies.Add(Enemy);
		// An attack already under way when we first see this enemy still gets its warning.
		if (Enemy->GetState() == EEnemyState::Attacking && Enemy->GetAttackTimeRemaining() > 0.f)
		{
			FActiveWarning& Warning = Warnings.AddDefaulted_GetRef();
			Warning.Source = Enemy;
			Warning.Remaining = Enemy->GetAttackTimeRemaining();
		}
		Enemy->OnAttackTelegraph.AddUniqueDynamic(this, &UHUDPresenterComponent::HandleAttackTelegraph);
		Enemy->OnStateChanged.AddUniqueDynamic(this, &UHUDPresenterComponent::HandleEnemyStateChanged);
		if (AGatebreaker* Boss = Cast<AGatebreaker>(Enemy))
		{
			BoundBosses.Add(Boss);
			Boss->OnTargetExposed.AddUniqueDynamic(this, &UHUDPresenterComponent::HandleBossTargetExposed);
		}
	}
}

// ---------------------------------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------------------------------

void UHUDPresenterComponent::SetBarFill(UStaticMeshComponent* Bar, const FVector& FullSize, float Fill) const
{
	if (!Bar) return;
	const float F = FMath::Clamp(Fill, 0.f, 1.f);
	// The long axis shrinks with the value: length, not colour, carries the reading.
	FVector Size = FullSize;
	if (FullSize.X >= FullSize.Y) Size.X *= FMath::Max(F, 0.02f); else Size.Y *= FMath::Max(F, 0.02f);
	Bar->SetRelativeScale3D(Size / 100.f);
}

void UHUDPresenterComponent::HandleLeftHeat(float HeatNormalized, bool bOverheated)
{
	LeftHeat = HeatNormalized;
	SetBarFill(LeftHeatBar, HeatBarSize, HeatNormalized);
	if (LeftLock) LeftLock->SetVisibility(bOverheated);
}

void UHUDPresenterComponent::HandleRightHeat(float HeatNormalized, bool bOverheated)
{
	RightHeat = HeatNormalized;
	SetBarFill(RightHeatBar, HeatBarSize, HeatNormalized);
	if (RightLock) RightLock->SetVisibility(bOverheated);
}

void UHUDPresenterComponent::HandleRigIntegrity(float Current, float Max, float Delta)
{
	RigCurrent = Current;
	RigMax = Max;
	SetBarFill(RigBar, RigBarSize, Max > 0.f ? Current / Max : 0.f);
	if (RigLabel) RigLabel->SetText(GetRigText());
}

void UHUDPresenterComponent::HandleObjectiveIntegrity(float Current, float Max, float Delta)
{
	ObjectiveCurrent = Current;
	ObjectiveMax = Max;
	if (ObjectiveMarker)
	{
		ObjectiveMarker->SetText(GetObjectiveMarkerText());
		ObjectiveMarker->SetVisibility(Max > 0.f);
	}
}

void UHUDPresenterComponent::HandleAttackTelegraph(AEnemyBase* Enemy, float TelegraphSeconds, AActor* Target)
{
	if (!Enemy) return;
	Warnings.RemoveAll([Enemy](const FActiveWarning& W) { return W.Source.Get() == Enemy; });
	FActiveWarning& Warning = Warnings.AddDefaulted_GetRef();
	Warning.Source = Enemy;
	Warning.Remaining = TelegraphSeconds;
	Warning.bTargetsRig = Target && Target == GetOwner();
}

void UHUDPresenterComponent::HandleEnemyStateChanged(AEnemyBase* Enemy, EEnemyState NewState, EEnemyState OldState)
{
	// The warning ends with the attack: resolved, interrupted, killed or parked.
	if (OldState == EEnemyState::Attacking)
	{
		Warnings.RemoveAll([Enemy](const FActiveWarning& W) { return W.Source.Get() == Enemy; });
	}
}

void UHUDPresenterComponent::HandleBossTargetExposed(FName Target, bool bExposed, float Seconds)
{
	// The event carries no sender; there is one boss per fight, so take the bound boss that matches.
	AGatebreaker* Boss = nullptr;
	for (const TWeakObjectPtr<AGatebreaker>& Bound : BoundBosses)
	{
		if (Bound.IsValid() && Bound->IsTargetExposed(Target) == bExposed) { Boss = Bound.Get(); break; }
	}
	BossWindows.RemoveAll([Target](const FBossWindow& W) { return W.Target == Target; });
	if (bExposed && Boss)
	{
		FBossWindow& Window = BossWindows.AddDefaulted_GetRef();
		Window.Boss = Boss;
		Window.Target = Target;
		Window.Remaining = Seconds;
	}
}

USceneComponent* UHUDPresenterComponent::BoxFor(const AGatebreaker* Boss, FName Target) const
{
	if (!Boss) return nullptr;
	if (Target == AGatebreaker::NodeLeftPart) return Boss->NodeLeft;
	if (Target == AGatebreaker::NodeRightPart) return Boss->NodeRight;
	if (Target == AGatebreaker::ChamberPart) return Boss->Chamber;
	if (Target == AGatebreaker::CorePart) return Boss->Core;
	return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Per-frame layout
// ---------------------------------------------------------------------------------------------

void UHUDPresenterComponent::SetObjectiveLine(const FText& Line)
{
	if (ObjectiveLine) ObjectiveLine->SetText(Line);
}

void UHUDPresenterComponent::ShowSecondaryMessage(const FText& Message, float Seconds)
{
	Secondary.Show(Message, Seconds);
	if (SecondaryLine) SecondaryLine->SetText(Message);
}

void UHUDPresenterComponent::AdvanceHud(float DeltaTime)
{
	if (!Rig) return;

	ScanTimer -= DeltaTime;
	if (ScanTimer <= 0.f)
	{
		ScanTimer = ScanSeconds;
		RefreshSources();
	}

	const FVector Head = Rig->Camera->GetComponentLocation();
	const FVector Forward = Rig->Camera->GetForwardVector();

	// Brace readiness on the frame: full when ready, empty while active, refilling through the cooldown.
	if (Rig->Defense)
	{
		BraceShown = Rig->Defense->GetReadiness();
		SetBarFill(BraceBar, BraceBarSize, BraceShown);
	}

	// Objective marker floats above the equipment and pulses while it is being hit.
	if (AMissionObjective* Current = Objective.Get())
	{
		PulseTime += DeltaTime;
		bObjectiveAlarm = Current->IsUnderAttack();
		const float Pulse = bObjectiveAlarm ? 1.f + 0.25f * FMath::Abs(FMath::Sin(PulseTime * 8.f)) : 1.f;
		ObjectiveMarker->SetWorldLocationAndRotation(Current->GetMarkerLocation(), (Head - Current->GetMarkerLocation()).Rotation());
		ObjectiveMarker->SetWorldSize(18.f * Pulse);
	}

	// Warnings: tick down, rank, and lay out the most urgent near their sources.
	TArray<ETMHud::FWarning> Ranked;
	for (FActiveWarning& Warning : Warnings)
	{
		Warning.Remaining = FMath::Max(0.f, Warning.Remaining - DeltaTime);
		if (Warning.Source.IsValid()) Ranked.Add({ Warning.Source.Get(), Warning.Remaining, Warning.bTargetsRig });
	}
	Warnings.RemoveAll([](const FActiveWarning& W) { return !W.Source.IsValid(); });
	Ranked = ETMHud::RankWarnings(MoveTemp(Ranked), WarningSlots.Num());
	for (int32 Index = 0; Index < WarningSlots.Num(); ++Index)
	{
		FWarningSlot& Slot = WarningSlots[Index];
		if (!Ranked.IsValidIndex(Index))
		{
			Slot.Source.Reset();
			Slot.Label->SetVisibility(false);
			Slot.Edge->SetVisibility(false);
			continue;
		}
		AEnemyBase* Source = static_cast<AEnemyBase*>(const_cast<void*>(Ranked[Index].SourceKey));
		Slot.Source = Source;
		const FVector At = Source->GetActorLocation() + FVector(0.f, 0.f, MarkerHeightAboveSource);
		Slot.Label->SetWorldLocationAndRotation(At, (Head - At).Rotation());
		Slot.Label->SetText(FText::FromString(FString::Printf(TEXT("! %.1f"), Ranked[Index].Remaining)));
		Slot.Label->SetVisibility(true);

		// Off-view source: an arrow on a ring 60 cm in front of the head, pointing toward it.
		Slot.bEdge = ETMHud::IsOutsideView(Head, Forward, At, EdgeCueHalfAngle);
		if (Slot.bEdge)
		{
			FVector Dir = (At - Head).GetSafeNormal();
			const FVector Side = (Dir - Forward * FVector::DotProduct(Dir, Forward)).GetSafeNormal();
			const FVector ArrowAt = Head + Forward * 60.f + Side * 25.f;
			Slot.Edge->SetWorldLocationAndRotation(ArrowAt, Side.Rotation() + FRotator(-90.f, 0.f, 0.f)); // cone tip points along Side
		}
		Slot.Edge->SetVisibility(Slot.bEdge);
	}

	// Boss weak-point windows: a countdown on the exposed target itself.
	for (FBossWindow& Window : BossWindows) Window.Remaining = FMath::Max(0.f, Window.Remaining - DeltaTime);
	BossWindows.RemoveAll([](const FBossWindow& W) { return !W.Boss.IsValid() || !W.Boss->IsTargetExposed(W.Target); });
	for (int32 Index = 0; Index < BossSlots.Num(); ++Index)
	{
		FBossSlot& Slot = BossSlots[Index];
		USceneComponent* Box = BossWindows.IsValidIndex(Index) ? BoxFor(BossWindows[Index].Boss.Get(), BossWindows[Index].Target) : nullptr;
		Slot.bActive = Box != nullptr;
		Slot.Label->SetVisibility(Slot.bActive);
		if (!Slot.bActive) continue;
		const FVector At = Box->GetComponentLocation() + FVector(0.f, 0.f, 90.f);
		Slot.Label->SetWorldLocationAndRotation(At, (Head - At).Rotation());
		Slot.Label->SetText(FText::FromString(FString::Printf(TEXT("[ OPEN %.1f ]"), BossWindows[Index].Remaining)));
	}

	// Secondary text gives way to any critical warning (bible section 11).
	Secondary.Advance(DeltaTime);
	if (SecondaryLine) SecondaryLine->SetVisibility(Secondary.IsVisible(Ranked.Num() > 0));
}

void UHUDPresenterComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	AdvanceHud(DeltaTime);
}

// ---------------------------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------------------------

bool UHUDPresenterComponent::IsHeatLockShown(bool bLeft) const
{
	const UStaticMeshComponent* Lock = bLeft ? LeftLock.Get() : RightLock.Get();
	return Lock && Lock->IsVisible();
}

FText UHUDPresenterComponent::GetRigText() const
{
	return FText::FromString(FString::Printf(TEXT("RIG %d"), FMath::RoundToInt(RigCurrent)));
}

FText UHUDPresenterComponent::GetObjectiveMarkerText() const
{
	const int32 Percent = ObjectiveMax > 0.f ? FMath::RoundToInt(100.f * ObjectiveCurrent / ObjectiveMax) : 0;
	return FText::FromString(FString::Printf(TEXT("POWER %d%%"), Percent));
}

int32 UHUDPresenterComponent::GetShownWarningCount() const
{
	int32 Count = 0;
	for (const FWarningSlot& Slot : WarningSlots) if (Slot.Label && Slot.Label->IsVisible()) ++Count;
	return Count;
}

bool UHUDPresenterComponent::IsSecondaryMessageShown() const
{
	return SecondaryLine && SecondaryLine->IsVisible();
}

int32 UHUDPresenterComponent::GetShownBossMarkerCount() const
{
	int32 Count = 0;
	for (const FBossSlot& Slot : BossSlots) if (Slot.bActive) ++Count;
	return Count;
}

bool UHUDPresenterComponent::GetWarningMarker(int32 Index, FVector& OutLocation, bool& bOutEdgeCue, FString& OutText) const
{
	if (!WarningSlots.IsValidIndex(Index) || !WarningSlots[Index].Label || !WarningSlots[Index].Label->IsVisible()) return false;
	OutLocation = WarningSlots[Index].Label->GetComponentLocation();
	bOutEdgeCue = WarningSlots[Index].bEdge;
	OutText = WarningSlots[Index].Label->Text.ToString();
	return true;
}

bool UHUDPresenterComponent::GetBossMarker(int32 Index, FVector& OutLocation, FString& OutText) const
{
	if (!BossSlots.IsValidIndex(Index) || !BossSlots[Index].bActive) return false;
	OutLocation = BossSlots[Index].Label->GetComponentLocation();
	OutText = BossSlots[Index].Label->Text.ToString();
	return true;
}

TArray<USceneComponent*> UHUDPresenterComponent::GetFrameElements() const
{
	return { RigBar.Get(), BraceBar.Get(), ObjectiveLine.Get(), SecondaryLine.Get() };
}
