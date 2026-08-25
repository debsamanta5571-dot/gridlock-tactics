#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "TacticsBoardPlayerController.generated.h"

/** Keeps the mouse free for Slate; disables default world navigation / spectator look on stray clicks. */
UCLASS()
class TACTICSGAMEUNREAL_API ATacticsBoardPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATacticsBoardPlayerController();

	/** Game+UI input with cursor visible even when Slate captures the mouse (menus + board HUD). */
	static void ApplySlateFriendlyMouseCursor(APlayerController* PC);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaSeconds) override;

	void OnToggleOptions();
};
