// AGatebreaker: Level 1 boss. A heavy six-legged siege platform locked into the chamber.
// Spec: bible section 9 "Gatebreaker boss specification", section 7 values, P-02, P-09, P-10. Owned by T-0021.
//
//   Phase 1 Anchor   - two shoulder shield nodes (600 HP) exposed in alternating windows; ends when both break.
//   Phase 2 Breach   - after every furnace attack the core (6500 HP, P-34) opens: 6 s if interrupted, 4 s if it fired.
//   Phase 3 Collapse - from core <= 30% (1950 HP); the core keeps opening, and an artillery attack (35) joins the cycle.
//
// In every phase a furnace attack charges for 2 s and hits the rig for 45 unless its charge chamber
// (240 HP) is broken first. All targets are weak points (x2, P-02); everything else is immune armour.
// Missed windows simply repeat, so the fight cannot softlock.
//
// The boss is not pooled. Place it in the level; it activates itself when the encounter director starts
// BossPhaseName, and tells the director to complete that phase when it dies (also after a retry).
#pragma once

#include "CoreMinimal.h"
#include "Enemies/Base/EnemyBase.h"
#include "Gatebreaker.generated.h"

class AEncounterDirector;
class UAudioComponent;
class USoundBase;
class UStaticMeshComponent;

UENUM(BlueprintType)
enum class EGatebreakerPhase : uint8
{
	Dormant,
	Anchor,
	Breach,
	Collapse,
	Destroyed,
};

