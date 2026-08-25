#include "TacticsBoardViewPawn.h"

#include "Components/SceneComponent.h"

ATacticsBoardViewPawn::ATacticsBoardViewPawn()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(Root);
}
