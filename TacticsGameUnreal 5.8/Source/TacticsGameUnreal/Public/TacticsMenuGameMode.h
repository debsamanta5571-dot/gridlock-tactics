#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "TacticsMenuGameMode.generated.h"

/** Main menu / deck builder - no 3D tactics board spawn. */
UCLASS()
class TACTICSGAMEUNREAL_API ATacticsMenuGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ATacticsMenuGameMode();
};
