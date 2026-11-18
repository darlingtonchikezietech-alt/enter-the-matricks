// AEnemyDeathBurst: the short explosion an enemy leaves when it dies (owner headset test, T-0058).
// A bright flash that swells and collapses, and a few pieces of debris thrown outward that fall and shrink.
// Cosmetic only: no collision, no damage. It removes itself after Lifetime.
// Placeholder look (engine shapes and the emissive engine material) until the VFX pass (T-0034, T-0036).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemyDeathBurst.generated.h"

class UMaterialInstanceDynamic;
class UStaticMeshComponent;

UCLASS(NotBlueprintable)
class ETM_API AEnemyDeathBurst : public AActor
{
	GENERATED_BODY()

public:
	AEnemyDeathBurst();

	/** Spawns a burst Size cm across (about the size of the enemy) at Location. */
	static AEnemyDeathBurst* SpawnBurst(UWorld* World, const FVector& Location, float Size, const FLinearColor& Color);

	virtual void Tick(float DeltaSeconds) override;
	/** Advances the animation; Tick calls it, tests may too. */
	void AdvanceBurst(float DeltaSeconds);

	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") float GetAge() const { return Age; }
	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") bool IsFlashVisible() const;
	UFUNCTION(BlueprintPure, Category = "ETM|Enemy") int32 GetDebrisCount() const { return Debris.Num(); }

	/** Seconds until the burst removes itself. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy") float Lifetime = 0.6f;
	/** The flash swells for this long, then collapses over FlashFadeSeconds. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy") float FlashGrowSeconds = 0.08f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "ETM|Enemy") float FlashFadeSeconds = 0.25f;

private:
	void Setup(float InSize, const FLinearColor& Color);

	struct FPiece
	{
		TObjectPtr<UStaticMeshComponent> Mesh;
		FVector Velocity = FVector::ZeroVector;
		FRotator Spin = FRotator::ZeroRotator;
		float Scale = 0.1f;
	};

	UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Flash;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
	TArray<FPiece> Debris;
	float Size = 100.f;
	float Age = 0.f;
};
