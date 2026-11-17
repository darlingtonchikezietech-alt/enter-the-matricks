// UAttackTokenSubsystem: limits how many enemy attacks can be in their warning-to-impact window at once.
// Spec: bible section 7 ("attack-token limits prevent several unavoidable hits from resolving
// simultaneously") and section 14 (simultaneous dangerous attacks: maximum 2). Owned by T-0045.
//
// AEnemyBase::BeginAttack asks for a token and is refused while the limit is in use; the enemy simply
// tries again on a later frame. Tokens return when the attack resolves, is interrupted, or the enemy
// dies, is parked or is destroyed. The encounter director (T-0017) may change the limit per phase.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "AttackTokenSubsystem.generated.h"

class AEnemyBase;

UCLASS()
class ETM_API UAttackTokenSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Grants a token if one is free. An enemy that already holds one keeps it and gets true. */
	bool TryAcquire(const AEnemyBase* Enemy);
	/** Returns the enemy's token, if it holds one. Safe to call when it holds none. */
	void Release(const AEnemyBase* Enemy);

	UFUNCTION(BlueprintPure, Category = "ETM|AttackTokens") int32 GetTokensInUse() const;
	UFUNCTION(BlueprintPure, Category = "ETM|AttackTokens") int32 GetMaxConcurrentAttacks() const { return MaxConcurrentAttacks; }
	/** Lowering the limit never cancels attacks already in progress; it only blocks new ones until enough finish. */
	UFUNCTION(BlueprintCallable, Category = "ETM|AttackTokens") void SetMaxConcurrentAttacks(int32 NewMax) { MaxConcurrentAttacks = FMath::Max(0, NewMax); }
	/** How often an attack was held back by the limit. For tuning: a high number means enemies are queueing. */
	UFUNCTION(BlueprintPure, Category = "ETM|AttackTokens") int32 GetRefusedCount() const { return RefusedCount; }

private:
	void Prune() const;

	mutable TArray<TWeakObjectPtr<const AEnemyBase>> Holders;
	int32 MaxConcurrentAttacks = 2;
	int32 RefusedCount = 0;
};
