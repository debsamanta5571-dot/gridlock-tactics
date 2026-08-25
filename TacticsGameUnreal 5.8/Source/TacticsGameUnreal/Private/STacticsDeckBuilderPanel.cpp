#include "STacticsDeckBuilderPanel.h"
#include "TacticsCardText.h"

#include "TacticsGameInstance.h"
#include "tactics/cards/cards.hpp"
#include "tactics/common/types.hpp"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"

namespace
{
constexpr FLinearColor kBg(0.03f, 0.05f, 0.1f, 0.94f);
constexpr FLinearColor kDeckBuilderText(0.92f, 0.92f, 0.92f, 1.f);
constexpr int32 kMaxMain = tactics::kMaxMainDeckCards;
constexpr int32 kMaxPerCard = tactics::kMaxCopiesPerCard;
constexpr int32 kMaxReserves = tactics::kMaxReservesSize;
constexpr int32 kMaxZones = tactics::kMaxZoneDeckSize;

int32 SumCopies(const TArray<FTacticsDeckCardEntry>& Entries)
{
	int32 N = 0;
	for (const FTacticsDeckCardEntry& E : Entries) {
		N += E.Copies;
	}
	return N;
}

int32 FindEntryIndex(TArray<FTacticsDeckCardEntry>& Entries, const FString& Key)
{
	for (int32 i = 0; i < Entries.Num(); ++i) {
		if (Entries[i].CardKey == Key) {
			return i;
		}
	}
	return INDEX_NONE;
}
}  // namespace

