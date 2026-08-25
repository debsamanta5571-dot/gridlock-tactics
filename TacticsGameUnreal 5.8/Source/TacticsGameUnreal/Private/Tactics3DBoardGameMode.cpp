#include "Tactics3DBoardGameMode.h"

#include "TacticsGameInstance.h"
#include "Tactics3DBoardPlayerController.h"
#include "Tactics3DBoardViewPawn.h"
#include "Tactics3DWorldBoardActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"

ATactics3DBoardGameMode::ATactics3DBoardGameMode()
{
	DefaultPawnClass = ATactics3DBoardViewPawn::StaticClass();
	PlayerControllerClass = ATactics3DBoardPlayerController::StaticClass();
}

void ATactics3DBoardGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (const UTacticsGameInstance* GI = Cast<UTacticsGameInstance>(GetGameInstance())) {
		if (!GI->ShouldRun3DMatchPresentation()) {
			return;
		}
	}
	SpawnMatchWorldPresentation();
}

void ATactics3DBoardGameMode::SpawnMatchWorldPresentation()
{
	UWorld* W = GetWorld();
	if (!W) {
		return;
	}

	if (WorldBoardActor && IsValid(WorldBoardActor)) {
		return;
	}

	bool bHasLight = false;
	for (TActorIterator<ADirectionalLight> It(W); It; ++It) {
		bHasLight = true;
		break;
	}
	if (!bHasLight) {
		ADirectionalLight* Dir = W->SpawnActor<ADirectionalLight>(ADirectionalLight::StaticClass(), FVector(-800.f, -200.f, 1200.f),
			FRotator(-58.f, 35.f, 0.f));
		if (Dir) {
			if (UDirectionalLightComponent* Dlc = Cast<UDirectionalLightComponent>(Dir->GetLightComponent())) {
				Dlc->SetIntensity(7.5f);
			}
		}
	}

	bool bHasStart = false;
	for (TActorIterator<APlayerStart> It(W); It; ++It) {
		bHasStart = true;
		break;
	}
	if (!bHasStart) {
		W->SpawnActor<APlayerStart>(APlayerStart::StaticClass(), FTransform(FRotator::ZeroRotator, FVector(0.f, 0.f, 320.f)));
	}

	FActorSpawnParameters Sp;
	Sp.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	WorldBoardActor = W->SpawnActor<ATactics3DWorldBoardActor>(ATactics3DWorldBoardActor::StaticClass(), FTransform::Identity, Sp);
}

void ATactics3DBoardGameMode::AlignPlayerViewToBoard()
{
	if (!WorldBoardActor || !WorldBoardActor->HasVisualGridBounds()) {
		return;
	}

	UWorld* W = GetWorld();
	if (!W) {
		return;
	}

	APlayerController* PC = W->GetFirstPlayerController();
	if (!PC) {
		return;
	}

	if (ATactics3DBoardViewPawn* ViewPawn = Cast<ATactics3DBoardViewPawn>(PC->GetPawn())) {
		ViewPawn->AlignViewToBoard(WorldBoardActor);
	}
}
