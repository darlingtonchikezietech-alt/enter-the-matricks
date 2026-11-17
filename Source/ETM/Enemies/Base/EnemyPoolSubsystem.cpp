#include "Enemies/Base/EnemyPoolSubsystem.h"
#include "Enemies/Base/EnemyBase.h"
#include "ETM.h"
#include "Engine/World.h"

AEnemyBase* UEnemyPoolSubsystem::CreateParked(TSubclassOf<AEnemyBase> EnemyClass)
{
	UWorld* World = GetWorld();
	if (!World || !EnemyClass) return nullptr;

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AEnemyBase* Enemy = World->SpawnActor<AEnemyBase>(EnemyClass, FTransform::Identity, Params);
	if (!Enemy) return nullptr;

	Enemy->MarkPooled();
	Enemy->Deactivate(); // no effect if already parked
	Enemy->SetActorHiddenInGame(true);
	Enemy->SetActorEnableCollision(false);
	Enemy->SetActorTickEnabled(false);
	Enemies.Add(Enemy);
	++TotalCreated;
	return Enemy;
}

void UEnemyPoolSubsystem::Prewarm(TSubclassOf<AEnemyBase> EnemyClass, int32 Count)
{
	if (!EnemyClass) return;

	int32 Existing = 0;
	for (const AEnemyBase* Enemy : Enemies)
	{
		if (Enemy && Enemy->GetClass() == EnemyClass) ++Existing;
	}
	Enemies.Reserve(Enemies.Num() + FMath::Max(0, Count - Existing));
	for (int32 Index = Existing; Index < Count; ++Index)
	{
		if (!CreateParked(EnemyClass)) break;
	}
}

AEnemyBase* UEnemyPoolSubsystem::Acquire(TSubclassOf<AEnemyBase> EnemyClass, const FTransform& SpawnTransform)
{
	if (!EnemyClass) return nullptr;

	AEnemyBase* Found = nullptr;
	for (AEnemyBase* Enemy : Enemies)
	{
		if (Enemy && !Enemy->IsActive() && Enemy->GetClass() == EnemyClass)
		{
			Found = Enemy;
			break;
		}
	}

	if (!Found)
	{
		if (!bAllowGrowth) return nullptr;
		Found = CreateParked(EnemyClass);
		if (!Found) return nullptr;
		++GrowthCount;
		UE_LOG(LogETM, Warning, TEXT("Enemy pool grew for %s during play (now %d actors). Raise the prewarm count."), *EnemyClass->GetName(), TotalCreated);
	}

	Found->Activate(SpawnTransform);
	return Found;
}

void UEnemyPoolSubsystem::DeactivateAll()
{
	for (AEnemyBase* Enemy : Enemies)
	{
		if (Enemy) Enemy->Deactivate();
	}
}

int32 UEnemyPoolSubsystem::GetThreatCount() const
{
	int32 Count = 0;
	for (const AEnemyBase* Enemy : Enemies)
	{
		if (Enemy && Enemy->IsThreat()) ++Count;
	}
	return Count;
}

int32 UEnemyPoolSubsystem::GetActiveCount() const
{
	int32 Count = 0;
	for (const AEnemyBase* Enemy : Enemies)
	{
		if (Enemy && Enemy->IsActive()) ++Count;
	}
	return Count;
}

int32 UEnemyPoolSubsystem::GetPooledCount(TSubclassOf<AEnemyBase> EnemyClass) const
{
	int32 Count = 0;
	for (const AEnemyBase* Enemy : Enemies)
	{
		if (Enemy && !Enemy->IsActive() && (!EnemyClass || Enemy->GetClass() == EnemyClass)) ++Count;
	}
	return Count;
}