void STacticsDeckBuilderPanel::Construct(const FArguments& InArgs)
{
	GameInstance = InArgs._GameInstance;
	if (UTacticsDeckLibrarySubsystem* Lib = GameInstance.IsValid()
			? GameInstance->GetSubsystem<UTacticsDeckLibrarySubsystem>()
			: nullptr) {
		Lib->EnsureGameplayCatalogsLoaded();
		FString Err;
		if (!Lib->LoadDeckByKey(Lib->GetActiveDeckKey(), WorkingDeck, Err)) {
			WorkingDeck = Lib->MakeDefaultBuilderDeck();
		}
		DeckNameInput = WorkingDeck.Key;
	} else {
		WorkingDeck.Key = TEXT("my_deck");
		DeckNameInput = WorkingDeck.Key;
	}

	ChildSlot
		[SNew(SBorder).BorderBackgroundColor(kBg).Padding(20.f)
				[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(0.45f).Padding(0.f, 0.f, 12.f, 0.f)
						[SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
								[SNew(STextBlock)
										.Text(FText::FromString(TEXT("Card library")))
										.Font(TacticsCardText::DesignFont(TEXT("Bold"), 18))
										.ColorAndOpacity(kDeckBuilderText)]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
								[SNew(SEditableTextBox)
										.HintText(FText::FromString(TEXT("Search (name, rules, type:unit, keyword:haste, cost:<=2, red...)")))
										.OnTextChanged_Lambda([this](const FText& T) {
											LibraryFilter = T.ToString();
											RebuildLibraryList();
										})]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
								[SAssignNew(LibraryCountLabel, STextBlock)
										.Text(FText::FromString(TEXT("...")))
										.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.f))]
							+ SVerticalBox::Slot().FillHeight(1.f)
								[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(LibraryListBox, SVerticalBox)]]]
					+ SHorizontalBox::Slot().FillWidth(0.55f)
						[SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()
								[SNew(STextBlock)
										.Text(FText::FromString(TEXT("Deck builder")))
										.Font(TacticsCardText::DesignFont(TEXT("Bold"), 22))
										.ColorAndOpacity(kDeckBuilderText)]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f)
								[SNew(SHorizontalBox)
									+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
										[SNew(SButton).TextStyle(&TacticsCardText::DefaultTextStyle(11, kDeckBuilderText))
												.Text(FText::FromString(FString::Printf(TEXT("Main (%d)"), kMaxMain)))
												.OnClicked_Lambda([this]() {
													EditZone = ETacticsDeckEditZone::Main;
													RebuildLibraryList();
													RebuildDeckList();
													return FReply::Handled();
												})]
									+ SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
										[SNew(SButton).TextStyle(&TacticsCardText::DefaultTextStyle(11, kDeckBuilderText))
												.Text(FText::FromString(FString::Printf(TEXT("Reserves (%d)"), kMaxReserves)))
												.OnClicked_Lambda([this]() {
													EditZone = ETacticsDeckEditZone::Reserves;
													RebuildLibraryList();
													RebuildDeckList();
													return FReply::Handled();
												})]
									+ SHorizontalBox::Slot().AutoWidth()
										[SNew(SButton).TextStyle(&TacticsCardText::DefaultTextStyle(11, kDeckBuilderText))
												.Text(FText::FromString(FString::Printf(TEXT("Territories (%d)"), kMaxZones)))
												.OnClicked_Lambda([this]() {
													EditZone = ETacticsDeckEditZone::Zones;
													RebuildLibraryList();
													RebuildDeckList();
													return FReply::Handled();
												})]]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f)
								[SNew(STextBlock)
										.Text_Lambda([this]() {
											return FText::FromString(FString::Printf(
												TEXT("Main %d/%d · Reserves %d/%d · Territories %d/%d"),
												CountMainCopies(), kMaxMain, CountReservesCopies(), kMaxReserves,
												CountZoneCopies(), kMaxZones));
										})
										.ColorAndOpacity(kDeckBuilderText)]
							+ SVerticalBox::Slot().FillHeight(1.f).Padding(0.f, 8.f)
								[SNew(SScrollBox) + SScrollBox::Slot()[SAssignNew(DeckListBox, SVerticalBox)]]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f)
								[SNew(SEditableTextBox)
										.Text(FText::FromString(DeckNameInput))
										.HintText(FText::FromString(TEXT("Deck file name")))
										.OnTextChanged_Lambda([this](const FText& T) { DeckNameInput = T.ToString(); })]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
								[SNew(SButton).TextStyle(&TacticsCardText::DefaultTextStyle(11, kDeckBuilderText))
										.Text(FText::FromString(TEXT("Save deck")))
										.OnClicked_Lambda([this]() {
											if (!GameInstance.IsValid()) {
												return FReply::Handled();
											}
											UTacticsDeckLibrarySubsystem* Lib =
												GameInstance->GetSubsystem<UTacticsDeckLibrarySubsystem>();
											if (!Lib) {
												return FReply::Handled();
											}
											WorkingDeck.Key = DeckNameInput.TrimStartAndEnd();
											FString Err;
											if (Lib->SaveDeck(WorkingDeck, Err)) {
												StatusMessage = FString::Printf(TEXT("Saved \"%s\"."), *WorkingDeck.Key);
												Lib->SetActiveDeckKey(WorkingDeck.Key);
											} else {
												StatusMessage = Err;
											}
											return FReply::Handled();
										})]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f)
								[SNew(SButton).TextStyle(&TacticsCardText::DefaultTextStyle(11, kDeckBuilderText))
										.Text(FText::FromString(TEXT("Back to main menu")))
										.OnClicked_Lambda([this]() {
											if (GameInstance.IsValid()) {
												GameInstance->ShowMainMenu();
											}
											return FReply::Handled();
										})]
							+ SVerticalBox::Slot().AutoHeight()
								[SNew(STextBlock)
										.Text_Lambda([this]() { return FText::FromString(StatusMessage); })
										.ColorAndOpacity(FLinearColor(0.75f, 0.9f, 1.f, 1.f))
										.AutoWrapText(true)]]]];
	RebuildLibraryList();
	RebuildDeckList();
	RefreshStatus();
}

int32 STacticsDeckBuilderPanel::CountMainCopies() const { return SumCopies(WorkingDeck.MainDeck); }
int32 STacticsDeckBuilderPanel::CountReservesCopies() const { return SumCopies(WorkingDeck.Reserves); }
int32 STacticsDeckBuilderPanel::CountZoneCopies() const
{
	int32 N = 0;
	for (const FTacticsDeckZoneEntry& Z : WorkingDeck.Zones) {
		N += Z.Copies;
	}
	return N;
}

int32 STacticsDeckBuilderPanel::CopiesInZone(const FString& CardKey, ETacticsDeckEditZone Zone) const
{
	if (Zone == ETacticsDeckEditZone::Zones) {
		return 0;
	}
	const TArray<FTacticsDeckCardEntry>& Entries =
		Zone == ETacticsDeckEditZone::Main ? WorkingDeck.MainDeck : WorkingDeck.Reserves;
	for (const FTacticsDeckCardEntry& E : Entries) {
		if (E.CardKey == CardKey) {
			return E.Copies;
		}
	}
	return 0;
}

