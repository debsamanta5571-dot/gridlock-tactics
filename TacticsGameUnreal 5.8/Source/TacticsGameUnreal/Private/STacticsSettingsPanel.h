#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"

class UTacticsGameInstance;

/** Options overlay: graphics quality, return to menu, quit. */
class STacticsSettingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STacticsSettingsPanel) {}
		SLATE_ARGUMENT(UTacticsGameInstance*, GameInstance)
		SLATE_ARGUMENT(bool, bInMatch)
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	/** Builds the options overlay. */
	void Construct(const FArguments& InArgs);

	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;

private:
	TWeakObjectPtr<UTacticsGameInstance> GameInstance;
	FSimpleDelegate OnBack;
	bool bInMatch{false};

	int32 CurrentGraphicsLevel() const;
	FReply SetGraphicsLevel(int32 Level);
	FReply CloseOverlay();
	FReply ReturnToMainMenu();
	FReply QuitGame();
	TSharedRef<SWidget> BuildGraphicsRow();
};
