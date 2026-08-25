#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "Tactics3DBoardPlayerController.generated.h"

class UTacticsGameInstance;
class ATactics3DWorldBoardActor;

/** Line traces under the cursor and forwards cell picks to `STacticsBoardPanel`. */
UCLASS()
class TACTICSGAMEUNREAL_API ATactics3DBoardPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ATactics3DBoardPlayerController();

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaSeconds) override;

	/** Selects the board cell under the cursor. */
	void OnPrimaryClick();
	/** Cancels the current board selection. */
	void OnSecondaryClick();
	void OnShortcutEscape();
	void OnShortcutCancel();
	void OnShortcutUndo();
	void OnShortcutPass();
	void OnShortcutConfirm();
	void OnZoomIn();
	void OnZoomOut();
	void OnMiddleMousePressed();
	void OnMiddleMouseReleased();
	void OnPanUpPressed();
	void OnPanUpReleased();
	void OnPanDownPressed();
	void OnPanDownReleased();
	void OnPanLeftPressed();
	void OnPanLeftReleased();
	void OnPanRightPressed();
	void OnPanRightReleased();
	void OnHandHotkey1();
	void OnHandHotkey2();
	void OnHandHotkey3();
	void OnHandHotkey4();
	void OnHandHotkey5();
	void OnHandHotkey6();
	void OnHandHotkey7();
	void OnHandHotkey8();
	void OnHandHotkey9();
	/** Plays or inspects the hand card in the given 1-based slot. */
	void ForwardHandHotkey(int32 Slot1Based);
	/** Converts a cursor hit into board column and row. */
	bool TraceBoardCellUnderCursor(int& OutWx, int& OutWy) const;

	/** Resolves the world board actor, caching it so PlayerTick avoids a per-frame Cast + actor scan. */
	ATactics3DWorldBoardActor* ResolveWorldBoard() const;

private:
	/** Cached once in BeginPlay - avoids a Cast<> on every PlayerTick frame. */
	UPROPERTY()
	TObjectPtr<UTacticsGameInstance> CachedGameInstance;

	/** Lazily resolved board actor; re-resolved only when the weak ref goes stale (map reload, etc.). */
	mutable TWeakObjectPtr<ATactics3DWorldBoardActor> CachedWorldBoard;

	bool bMiddleMousePanning{false};
	float LastPanMouseX{0.f};
	float LastPanMouseY{0.f};
	bool bPanUp{false};
	bool bPanDown{false};
	bool bPanLeft{false};
	bool bPanRight{false};
};
