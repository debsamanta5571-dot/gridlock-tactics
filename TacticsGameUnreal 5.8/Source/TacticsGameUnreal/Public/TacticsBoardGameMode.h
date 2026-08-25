#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TacticsBoardGameMode.generated.h"

/** Minimal game mode for the tactics-only map: static view pawn + controller with look/move input ignored (Slate-first). */
UCLASS()
class TACTICSGAMEUNREAL_API ATacticsBoardGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATacticsBoardGameMode();
};
