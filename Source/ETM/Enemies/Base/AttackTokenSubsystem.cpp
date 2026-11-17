#include "Enemies/Base/AttackTokenSubsystem.h"
#include "Enemies/Base/EnemyBase.h"

void UAttackTokenSubsystem::Prune() const
{
	// An enemy destroyed without releasing (level unload, editor delete) must not hold a token forever.
	Holders.RemoveAll([](const TWeakObjectPtr<const AEnemyBase>& Holder) { return !Holder.IsValid(); });
}

bool UAttackTokenSubsystem::TryAcquire(const AEnemyBase* Enemy)
{
	if (!Enemy) return false;
	Prune();
	if (Holders.Contains(Enemy)) return true;
	if (Holders.Num() >= MaxConcurrentAttacks)
	{
		++RefusedCount;
		return false;
	}
	Holders.Add(Enemy);
	return true;
}

void UAttackTokenSubsystem::Release(const AEnemyBase* Enemy)
{
	Holders.Remove(Enemy);
}

int32 UAttackTokenSubsystem::GetTokensInUse() const
{
	Prune();
	return Holders.Num();
}
