#include "Core/Flow/ETMMenuPanel.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	// Layout in the panel's own frame: +X faces the player, +Y is the player's left, +Z is up. Centimetres.
	constexpr float TitleZ = 42.f;
	constexpr float BodyTopZ = 22.f;
	constexpr float BodyLineHeight = 7.5f;
	constexpr float ButtonWidth = 52.f;
	constexpr float ButtonHeight = 16.f;
	constexpr float ButtonGap = 14.f;

	UTextRenderComponent* MakeText(AActor* Owner, USceneComponent* Parent, float Size, const FColor& Color, UMaterialInterface* Material)
	{
		UTextRenderComponent* Text = NewObject<UTextRenderComponent>(Owner);
		if (Material) Text->SetTextMaterial(Material);
		Text->SetupAttachment(Parent);
		Text->SetHorizontalAlignment(EHTA_Center);
		Text->SetVerticalAlignment(EVRTA_TextCenter);
		Text->SetWorldSize(Size);
		Text->SetTextRenderColor(Color);
		Text->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Text->SetCastShadow(false);
		Text->RegisterComponent();
		return Text;
	}
}

AETMMenuPanel::AETMMenuPanel()
{
	PrimaryActorTick.bCanEverTick = false;
	// A hard reference, so the material is cooked into the build.
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> UnlitText(TEXT("/Engine/EngineMaterials/UnlitText.UnlitText"));
	TextMaterial = UnlitText.Object;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;

	TitleText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Title"));
	TitleText->SetupAttachment(Root);
	TitleText->SetRelativeLocation(FVector(0.f, 0.f, TitleZ));
	TitleText->SetHorizontalAlignment(EHTA_Center);
	TitleText->SetVerticalAlignment(EVRTA_TextCenter);
	TitleText->SetWorldSize(11.f);
	TitleText->SetTextRenderColor(FColor(255, 214, 90));
	TitleText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	TitleText->SetCastShadow(false);
	if (TextMaterial) TitleText->SetTextMaterial(TextMaterial);

	BodyText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Body"));
	BodyText->SetupAttachment(Root);
	BodyText->SetHorizontalAlignment(EHTA_Center);
	BodyText->SetVerticalAlignment(EVRTA_TextTop);
	BodyText->SetWorldSize(5.5f);
	BodyText->SetTextRenderColor(FColor(220, 235, 245));
	BodyText->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyText->SetCastShadow(false);
	if (TextMaterial) BodyText->SetTextMaterial(TextMaterial);
}

void AETMMenuPanel::Setup(const FText& Title, const FString& Body, const TArray<FETMMenuButton>& Buttons)
{
	TitleText->SetText(Title);
	TitleString = Title.ToString();
	BodyString = Body;
	BodyText->SetText(FText::FromString(Body.Replace(TEXT("\n"), TEXT("<br>"))));
	TArray<FString> Lines;
	Body.ParseIntoArray(Lines, TEXT("\n"), false);
	BodyText->SetRelativeLocation(FVector(0.f, 0.f, BodyTopZ));

	for (UActorComponent* Part : ButtonParts) if (Part) Part->DestroyComponent();
	ButtonParts.Reset();
	ButtonBoxes.Reset();
	ButtonIds.Reset();

	// Buttons in one row under the body text.
	const float RowZ = BodyTopZ - FMath::Max(1, Lines.Num()) * BodyLineHeight - ButtonHeight;
	const float RowWidth = Buttons.Num() * ButtonWidth + FMath::Max(0, Buttons.Num() - 1) * ButtonGap;
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	for (int32 i = 0; i < Buttons.Num(); ++i)
	{
		// +Y is the player's left, so the first button gets the largest Y.
		const float Y = RowWidth * 0.5f - ButtonWidth * 0.5f - i * (ButtonWidth + ButtonGap);
		const FVector At(0.f, Y, RowZ);

		UBoxComponent* Box = NewObject<UBoxComponent>(this);
		Box->SetupAttachment(Root);
		Box->SetRelativeLocation(At);
		Box->SetBoxExtent(FVector(3.f, ButtonWidth * 0.5f, ButtonHeight * 0.5f));
		Box->SetCollisionProfileName(TEXT("BlockAll"));
		Box->ComponentTags.Add(Buttons[i].Id);
		Box->RegisterComponent();

		UStaticMeshComponent* Plate = NewObject<UStaticMeshComponent>(this);
		Plate->SetupAttachment(Box);
		Plate->SetStaticMesh(Cube);
		Plate->SetRelativeLocation(FVector(-1.f, 0.f, 0.f));
		Plate->SetRelativeScale3D(FVector(2.f, ButtonWidth, ButtonHeight) / 100.f);
		Plate->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Plate->SetCastShadow(false);
		Plate->RegisterComponent();

		UTextRenderComponent* Label = MakeText(this, Box, 6.f, FColor(15, 25, 35), TextMaterial);
		Label->SetRelativeLocation(FVector(3.5f, 0.f, 0.f));
		Label->SetText(Buttons[i].Label);

		ButtonBoxes.Add(Box);
		ButtonParts.Append({ Box, Plate, Label });
		ButtonIds.Add(Buttons[i].Id);
	}

	bPressed = false;
	ArmedAt = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0) + ArmDelay;
}

bool AETMMenuPanel::IsArmed() const
{
	return !bPressed && GetWorld() && GetWorld()->GetTimeSeconds() >= ArmedAt;
}

bool AETMMenuPanel::Press(FName ButtonId)
{
	if (!IsArmed() || !ButtonIds.Contains(ButtonId)) return false;
	// One press per panel: at 10 shots a second the next shots must not pick again on whatever replaces it.
	bPressed = true;
	OnButtonPressed.Broadcast(ButtonId);
	return true;
}

float AETMMenuPanel::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (DamageEvent.IsOfType(FPointDamageEvent::ClassID))
	{
		const FPointDamageEvent& Point = static_cast<const FPointDamageEvent&>(DamageEvent);
		const UPrimitiveComponent* Hit = Point.HitInfo.GetComponent();
		for (int32 i = 0; Hit && i < ButtonBoxes.Num(); ++i)
		{
			if (ButtonBoxes[i].Get() == Hit) { Press(ButtonIds[i]); break; }
		}
	}
	return 0.f; // panels take no damage
}

FString AETMMenuPanel::GetTitleString() const
{
	return TitleString;
}

TArray<FName> AETMMenuPanel::GetButtonIds() const
{
	return ButtonIds;
}

UBoxComponent* AETMMenuPanel::GetButtonBox(FName ButtonId) const
{
	const int32 Index = ButtonIds.Find(ButtonId);
	return ButtonBoxes.IsValidIndex(Index) ? ButtonBoxes[Index].Get() : nullptr;
}
