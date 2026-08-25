#include "TacticsBoardPlayerController.h"
#include "TacticsGameInstance.h"
#include "Engine/World.h"
#include "InputCoreTypes.h"

ATacticsBoardPlayerController::ATacticsBoardPlayerController()
{
	bShowMouseCursor = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
}

void ATacticsBoardPlayerController::ApplySlateFriendlyMouseCursor(APlayerController* PC)
{
	if (!PC) {
		return;
	}
	if (const UWorld* World = PC->GetWorld(); World && World->GetNetMode() == NM_DedicatedServer) {
		return;
	}

	PC->bShowMouseCursor = true;
	PC->SetShowMouseCursor(true);
	PC->bEnableClickEvents = true;
	PC->bEnableMouseOverEvents = true;
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);

	FInputModeGameAndUI InputMode;
	InputMode.SetHideCursorDuringCapture(false);
	InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	PC->SetInputMode(InputMode);
}

void ATacticsBoardPlayerController::BeginPlay()
{
	Super::BeginPlay();
	ApplySlateFriendlyMouseCursor(this);
}

void ATacticsBoardPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::Escape, IE_Pressed, this, &ATacticsBoardPlayerController::OnToggleOptions);
		InputComponent->BindKey(EKeys::BackSpace, IE_Pressed, this, &ATacticsBoardPlayerController::OnToggleOptions);
	}
}

void ATacticsBoardPlayerController::OnToggleOptions()
{
	if (UTacticsGameInstance* TGI = Cast<UTacticsGameInstance>(GetGameInstance()))
	{
		TGI->ToggleOptionsOverlay();
	}
}

void ATacticsBoardPlayerController::PlayerTick(const float DeltaSeconds)
{
	Super::PlayerTick(DeltaSeconds);

	// Slate button capture can occasionally hide the OS cursor; restore immediately.
	if (!bShowMouseCursor) {
		ApplySlateFriendlyMouseCursor(this);
	}
}