bool STacticsDeckBuilderPanel::CanAddCard(const FString& CardKey, ETacticsDeckEditZone Zone) const
{
	if (Zone == ETacticsDeckEditZone::Main) {
		return CountMainCopies() < kMaxMain && CopiesInZone(CardKey, Zone) < kMaxPerCard;
	}
	if (Zone == ETacticsDeckEditZone::Reserves) {
		return CountReservesCopies() < kMaxReserves && CopiesInZone(CardKey, Zone) < kMaxPerCard;
	}
	return false;
}

void STacticsDeckBuilderPanel::AddCard(const FString& CardKey)
{
	if (!CanAddCard(CardKey, EditZone)) {
		return;
	}
	TArray<FTacticsDeckCardEntry>& Entries =
		EditZone == ETacticsDeckEditZone::Main ? WorkingDeck.MainDeck : WorkingDeck.Reserves;
	const int32 Idx = FindEntryIndex(Entries, CardKey);
	if (Idx == INDEX_NONE) {
		FTacticsDeckCardEntry Row;
		Row.CardKey = CardKey;
		Row.Copies = 1;
		Entries.Add(Row);
	} else {
		Entries[Idx].Copies += 1;
	}
	RebuildDeckList();
	RebuildLibraryList();
	RefreshStatus();
}

void STacticsDeckBuilderPanel::RemoveCard(const FString& CardKey)
{
	TArray<FTacticsDeckCardEntry>& Entries =
		EditZone == ETacticsDeckEditZone::Main ? WorkingDeck.MainDeck : WorkingDeck.Reserves;
	const int32 Idx = FindEntryIndex(Entries, CardKey);
	if (Idx == INDEX_NONE) {
		return;
	}
	Entries[Idx].Copies -= 1;
	if (Entries[Idx].Copies <= 0) {
		Entries.RemoveAt(Idx);
	}
	RebuildDeckList();
	RebuildLibraryList();
	RefreshStatus();
}

void STacticsDeckBuilderPanel::AddDefaultZone(const FString& EnergyKey, const FString& DisplayName)
{
	if (CountZoneCopies() >= kMaxZones) {
		return;
	}
	FTacticsDeckZoneEntry Z;
	Z.ZoneId = FString::Printf(TEXT("%s_%d"), *EnergyKey, WorkingDeck.Zones.Num());
	Z.Name = DisplayName;
	Z.EnergyKey = EnergyKey;
	Z.EnergyAmount = 1;
	Z.Copies = 1;
	WorkingDeck.Zones.Add(Z);
	RebuildDeckList();
	RefreshStatus();
}

void STacticsDeckBuilderPanel::RemoveOneZone(const FString& ZoneId)
{
	for (int32 i = 0; i < WorkingDeck.Zones.Num(); ++i) {
		if (WorkingDeck.Zones[i].ZoneId == ZoneId) {
			WorkingDeck.Zones[i].Copies -= 1;
			if (WorkingDeck.Zones[i].Copies <= 0) {
				WorkingDeck.Zones.RemoveAt(i);
			}
			break;
		}
	}
	RebuildDeckList();
	RefreshStatus();
}

void STacticsDeckBuilderPanel::RebuildLibraryList()
{
	if (!LibraryListBox.IsValid()) {
		return;
	}
	LibraryListBox->ClearChildren();
	UTacticsDeckLibrarySubsystem* Lib =
		GameInstance.IsValid() ? GameInstance->GetSubsystem<UTacticsDeckLibrarySubsystem>() : nullptr;
	if (!Lib) {
		if (LibraryCountLabel.IsValid()) {
			LibraryCountLabel->SetText(FText::FromString(TEXT("(subsystem unavailable)")));
		}
		return;
	}
	const TArray<FString> MatchedKeys = Lib->SearchCardKeys(LibraryFilter);
	if (LibraryCountLabel.IsValid()) {
		const int32 TotalCards = Lib->ListCardKeysSorted().Num();
		LibraryCountLabel->SetText(FText::FromString(
			FString::Printf(TEXT("%d shown / %d total"), MatchedKeys.Num(), TotalCards)));
	}
	for (const FString& Key : MatchedKeys) {
		FString Name = Key;
		Lib->TryGetCardDisplayName(Key, Name);
		const FString CardKey = Key;
		LibraryListBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
					[SNew(STextBlock)
							.Text(FText::FromString(FString::Printf(TEXT("%s (%s)"), *Name, *Key)))
							.ColorAndOpacity(kDeckBuilderText)]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4.f, 0.f)
					[SNew(SButton).TextStyle(&TacticsCardText::DefaultTextStyle(11, kDeckBuilderText))
							.Text(FText::FromString(TEXT("+")))
							.IsEnabled_Lambda([this, CardKey]() { return CanAddCard(CardKey, EditZone); })
							.OnClicked_Lambda([this, CardKey]() {
								if (EditZone != ETacticsDeckEditZone::Zones) {
									AddCard(CardKey);
								}
								return FReply::Handled();
							})]];
	}
}

