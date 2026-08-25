#pragma once

#include "CoreMinimal.h"
#include "TacticsDeckLibrarySubsystem.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"

class UTacticsGameInstance;

/** Which pile the deck builder is editing. */
enum class ETacticsDeckEditZone : uint8
{
	Main,
	Reserves,
	Zones
};

/** Screen for building and saving a deck list. */
class STacticsDeckBuilderPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STacticsDeckBuilderPanel) {}
		SLATE_ARGUMENT(UTacticsGameInstance*, GameInstance)
	SLATE_END_ARGS()

	/** Builds the deck-builder layout. */
	void Construct(const FArguments& InArgs);

private:
	TWeakObjectPtr<UTacticsGameInstance> GameInstance;
	FTacticsDeckListData WorkingDeck;
	ETacticsDeckEditZone EditZone{ETacticsDeckEditZone::Main};
	FString DeckNameInput;
	FString StatusMessage;
	FString LibraryFilter;

	int32 CountMainCopies() const;
	int32 CountReservesCopies() const;
	int32 CountZoneCopies() const;
	int32 CopiesInZone(const FString& CardKey, ETacticsDeckEditZone Zone) const;
	bool CanAddCard(const FString& CardKey, ETacticsDeckEditZone Zone) const;
	void AddCard(const FString& CardKey);
	void RemoveCard(const FString& CardKey);
	void AddDefaultZone(const FString& EnergyKey, const FString& DisplayName);
	void RemoveOneZone(const FString& ZoneId);
	void RebuildLibraryList();
	void RebuildDeckList();
	void RefreshStatus();

	TSharedPtr<SVerticalBox> LibraryListBox;
	TSharedPtr<SVerticalBox> DeckListBox;
	TSharedPtr<STextBlock> LibraryCountLabel;
};
