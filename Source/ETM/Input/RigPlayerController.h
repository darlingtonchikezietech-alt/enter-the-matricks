// ARigPlayerController: reads the Touch controllers through Enhanced Input and drives AXRSeatedRigPawn.
// Spec: bible section 4 "Input mapping", P-11, P-12. Owned by T-0012.
//
// The input actions and the mapping context are built in code, not as assets, so they merge
// cleanly and are covered by tests. The mapping context always maps every button; which button
// means what is decided by the control scheme in Input/RigInputModel.h, so switching scheme
// never rebuilds the mappings.
//
// To use it, a game mode must set this as its PlayerControllerClass and AXRSeatedRigPawn as its pawn.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Input/RigInputModel.h"
#include "RigPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRigPauseRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRigConfirmInteraction);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRigControlSchemeChanged, ERigControlScheme, NewScheme);

UCLASS()
class ETM_API ARigPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ARigPlayerController();

	virtual void PlayerTick(float DeltaTime) override;

	/** Switches mapping at any time (settings menu). Saved to the input slot when bSave is set. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Input") void SetControlScheme(ERigControlScheme NewScheme, bool bSave = true);
	UFUNCTION(BlueprintPure, Category = "ETM|Input") ERigControlScheme GetControlScheme() const { return Router.GetScheme(); }

	/** Accidental-vent counters for one scheme since the last reset (P-12, M1 playtest). */
	UFUNCTION(BlueprintPure, Category = "ETM|Input") FRigVentStats GetVentStats(ERigControlScheme Scheme) const { return Router.GetStats(Scheme); }
	UFUNCTION(BlueprintPure, Category = "ETM|Input") float GetSuspectVentsPerMinute(ERigControlScheme Scheme) const { return Router.GetStats(Scheme).GetSuspectVentsPerMinute(); }
	UFUNCTION(BlueprintCallable, Category = "ETM|Input") void ResetVentStats() { Router.ResetStats(); }
	/** Writes both schemes' counters to the log. Also runs when play ends. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Input") void LogVentStats() const;

	/** The mapping context, built on first use. Exposed for tests and for debugging tools. */
	const UInputMappingContext* GetOrBuildMappingContext();
	/** Feeds one frame of raw controller state through the router and applies the result to the pawn. */
	void ProcessRawInput(const ETMInput::FRawInput& Raw, float DeltaTime);

	/** Menu button. The pause flow (T-0030) listens to this. */
	UPROPERTY(BlueprintAssignable, Category = "ETM|Input") FOnRigPauseRequested OnPauseRequested;
	/** B or Y: contextual objective interaction. */
	UPROPERTY(BlueprintAssignable, Category = "ETM|Input") FOnRigConfirmInteraction OnConfirmInteraction;
	UPROPERTY(BlueprintAssignable, Category = "ETM|Input") FOnRigControlSchemeChanged OnControlSchemeChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UInputAction* MakeAction(const TCHAR* Name, bool bAnalog);
	ETMInput::FRawInput ReadRawInput() const;
	void LoadSavedScheme();

	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> MappingContext;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LeftTriggerAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RightTriggerAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LeftGripAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RightGripAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LeftPrimaryAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RightPrimaryAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LeftSecondaryAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RightSecondaryAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> LeftStickClickAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> RightStickClickAction;
	UPROPERTY(Transient) TObjectPtr<UInputAction> MenuAction;

	ETMInput::FRouter Router;
};
