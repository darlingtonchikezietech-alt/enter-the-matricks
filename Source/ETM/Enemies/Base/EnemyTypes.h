// Shared enemy types: lifecycle states, the transition table, and destructible parts.
// Spec: bible section 7 "Enemy behavior rules", P-02, P-09. Owned by T-0013.
#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "EnemyTypes.generated.h"

/** Every enemy moves through these states (bible section 7). Inactive means parked in the pool. */
UENUM(BlueprintType)
enum class EEnemyState : uint8
{
	Inactive,
	Spawning,
	Approaching,
	Attacking,
	Staggered,
	Dying,
};

/**
 * A destructible sub-target with its own health: Bulwark side nodes, a Lance charge chamber,
 * a Cradle pod, a Bulwark shield. Collision components that carry PartName as a component tag
 * belong to the part. Damage arriving here is already multiplied by the cannon (raw HP, P-02).
 */
USTRUCT(BlueprintType)
struct FEnemyPart
{
	GENERATED_BODY()

	/** Also the component tag that marks this part's collision. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part") FName PartName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part", meta = (ClampMin = "1")) float MaxHealth = 80.f;
	/** Extra scale on damage this part receives, for surfaces the cannon tags cannot express (Bulwark shield: 0.1 with no armor tag). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part", meta = (ClampMin = "0")) float DamageScale = 1.f;
	/** Damage to this part also reduces the body's health. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part") bool bPassDamageToBody = false;
	/** Breaking this part staggers the enemy, which cancels an attack in progress (Lance chamber, Cradle pod). */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part") bool bInterruptOnBreak = true;
	/** Body damage dealt when this part breaks. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Part", meta = (ClampMin = "0")) float BodyDamageOnBreak = 0.f;

	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Part") float Health = 0.f;
	UPROPERTY(Transient, VisibleInstanceOnly, BlueprintReadOnly, Category = "Part") bool bBroken = false;
};

namespace ETMEnemy
{
	/** The only legal state changes. Anything else is refused. */
	inline bool CanTransition(EEnemyState From, EEnemyState To)
	{
		switch (From)
		{
		case EEnemyState::Inactive:    return To == EEnemyState::Spawning;
		case EEnemyState::Spawning:    return To == EEnemyState::Approaching || To == EEnemyState::Dying;
		case EEnemyState::Approaching: return To == EEnemyState::Attacking || To == EEnemyState::Staggered || To == EEnemyState::Dying;
		case EEnemyState::Attacking:   return To == EEnemyState::Approaching || To == EEnemyState::Staggered || To == EEnemyState::Dying;
		case EEnemyState::Staggered:   return To == EEnemyState::Approaching || To == EEnemyState::Dying;
		case EEnemyState::Dying:       return To == EEnemyState::Inactive;
		default:                       return false;
		}
	}

	/** Only an enemy that is approaching or attacking may hurt anything. Dying units stop dealing damage immediately. */
	inline bool CanDealDamage(EEnemyState State)
	{
		return State == EEnemyState::Approaching || State == EEnemyState::Attacking;
	}

	/** Parked and dying enemies ignore incoming damage. */
	inline bool CanTakeDamage(EEnemyState State)
	{
		return State != EEnemyState::Inactive && State != EEnemyState::Dying;
	}
}
