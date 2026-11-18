// AEnemyBase: shared lifecycle, damage intake, destructible parts and pooling hooks for every Veyr unit.
// Spec: bible section 7 (behavior rules), section 13 (AEnemyBase), P-02, P-09, P-30. Owned by T-0013.
//
// Archetypes (T-0014, T-0022) subclass this and add movement and attack selection. They call
// BeginAttack to start a telegraphed attack and DealDamageTo for anything else that hurts; both
// refuse to deal damage once the enemy is staggered, dying or parked.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Enemies/Base/EnemyTypes.h"
#include "EnemyBase.generated.h"

class AEnemyBase;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEnemyStateChanged, AEnemyBase*, Enemy, EEnemyState, NewState, EEnemyState, OldState);
/** PartName is None for a body hit. Amount is what the body or the part actually lost. */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEnemyDamaged, AEnemyBase*, Enemy, float, Amount, FName, PartName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnemyPartBroken, AEnemyBase*, Enemy, FName, PartName);
/** An attack warning has started. Presentation must show shape and sound, not colour alone (bible section 7). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEnemyAttackTelegraph, AEnemyBase*, Enemy, float, TelegraphSeconds, AActor*, Target);
/** An attack was stopped before it resolved. PreventedDamage feeds the "damage prevented" score (P-30). */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnemyAttackInterrupted, AEnemyBase*, Enemy, float, PreventedDamage);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnEnemyAttackResolved, AEnemyBase*, Enemy, AActor*, Target, float, DamageDealt);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnemyKilled, AEnemyBase*, Enemy);

UCLASS()
class ETM_API AEnemyBase : public AActor
{
	GENERATED_BODY()

public:
	AEnemyBase();

	virtual void Tick(float DeltaSeconds) override;
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	// ---- Lifecycle ----

	/** Brings a parked enemy into play at the given transform with full health and intact parts. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Enemy") void Activate(const FTransform& SpawnTransform);
	/** Parks the enemy immediately, with no death sequence (level teardown, checkpoint retry). */
	UFUNCTION(BlueprintCallable, Category = "ETM|Enemy") void Deactivate();
	/** Starts the destruction sequence. Any attack in progress is cancelled and deals nothing. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Enemy") void Kill();
	/** Staggers the enemy, cancelling an attack in progress. Returns false if it cannot be staggered right now. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Enemy") bool Interrupt();

	// ---- Attacks ----

	/**
	 * Starts a telegraphed attack. After TelegraphSeconds the attack resolves and deals Damage to Target,
	 * unless the enemy is staggered or killed first. Returns false if the enemy cannot attack right now.
	 */
	UFUNCTION(BlueprintCallable, Category = "ETM|Enemy") bool BeginAttack(AActor* Target, float Damage, float TelegraphSeconds);
	/** The single gate for hurting anything. Deals nothing unless the enemy is approaching or attacking. Returns the damage dealt. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Enemy") float DealDamageTo(AActor* Target, float Amount);

	// ---- State ----

	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") EEnemyState GetState() const { return State; }
	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") bool IsActive() const { return State != EEnemyState::Inactive; }
	/** Counts against the active-enemy cap: in play and not yet dying. */
	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") bool IsThreat() const { return State != EEnemyState::Inactive && State != EEnemyState::Dying; }
	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") bool CanDealDamage() const { return ETMEnemy::CanDealDamage(State); }
	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") float GetHealth() const { return Health; }
	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") float GetHealthNormalized() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }
	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") float GetTimeInState() const { return TimeInState; }
	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") float GetAttackTimeRemaining() const { return bAttackPending ? AttackTimeRemaining : 0.f; }
	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") bool IsPartBroken(FName PartName) const;
	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") float GetPartHealth(FName PartName) const;
	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") bool IsPooled() const { return bPooled; }

	/** Advances state timers. Tick calls this; it is public so tests can step time directly. */
	void AdvanceState(float DeltaTime);
	/** Set by the pool when it creates the actor. Pooled enemies are parked after death; others are destroyed. */
	void MarkPooled() { bPooled = true; }

	// ---- Tuning ----

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy", meta = (ClampMin = "1")) float MaxHealth = 80.f;
	/** Time from appearing at an entrance to becoming able to attack. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy", meta = (ClampMin = "0")) float SpawnDuration = 0.5f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy", meta = (ClampMin = "0")) float StaggerDuration = 1.0f;
	/** Length of the destruction sequence. Collision is off for all of it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy", meta = (ClampMin = "0")) float DeathDuration = 0.75f;
	/** On death the enemy vanishes in a small explosion (AEnemyDeathBurst, T-0058). Bosses turn this off for their own collapse. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy") bool bExplodeOnDeath = true;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy") FLinearColor DeathBurstColor = FLinearColor(1.f, 0.55f, 0.18f);
	/** Body damage within StaggerWindow that staggers the enemy (concentrated fire). 0 disables it. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy", meta = (ClampMin = "0")) float StaggerDamageThreshold = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy", meta = (ClampMin = "0")) float StaggerWindow = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy") TArray<FEnemyPart> Parts;
	/** Attacks need a token from UAttackTokenSubsystem (max 2 at once). Turn off only for harmless attacks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy") bool bUsesAttackToken = true;

	// ---- Events ----

	UPROPERTY(BlueprintAssignable, Category = "ETM|Enemy") FOnEnemyStateChanged OnStateChanged;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Enemy") FOnEnemyDamaged OnDamaged;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Enemy") FOnEnemyPartBroken OnPartBroken;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Enemy") FOnEnemyAttackTelegraph OnAttackTelegraph;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Enemy") FOnEnemyAttackInterrupted OnAttackInterrupted;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Enemy") FOnEnemyAttackResolved OnAttackResolved;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Enemy") FOnEnemyKilled OnKilled;

protected:
	virtual void PostInitializeComponents() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/** Called after every state change. Archetypes start and stop movement, animation and effects here. */
	virtual void HandleStateChanged(EEnemyState NewState, EEnemyState OldState) {}
	/** Called when a part breaks, before any stagger. Archetypes react here (Bulwark drops its shield). */
	virtual void HandlePartBroken(const FEnemyPart& Part) {}
	/** Called when the telegraph ends. The default deals the pending damage to the pending target. */
	virtual void ResolveAttack(AActor* Target, float Damage);

	/** Blueprint hook for presentation. Gameplay rules stay in C++. */
	UFUNCTION(BlueprintImplementableEvent, Category = "ETM|Enemy", meta = (DisplayName = "On State Entered")) void ReceiveStateEntered(EEnemyState NewState, EEnemyState OldState);

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ETM|Enemy") TObjectPtr<USceneComponent> SceneRoot;

private:
	bool SetState(EEnemyState NewState);
	void ReleaseAttackToken();
	void ApplyBodyDamage(float Amount);
	void ApplyPartDamage(FEnemyPart& Part, float Amount);
	FEnemyPart* FindPartForComponent(const UPrimitiveComponent* Component);
	const FEnemyPart* FindPart(FName PartName) const;
	void SetPartCollisionEnabled(FName PartName, bool bEnabled);
	void SetInPlay(bool bInPlay);

	UPROPERTY(Transient) TWeakObjectPtr<AActor> PendingAttackTarget;

	EEnemyState State = EEnemyState::Inactive;
	float Health = 0.f;
	float TimeInState = 0.f;
	float AttackTimeRemaining = 0.f;
	float PendingAttackDamage = 0.f;
	float StaggerDamage = 0.f;
	float TimeSinceBodyDamage = 0.f;
	bool bAttackPending = false;
	bool bPooled = false;
};
