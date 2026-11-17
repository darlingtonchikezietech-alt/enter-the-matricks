// Helpers for the prototype enemies' placeholder geometry, built from the engine's basic shapes.
// The final art replaces these in T-0034; the silhouettes follow bible section 7 so playtests read
// shape, not colour. Call only from an actor constructor. Owned by T-0014.
#pragma once

#include "CoreMinimal.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "UObject/ConstructorHelpers.h"

namespace ETMPlaceholder
{
	enum class EShape { Cube, Cone, Cylinder, Sphere };

	inline UStaticMesh* LoadShape(EShape Shape)
	{
		const TCHAR* Path = TEXT("/Engine/BasicShapes/Cube.Cube");
		switch (Shape)
		{
		case EShape::Cone: Path = TEXT("/Engine/BasicShapes/Cone.Cone"); break;
		case EShape::Cylinder: Path = TEXT("/Engine/BasicShapes/Cylinder.Cylinder"); break;
		case EShape::Sphere: Path = TEXT("/Engine/BasicShapes/Sphere.Sphere"); break;
		default: break;
		}
		ConstructorHelpers::FObjectFinderOptional<UStaticMesh> Finder(Path);
		return Finder.Get();
	}

	/**
	 * Creates a placeholder part. Basic shapes are 100 cm across, so Size is in centimetres.
	 * Target parts answer the cannon's Visibility traces (query only); cosmetic parts have no collision.
	 */
	inline UStaticMeshComponent* MakePart(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, EShape Shape,
		const FVector& Size, const FVector& RelativeLocation, const FRotator& RelativeRotation, bool bTarget)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetStaticMesh(LoadShape(Shape));
		Part->SetRelativeLocation(RelativeLocation);
		Part->SetRelativeRotation(RelativeRotation);
		Part->SetRelativeScale3D(Size / 100.f);
		Part->SetCastShadow(false); // no persistent real-time shadows in the Quest 2 baseline (bible section 14)
		Part->SetCanEverAffectNavigation(false);
		if (bTarget)
		{
			Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Part->SetCollisionResponseToAllChannels(ECR_Ignore);
			Part->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
		}
		else
		{
			Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		}
		return Part;
	}
}
