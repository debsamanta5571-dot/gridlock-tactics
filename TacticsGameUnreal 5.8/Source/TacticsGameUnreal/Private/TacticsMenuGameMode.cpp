#include "TacticsMenuGameMode.h"

#include "TacticsBoardPlayerController.h"
#include "TacticsBoardViewPawn.h"

ATacticsMenuGameMode::ATacticsMenuGameMode()
{
	DefaultPawnClass = ATacticsBoardViewPawn::StaticClass();
	PlayerControllerClass = ATacticsBoardPlayerController::StaticClass();
}
