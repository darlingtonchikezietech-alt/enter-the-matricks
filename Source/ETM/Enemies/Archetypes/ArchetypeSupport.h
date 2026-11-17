// Small helpers shared by the enemy archetypes: target lookup, the infrastructure-attacker tag, and
// straight-line movement. Owned by T-0022 (used by T-0014's archetypes too once they are refactored).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Objectives/MissionObjective.h"
#include "Objectives/MissionStateSubsystem.h"
#include "Objectives/MissionTypes.h"

namespace ETMArchetype
{
	/** The mission's active objective, if any. */
	inline AMissionObjective* FindObjective(const UObject* WorldContext)
	{
		const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
		const UMissionStateSubsystem* Mission = World ? World->GetSubsystem<UMissionStateSubsystem>() : nullptr;
		return Mission ? Mission->GetObjective() : nullptr;
	}

	/** The rig (first local player's pawn). */
	inline AActor* FindRig(const UObject* WorldContext)
	{
		return UGameplayStatics::GetPlayerPawn(WorldContext, 0);
	}

	/**
	 * Enemies whose designated target can be either the rig or the objective (Lance Walker, Cradle Bomber)
	 * carry the infrastructure-attacker tag only while they target the objective, so the objective accepts
	 * their damage then and only then (T-0018).
	 */
	inline void SyncInfrastructureTag(AActor* Self, const AActor* Target)
	{
		Self->Tags.Remove(ETMObjectiveTags::InfrastructureAttacker);
		if (Target && Target->IsA<AMissionObjective>())
		{
			Self->Tags.AddUnique(ETMObjectiveTags::InfrastructureAttacker);
		}
	}

	/**
	 * Moves Self toward Goal by at most Speed * Dt, stopping StopDistance short. Ground units keep their
	 * height (bFlat). Faces the direction of travel. Returns true once within StopDistance.
	 */
	inline bool MoveToward(AActor* Self, const FVector& Goal, float Speed, float Dt, float StopDistance, bool bFlat)
	{
		const FVector Here = Self->GetActorLocation();
		FVector Delta = Goal - Here;
		if (bFlat) Delta.Z = 0.f;
		const float Distance = Delta.Size();
		if (Distance <= StopDistance) return true;
		const FVector Direction = Delta / Distance;
		const float Step = FMath::Min(Speed * Dt, Distance - StopDistance);
		Self->SetActorLocationAndRotation(Here + Direction * Step, FRotator(0.f, Direction.Rotation().Yaw, 0.f));
		return Distance - Step <= StopDistance + KINDA_SMALL_NUMBER;
	}

	/** Turns Self to face Target on the horizontal plane. */
	inline void FaceToward(AActor* Self, const FVector& Target)
	{
		const FVector Delta = Target - Self->GetActorLocation();
		if (!FVector(Delta.X, Delta.Y, 0.f).IsNearlyZero()) Self->SetActorRotation(FRotator(0.f, Delta.Rotation().Yaw, 0.f));
	}
}
