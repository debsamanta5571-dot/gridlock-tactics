#pragma once

#include "CoreMinimal.h"
#include "Input/Reply.h"
#include "Styling/SlateColor.h"
#include "Widgets/SCompoundWidget.h"

class UTacticsGameInstance;

/** Start screen: Play vs AI, Host LAN, and Join. */
class STacticsMainMenuPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STacticsMainMenuPanel) {}
		SLATE_ARGUMENT(UTacticsGameInstance*, GameInstance)
	SLATE_END_ARGS()

	/** Builds the start-screen layout. */
	void Construct(const FArguments& InArgs);
	/** Rebuilds lobby and vs-AI status from the game instance. */
	void RefreshUi();

private:
	TWeakObjectPtr<UTacticsGameInstance> GameInstance;
	FString JoinPortText = TEXT("8788");
	FString HostPortText = TEXT("8788");
	FString SelectedDeckKey = TEXT("militia_starforged");
	FString SelectedAiDeckKey = TEXT("dieselheart_endless_assault");
	FString SelectedDifficulty = TEXT("normal");
	FString StatusMessage;
	bool bShowJoinField{false};
	bool bShowHostField{false};
	bool bShowVsAiSetup{false};

	TSharedRef<SWidget> BuildTitleCard();
	TSharedRef<SWidget> BuildLobbyCard();
	TSharedRef<SWidget> BuildVsAiCard();
	/** Deck chips for the local player's preselected lists. */
	TSharedRef<SWidget> BuildDeckPicker();
	/** Opponent deck and difficulty for Play vs AI. */
	TSharedRef<SWidget> BuildAiSetup();
	TSharedRef<SWidget> BuildDeckChipRow(int32 StartIndex, int32 Count, bool bAiDeck);
	TSharedRef<SWidget> MakePlaque(
		const FText& Label, FOnClicked OnClicked, const FString& ControlId, bool bPrimary,
		const FString& Rank = FString(), bool bCompact = false, int32 CompactGroup = 0);
	TArray<FString> PresetDeckKeys() const;
	void SelectDeckAt(int32 Index);
	void SelectAiDeckAt(int32 Index);
	void SelectDifficulty(const FString& Difficulty);
	bool IsInMultiplayerLobby() const;
	bool IsJoiningLobby() const;
	void PushMatchSettings();
	void RebuildDeckOptions();
	void ApplySelectedDeckToLobby();
	FReply OpenVsAiSetup();
	FReply CloseVsAiSetup();
	FReply StartVsAi();
	FReply StartHost();
	FReply StartJoin();
	bool IsShowingVsAiSetup() const { return bShowVsAiSetup && !IsInMultiplayerLobby(); }
};
