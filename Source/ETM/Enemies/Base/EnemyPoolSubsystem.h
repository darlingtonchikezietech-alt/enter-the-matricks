// UEnemyPoolSubsystem: owns every enemy actor in a world and hands parked ones back out.
// Spec: bible section 13 (pooling), section 14 (active enemies: normal cap 8, boss plus at most 4 adds).
// Owned by T-0013.
//
// Prewarm during loading, then Acquire during combat. In steady state Acquire reuses a parked actor
// and allocates nothing. An enemy returns itself to the pool by finishing its death sequence; there
// is no release call to forget.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Templates/SubclassOf.h"
#include "EnemyPoolSubsystem.generated.h"

class AEnemyBase;

UCLASS()
class ETM_API UEnemyPoolSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Creates parked enemies of a class until at least Count exist. Call while loading, not in combat. */
	UFUNCTION(BlueprintCallable, Category = "ETM|EnemyPool") void Prewarm(TSubclassOf<AEnemyBase> EnemyClass, int32 Count);
	/**
	 * Brings a parked enemy of exactly this class into play at the transform.
	 * If none is parked: creates one when bAllowGrowth is set (and logs it), otherwise returns null.
	 */
	UFUNCTION(BlueprintCallable, Category = "ETM|EnemyPool") AEnemyBase* Acquire(TSubclassOf<AEnemyBase> EnemyClass, const FTransform& SpawnTransform);
	/** Parks every enemy at once with no death sequences (checkpoint retry, level teardown). */
	UFUNCTION(BlueprintCallable, Category = "ETM|EnemyPool") void DeactivateAll();

	/** Enemies in play and not yet dying: the number the active-enemy cap applies to. */
	UFUNCTION(BlueprintPure, Category = "ETM|EnemyPool") int32 GetThreatCount() const;
	/** Enemies not parked, including ones still playing a death sequence. */
	UFUNCTION(BlueprintPure, Category = "ETM|EnemyPool") int32 GetActiveCount() const;
	UFUNCTION(BlueprintPure, Category = "ETM|EnemyPool") int32 GetPooledCount(TSubclassOf<AEnemyBase> EnemyClass) const;
	/** Actors created since the world started. Flat during combat means no per-spawn allocation. */
	UFUNCTION(BlueprintPure, Category = "ETM|EnemyPool") int32 GetTotalCreated() const { return TotalCreated; }
	/** How many times Acquire had to create an actor because the pool was too small. Should stay 0 in a shipped encounter. */
	UFUNCTION(BlueprintPure, Category = "ETM|EnemyPool") int32 GetGrowthCount() const { return GrowthCount; }

	/** Off for shipping encounters once prewarm counts are known; on while prototyping. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|EnemyPool") bool bAllowGrowth = true;

private:
	AEnemyBase* CreateParked(TSubclassOf<AEnemyBase> EnemyClass);

	/** Every enemy this pool created. Small (tens of actors), so linear scans are cheaper than bookkeeping. */
	UPROPERTY(Transient) TArray<TObjectPtr<AEnemyBase>> Enemies;
	int32 TotalCreated = 0;
	int32 GrowthCount = 0;
};
