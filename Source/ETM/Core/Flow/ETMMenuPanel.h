// AETMMenuPanel: a world-space menu or results panel (bible section 11: results on a stable world-space
// panel, UI at about 1-2 m). The player picks a button by aiming a cannon at it and firing: the cannon's
// point damage lands on the button's box. No laser pointer or extra input is needed. Owned by T-0031.
//
// Text uses UTextRenderComponent so the panel needs no widget or font assets. Art can replace the look later
// (Content/ETM/UI/Menus) without changing how buttons are pressed.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ETMMenuPanel.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UTextRenderComponent;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMenuButtonPressed, FName, ButtonId);

USTRUCT(BlueprintType)
struct FETMMenuButton
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu") FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Menu") FText Label;
};

UCLASS()
class ETM_API AETMMenuPanel : public AActor
{
	GENERATED_BODY()

public:
	AETMMenuPanel();

	/** Fills the panel. Body lines are separated by "\n". Buttons sit in one row below the body, left to right. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Menu") void Setup(const FText& Title, const FString& Body, const TArray<FETMMenuButton>& Buttons);

	/** Presses a button as a shot would. False if it does not exist or the panel is not armed yet. */
	UFUNCTION(BlueprintCallable, Category = "ETM|Menu") bool Press(FName ButtonId);

	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

	UFUNCTION(BlueprintPure, Category = "ETM|Menu") bool IsArmed() const;
	UFUNCTION(BlueprintPure, Category = "ETM|Menu") FString GetTitleString() const;
	UFUNCTION(BlueprintPure, Category = "ETM|Menu") FString GetBodyString() const { return BodyString; }
	UFUNCTION(BlueprintPure, Category = "ETM|Menu") TArray<FName> GetButtonIds() const;
	UFUNCTION(BlueprintPure, Category = "ETM|Menu") UBoxComponent* GetButtonBox(FName ButtonId) const;

	/** Seconds after Setup before a hit counts, so a trigger already held (or a shot in flight) cannot pick for you. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ETM|Menu", meta = (ClampMin = "0")) float ArmDelay = 0.75f;

	UPROPERTY(BlueprintAssignable, Category = "ETM|Menu") FOnMenuButtonPressed OnButtonPressed;

	UPROPERTY(VisibleAnywhere, Category = "ETM|Menu") TObjectPtr<USceneComponent> Root;
	UPROPERTY(VisibleAnywhere, Category = "ETM|Menu") TObjectPtr<UTextRenderComponent> TitleText;
	UPROPERTY(VisibleAnywhere, Category = "ETM|Menu") TObjectPtr<UTextRenderComponent> BodyText;
	/** Unlit, so text reads the same however the scene is lit (T-0020: lit text turned black when the light moved). */
	UPROPERTY(EditAnywhere, Category = "ETM|Menu") TObjectPtr<UMaterialInterface> TextMaterial;

private:
	UPROPERTY(Transient) TArray<TObjectPtr<UBoxComponent>> ButtonBoxes;
	UPROPERTY(Transient) TArray<TObjectPtr<UActorComponent>> ButtonParts;
	TArray<FName> ButtonIds;
	FString TitleString;
	FString BodyString;
	double ArmedAt = 0.0;
	bool bPressed = false;
};
