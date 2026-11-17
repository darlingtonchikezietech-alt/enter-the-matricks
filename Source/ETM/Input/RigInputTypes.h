// Control scheme and vent statistics types shared by the input router, the controller and the settings UI.
// Spec: bible section 4 "Input mapping", P-11, P-12. Owned by T-0012.
#pragma once

#include "CoreMinimal.h"
#include "RigInputTypes.generated.h"

/** The two mappings under test in M1 (P-12). Fire, pause and confirm are the same in both. */
UENUM(BlueprintType)
enum class ERigControlScheme : uint8
{
	/** Bible mapping: grip vents that cannon (above 0.85 for 100 ms); A or X activates the brace. */
	GripVent,
	/** P-12 alternative: hold X to vent left and A to vent right; either thumbstick click activates the brace. */
	ButtonVent,
};

/** Counters for the M1 accidental-vent measurement (P-12). One set per control scheme. */
USTRUCT(BlueprintType)
struct FRigVentStats
{
	GENERATED_BODY()

	/** Seconds of combat input processed under this scheme. */
	UPROPERTY(BlueprintReadOnly, Category = "Vent") float PlaySeconds = 0.f;
	/** Every time a vent opened. */
	UPROPERTY(BlueprintReadOnly, Category = "Vent") int32 VentActivations = 0;
	/** Vents that opened while that hand's trigger was held: the vent blocked a shot the player was trying to fire. */
	UPROPERTY(BlueprintReadOnly, Category = "Vent") int32 VentsWhileFiring = 0;
	/** Vents shorter than the short-vent limit: too brief to be a deliberate cool-down. */
	UPROPERTY(BlueprintReadOnly, Category = "Vent") int32 ShortVents = 0;
	/** Vents that were either of the above. This is the number P-12 asks M1 to compare between schemes. */
	UPROPERTY(BlueprintReadOnly, Category = "Vent") int32 SuspectVents = 0;

	float GetSuspectVentsPerMinute() const { return PlaySeconds > 0.f ? SuspectVents * 60.f / PlaySeconds : 0.f; }
	float GetSuspectFraction() const { return VentActivations > 0 ? static_cast<float>(SuspectVents) / VentActivations : 0.f; }
};
