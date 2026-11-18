#include "Enemies/Base/EnemyDeathBurst.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr int32 DebrisPieces = 6;
	constexpr float Gravity = 980.f;

	UStaticMeshComponent* MakePiece(AActor* Owner, UStaticMesh* Mesh, UMaterialInterface* Material)
	{
		UStaticMeshComponent* Piece = NewObject<UStaticMeshComponent>(Owner);
		Piece->SetupAttachment(Owner->GetRootComponent());
		Piece->SetStaticMesh(Mesh);
		Piece->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Piece->SetCastShadow(false);
		if (Material) Piece->SetMaterial(0, Material);
		Piece->RegisterComponent();
		return Piece;
	}
}

static UStaticMesh* GBurstSphere = nullptr;
static UStaticMesh* GBurstCube = nullptr;
static UMaterialInterface* GBurstMaterial = nullptr;

AEnemyDeathBurst::AEnemyDeathBurst()
{
	PrimaryActorTick.bCanEverTick = true;
	SetCanBeDamaged(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));

	// Hard references so the shapes and material are cooked.
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Sphere(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	static ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> Emissive(TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
	GBurstSphere = Sphere.Get();
	GBurstCube = Cube.Get();
	GBurstMaterial = Emissive.Get();
}

AEnemyDeathBurst* AEnemyDeathBurst::SpawnBurst(UWorld* World, const FVector& Location, float Size, const FLinearColor& Color)
{
	if (!World) return nullptr;
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Params.ObjectFlags |= RF_Transient;
	AEnemyDeathBurst* Burst = World->SpawnActor<AEnemyDeathBurst>(Location, FRotator::ZeroRotator, Params);
	if (Burst) Burst->Setup(Size, Color);
	return Burst;
}

void AEnemyDeathBurst::Setup(float InSize, const FLinearColor& Color)
{
	Size = FMath::Clamp(InSize, 40.f, 400.f);
	if (GBurstMaterial)
	{
		Material = UMaterialInstanceDynamic::Create(GBurstMaterial, this);
		Material->SetVectorParameterValue(TEXT("Color"), Color * 3.f); // over 1 so it reads as light
	}
	Flash = MakePiece(this, GBurstSphere, Material);
	Flash->SetRelativeScale3D(FVector(0.01f));

	// Debris: thrown outward and up, spread evenly around, with a little randomness so no two bursts match.
	FRandomStream Random(GetUniqueID());
	for (int32 i = 0; i < DebrisPieces; ++i)
	{
		FPiece Piece;
		Piece.Mesh = MakePiece(this, GBurstCube, Material);
		const float Yaw = 360.f * i / DebrisPieces + Random.FRandRange(-20.f, 20.f);
		const FVector Out = FRotator(Random.FRandRange(10.f, 50.f), Yaw, 0.f).Vector();
		Piece.Velocity = Out * Random.FRandRange(350.f, 650.f) * (Size / 100.f);
		Piece.Spin = FRotator(Random.FRandRange(-720.f, 720.f), Random.FRandRange(-720.f, 720.f), 0.f);
		Piece.Scale = 0.12f * Size / 100.f;
		Piece.Mesh->SetRelativeScale3D(FVector(Piece.Scale));
		Debris.Add(Piece);
	}
}

bool AEnemyDeathBurst::IsFlashVisible() const
{
	return Flash && Flash->IsVisible();
}

void AEnemyDeathBurst::AdvanceBurst(float DeltaSeconds)
{
	Age += DeltaSeconds;

	// Flash: swell to the enemy's size, then collapse.
	if (Flash)
	{
		float Grow = 0.f;
		if (Age < FlashGrowSeconds) Grow = Age / FlashGrowSeconds;
		else Grow = 1.f - (Age - FlashGrowSeconds) / FMath::Max(0.01f, FlashFadeSeconds);
		Flash->SetVisibility(Grow > 0.f);
		Flash->SetRelativeScale3D(FVector(FMath::Max(0.01f, Grow) * Size / 100.f));
	}

	// Debris: ballistic, spinning, shrinking to nothing by the end.
	const float Shrink = FMath::Clamp(1.f - Age / FMath::Max(0.01f, Lifetime), 0.f, 1.f);
	for (FPiece& Piece : Debris)
	{
		if (!Piece.Mesh) continue;
		Piece.Velocity.Z -= Gravity * DeltaSeconds;
		Piece.Mesh->AddRelativeLocation(Piece.Velocity * DeltaSeconds);
		Piece.Mesh->AddRelativeRotation(Piece.Spin * DeltaSeconds);
		Piece.Mesh->SetRelativeScale3D(FVector(Piece.Scale * Shrink));
	}

	if (Age >= Lifetime) Destroy();
}

void AEnemyDeathBurst::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	AdvanceBurst(DeltaSeconds);
}