void STacticsDeckBuilderPanel::RebuildDeckList()
{
	if (!DeckListBox.IsValid()) {
		return;
	}
	DeckListBox->ClearChildren();
	if (EditZone == ETacticsDeckEditZone::Zones) {
		for (const FTacticsDeckZoneEntry& Z : WorkingDeck.Zones) {
			const FString ZoneId = Z.ZoneId;
			DeckListBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
				[SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.f)
						[SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(
									TEXT("%s x%d (%s +%d)"), *Z.Name, Z.Copies, *Z.EnergyKey, Z.EnergyAmount)))
								.ColorAndOpacity(kDeckBuilderText)]
					+ SHorizontalBox::Slot().AutoWidth()
						[SNew(SButton).TextStyle(&TacticsCardText::DefaultTextStyle(11, kDeckBuilderText))
								.Text(FText::FromString(TEXT("-")))
								.OnClicked_Lambda([this, ZoneId]() {
									RemoveOneZone(ZoneId);
									return FReply::Handled();
								})]];
		}
		auto ZAdd = [this](const FString& Key, const FString& Name) {
			DeckListBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
				[SNew(SButton).TextStyle(&TacticsCardText::DefaultTextStyle(11, kDeckBuilderText))
						.Text(FText::FromString(FString::Printf(TEXT("+ %s"), *Name)))
						.IsEnabled_Lambda([this]() { return CountZoneCopies() < kMaxZones; })
						.OnClicked_Lambda([this, Key, Name]() {
							AddDefaultZone(Key, Name);
							return FReply::Handled();
						})];
		};
		for (const tactics::EnergyType Et : tactics::kEnergyBillingAllTypes) {
			const std::string Key = tactics::to_string(Et);
			FString UeKey = UTF8_TO_TCHAR(Key.c_str());
			FString Label = UeKey;
			if (!Label.IsEmpty()) {
				Label[0] = FChar::ToUpper(Label[0]);
			}
			ZAdd(UeKey, FString::Printf(TEXT("%s Territory"), *Label));
		}
		return;
	}
	const TArray<FTacticsDeckCardEntry>& Entries =
		EditZone == ETacticsDeckEditZone::Main ? WorkingDeck.MainDeck : WorkingDeck.Reserves;
	UTacticsDeckLibrarySubsystem* Lib =
		GameInstance.IsValid() ? GameInstance->GetSubsystem<UTacticsDeckLibrarySubsystem>() : nullptr;
	for (const FTacticsDeckCardEntry& E : Entries) {
		const FString CardKey = E.CardKey;
		FString Name = CardKey;
		if (Lib) {
			Lib->TryGetCardDisplayName(CardKey, Name);
		}
		DeckListBox->AddSlot().AutoHeight().Padding(0.f, 2.f)
			[SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
					[SNew(STextBlock)
							.Text(FText::FromString(FString::Printf(TEXT("%s x%d"), *Name, E.Copies)))
							.ColorAndOpacity(kDeckBuilderText)]
				+ SHorizontalBox::Slot().AutoWidth()
					[SNew(SButton).TextStyle(&TacticsCardText::DefaultTextStyle(11, kDeckBuilderText))
							.Text(FText::FromString(TEXT("-")))
							.OnClicked_Lambda([this, CardKey]() {
								RemoveCard(CardKey);
								return FReply::Handled();
							})]];
	}
}

void STacticsDeckBuilderPanel::RefreshStatus()
{
	if (UTacticsDeckLibrarySubsystem* Lib =
			GameInstance.IsValid() ? GameInstance->GetSubsystem<UTacticsDeckLibrarySubsystem>() : nullptr) {
		FString Err;
		StatusMessage = Lib->ValidateDeck(WorkingDeck, Err) ? TEXT("Deck is valid - ready to save.") : Err;
	}
}
