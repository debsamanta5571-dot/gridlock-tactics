#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class UTacticsGameInstance;

/** Match settings: board cell spacing and save/load of visual prefs. */
class STacticsSettingsPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STacticsSettingsPanel) {}
		SLATE_ARGUMENT(UTacticsGameInstance*, GameInstance)
		SLATE_EVENT(FSimpleDelegate, OnBack)
	SLATE_END_ARGS()

	/** Builds the settings panel. */
	void Construct(const FArguments& InArgs);

private:
	TWeakObjectPtr<UTacticsGameInstance> GameInstance;
	FSimpleDelegate OnBack;
	FString StatusMessage;

	void RefreshStatus();
	/** Changes 3D board cell spacing by Delta and refreshes the label. */
	FReply AdjustCellSpacing(float Delta);
	/** Writes visual settings to GameUserSettings.ini. */
	FReply SaveSettings();
	FReply ResetDefaults();
	TSharedRef<SWidget> BuildCellSpacingControl();
};
