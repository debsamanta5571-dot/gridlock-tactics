#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "TacticsBoardViewPawn.generated.h"

/** Static pawn for the tactics UI map: no flying spectator controls; input is ignored on the pawn. */
UCLASS()
class TACTICSGAMEUNREAL_API ATacticsBoardViewPawn : public APawn
{
	GENERATED_BODY()

public:
	ATacticsBoardViewPawn();
};
