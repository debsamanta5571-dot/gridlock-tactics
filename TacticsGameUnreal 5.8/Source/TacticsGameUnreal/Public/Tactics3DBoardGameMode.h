#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Tactics3DBoardGameMode.generated.h"

class ATactics3DWorldBoardActor;

/** Spawns the instanced 3D grid; keeps Slate overlay for hand / stack / actions via `UTacticsGameInstance`. */
UCLASS()
class TACTICSGAMEUNREAL_API ATactics3DBoardGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATactics3DBoardGameMode();

	/** Spawns the 3D board presentation when the match map starts. */
	virtual void BeginPlay() override;

	/** Spawns lighting, player start, and the instanced 3D grid (idempotent for light/start). */
	void SpawnMatchWorldPresentation();

	/** Snap the view pawn to the board center once grid bounds are known. */
	void AlignPlayerViewToBoard();

	UPROPERTY()
	TObjectPtr<ATactics3DWorldBoardActor> WorldBoardActor;
};
