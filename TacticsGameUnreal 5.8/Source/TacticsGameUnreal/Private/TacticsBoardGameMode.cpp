#include "TacticsBoardGameMode.h"

#include "TacticsBoardPlayerController.h"
#include "TacticsBoardViewPawn.h"

ATacticsBoardGameMode::ATacticsBoardGameMode()
{
	DefaultPawnClass = ATacticsBoardViewPawn::StaticClass();
	PlayerControllerClass = ATacticsBoardPlayerController::StaticClass();
}