UENUM(BlueprintType)
enum class EGatebreakerAttack : uint8
{
	None,
	Furnace,
	Artillery,
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGatebreakerPhaseChanged, EGatebreakerPhase, NewPhase);
/** A target opened or closed. Target is NodeLeft, NodeRight, Chamber or Core. Seconds is the window length when opening. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnGatebreakerTargetExposed, FName, Target, bool, bExposed, float, Seconds);
/** A hit struck immune armour (deflection feedback, bible section 9). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGatebreakerDeflected, FVector, Location);

UCLASS()
class ETM_API AGatebreaker : public AEnemyBase
{
	GENERATED_BODY()

public:
	AGatebreaker();
	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintPure, Category = "ETM|Gatebreaker") EGatebreakerPhase GetBossPhase() const { return BossPhase; }
	UFUNCTION(BlueprintPure, Category = "ETM|Gatebreaker") EGatebreakerAttack GetCurrentAttack() const { return CurrentAttack; }
	UFUNCTION(BlueprintPure, Category = "ETM|Gatebreaker") bool IsTargetExposed(FName Target) const;
	UFUNCTION(BlueprintPure, Category = "ETM|Gatebreaker") float GetCoreHealth() const { return GetPartHealth(CorePart); }
	UFUNCTION(BlueprintPure, Category = "ETM|Gatebreaker") float GetCoreOpenRemaining() const { return CoreOpenRemaining; }
	/** How many times the core has opened since activation. */
	UFUNCTION(BlueprintPure, Category = "ETM|Gatebreaker") int32 GetCoreExposureCount() const { return CoreExposureCount; }
	UFUNCTION(BlueprintPure, Category = "ETM|Gatebreaker") int32 GetDeflectedCount() const { return DeflectedCount; }

	/** Binds to a director (unbinding any previous one). Called at BeginPlay for a director set in the level. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Gatebreaker") void SetDirector(AEncounterDirector* InDirector);

	/** One simulation step without the world's tick: state timers, then behaviour. For tests and tools. */
	void SimulateStep(float DeltaSeconds);

	// ---- Target names: also the component tags on the target meshes ----
	static const FName NodeLeftPart;
	static const FName NodeRightPart;
	static const FName ChamberPart;
	static const FName CorePart;

	// ---- Bible values ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker") float FurnaceDamage = 45.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker") float FurnaceChargeSeconds = 2.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker") float CoreWindowInterrupted = 6.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker") float CoreWindowFired = 4.f;
	/** Phase 3 starts when the core is at or below this fraction (P-10: 30%, 1950 HP after P-34). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker") float CollapseThreshold = 0.3f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker") float ArtilleryDamage = 35.f;

	// ---- Prototype values (not in the bible; tune in the M2 slice) ----
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker|Prototype") float NodeWindowSeconds = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker|Prototype") float NodeGapSeconds = 1.5f;
	/** Quiet time before the next attack: after a closed core window (for venting) or after an attack in phase 1. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker|Prototype") float AttackInterval = 5.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker|Prototype") float ArtilleryTelegraphSeconds = 2.f;

	// ---- Wiring ----
	/** The rig. If unset, the first local player's pawn. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Gatebreaker") TObjectPtr<AActor> Target;
	/** Director whose BossPhaseName phase activates this boss, and which is told when it dies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Gatebreaker") TObjectPtr<AEncounterDirector> Director;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker") FName BossPhaseName = TEXT("Gatebreaker");
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker") TObjectPtr<USoundBase> FurnaceChargeSound;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Gatebreaker") TObjectPtr<USoundBase> ArtilleryChargeSound;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Gatebreaker") FOnGatebreakerPhaseChanged OnBossPhaseChanged;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Gatebreaker") FOnGatebreakerTargetExposed OnTargetExposed;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Gatebreaker") FOnGatebreakerDeflected OnDeflected;

	// ---- Placeholder geometry (replaced by final art in T-0034) ----
	UPROPERTY(VisibleAnywhere, Category = "ETM|Gatebreaker") TObjectPtr<UStaticMeshComponent> Hull;
	UPROPERTY(VisibleAnywhere, Category = "ETM|Gatebreaker") TObjectPtr<UStaticMeshComponent> NodeLeft;
	UPROPERTY(VisibleAnywhere, Category = "ETM|Gatebreaker") TObjectPtr<UStaticMeshComponent> NodeRight;
	UPROPERTY(VisibleAnywhere, Category = "ETM|Gatebreaker") TObjectPtr<UStaticMeshComponent> Chamber;
	UPROPERTY(VisibleAnywhere, Category = "ETM|Gatebreaker") TObjectPtr<UStaticMeshComponent> Core;
	UPROPERTY(VisibleAnywhere, Category = "ETM|Gatebreaker") TObjectPtr<UStaticMeshComponent> NodeLeftShutter;
	UPROPERTY(VisibleAnywhere, Category = "ETM|Gatebreaker") TObjectPtr<UStaticMeshComponent> NodeRightShutter;
	UPROPERTY(VisibleAnywhere, Category = "ETM|Gatebreaker") TObjectPtr<UStaticMeshComponent> ChamberShutter;
	UPROPERTY(VisibleAnywhere, Category = "ETM|Gatebreaker") TObjectPtr<UStaticMeshComponent> CoreShutter;
	/** Raises during the artillery warning: the shape cue for the phase-3 attack. */
	UPROPERTY(VisibleAnywhere, Category = "ETM|Gatebreaker") TObjectPtr<USceneComponent> ArtilleryPivot;
	UPROPERTY(VisibleAnywhere, Category = "ETM|Gatebreaker") TObjectPtr<UAudioComponent> ChargeAudio;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void HandleStateChanged(EEnemyState NewState, EEnemyState OldState) override;
	virtual void HandlePartBroken(const FEnemyPart& Part) override;
	virtual void ResolveAttack(AActor* AttackTarget, float Damage) override;

private:
	void UpdateBehavior(float DeltaSeconds);
	void UpdateNodeWindows(float DeltaSeconds);
	void StartAttack();
	void EndFurnace(bool bInterrupted);
	void OpenCore(float Seconds);
	void SetExposed(FName TargetName, bool bExposed, float Seconds = 0.f);
	void SetBossPhase(EGatebreakerPhase NewPhase);
	void CloseEverything();
	void ResetPart(FName PartName);
	AActor* ResolveTarget();
	UStaticMeshComponent* MeshFor(FName TargetName) const;
	UStaticMeshComponent* ShutterFor(FName TargetName) const;

	UFUNCTION() void HandleDirectorPhaseStarted(int32 PhaseIndex, FName PhaseName);

	FTransform HomeTransform;
	TSet<FName> Exposed;
	EGatebreakerPhase BossPhase = EGatebreakerPhase::Dormant;
	EGatebreakerAttack CurrentAttack = EGatebreakerAttack::None;
	float AttackCooldown = 0.f;
	float CoreOpenRemaining = 0.f;
	float NodeTimer = 0.f;
	int32 NodeTurn = 0;
	int32 CoreExposureCount = 0;
	int32 DeflectedCount = 0;
	bool bNodeWindowOpen = false;
	bool bArtilleryNext = false;
};
