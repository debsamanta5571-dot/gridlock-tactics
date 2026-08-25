#include "STacticsMainMenuPanel.h"

#include "TacticsCardArtUi.h"
#include "TacticsCardText.h"
#include "TacticsDeckLibrarySubsystem.h"
#include "TacticsGameInstance.h"
#include "TacticsWebSocketSubsystem.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Math/UnrealMathUtility.h"
#include "Misc/Attribute.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
const FLinearColor kOrange(0.36f, 0.11f, 0.07f, 1.f);
const FLinearColor kCyan(0.52f, 0.44f, 0.28f, 1.f);
const FLinearColor kMuted(0.40f, 0.38f, 0.36f, 1.f);
const FLinearColor kText(0.78f, 0.76f, 0.72f, 1.f);
const FLinearColor kPanel(0.028f, 0.026f, 0.024f, 0.96f);
const FLinearColor kLine(0.16f, 0.08f, 0.06f, 0.70f);
const FLinearColor kBtnIdle(0.08f, 0.075f, 0.07f, 0.96f);
const FLinearColor kBtnPrimary(0.26f, 0.07f, 0.05f, 1.f);
constexpr float kMenuCardRadius = 18.f;
constexpr float kMenuBtnRadius = 10.f;

const FSlateBrush* MenuCardBrush()
{
	static const FSlateRoundedBoxBrush Brush(kPanel, kMenuCardRadius, kLine, 1.5f);
	return &Brush;
}

const FButtonStyle& MenuPlaqueStyle()
{
	static const FSlateRoundedBoxBrush Normal(FLinearColor::White, kMenuBtnRadius);
	static const FSlateRoundedBoxBrush Hovered(FLinearColor::White, kMenuBtnRadius);
	static const FSlateRoundedBoxBrush Pressed(FLinearColor::White, kMenuBtnRadius);
	static const FButtonStyle Style = FButtonStyle()
		.SetNormal(Normal)
		.SetHovered(Hovered)
		.SetPressed(Pressed)
		.SetDisabled(Normal)
		.SetNormalPadding(FMargin(0.f))
		.SetPressedPadding(FMargin(0.f, 1.f, 0.f, 0.f));
	return Style;
}

/** Short display name for a preset deck key on the start screen chips. */
FString ThemeChipLabel(const FString& Key)
{
	if (Key == TEXT("militia_starforged")) {
		return TEXT("Starforged");
	}
	if (Key == TEXT("militia_shock_and_awe")) {
		return TEXT("Shock and Awe");
	}
	if (Key == TEXT("militia_cyberunners")) {
		return TEXT("Cyberunners");
	}
	if (Key == TEXT("dieselheart_endless_assault")) {
		return TEXT("Endless Assault");
	}
	if (Key == TEXT("dieselheart_covering_fire")) {
		return TEXT("Covering Fire");
	}
	if (Key == TEXT("dieselheart_horrors_of_war")) {
		return TEXT("Horrors of War");
	}
	return Key;
}
}

void STacticsMainMenuPanel::Construct(const FArguments& InArgs)
{
	GameInstance = InArgs._GameInstance;
	if (GameInstance.IsValid()) {
		if (UTacticsDeckLibrarySubsystem* Lib = GameInstance->GetSubsystem<UTacticsDeckLibrarySubsystem>()) {
			const FString Active = Lib->GetActiveDeckKey();
			if (PresetDeckKeys().Contains(Active)) {
				SelectedDeckKey = Active;
			}
		}
	}
	RebuildDeckOptions();
	if (GameInstance.IsValid()) {
		if (UTacticsWebSocketSubsystem* Net = GameInstance->GetSubsystem<UTacticsWebSocketSubsystem>()) {
			Net->OnLobbyChanged.AddSP(this, &STacticsMainMenuPanel::RefreshUi);
		}
	}

	const FSlateBrush* Splash = TacticsCardArtUi::GetCardArtBrush(TEXT("ui/menu_splash"), FVector2D(1920.f, 1080.f));

	ChildSlot
		[SNew(SOverlay)
			+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SImage)
						.Image(Splash)
						.Visibility(Splash ? EVisibility::HitTestInvisible : EVisibility::Collapsed)
				]
			+ SOverlay::Slot()
				.HAlign(HAlign_Fill)
				.VAlign(VAlign_Fill)
				[
					SNew(SBorder)
						.BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
						.BorderBackgroundColor(FLinearColor(0.012f, 0.012f, 0.012f, Splash ? 0.72f : 0.94f))
						.Visibility(EVisibility::HitTestInvisible)
				]
			+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(24.f)
				[
					SNew(SBox)
						.WidthOverride(640.f)
						[
							SNew(SBorder)
								.BorderImage(MenuCardBrush())
								.Padding(FMargin(36.f, 32.f, 36.f, 28.f))
								[
									SNew(SBox)
										.Visibility_Lambda([this]() {
											return (IsInMultiplayerLobby() || IsShowingVsAiSetup())
												? EVisibility::Collapsed
												: EVisibility::Visible;
										})
										[BuildTitleCard()]
								]
						]
				]
			+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(24.f)
				[
					SNew(SBox)
						.WidthOverride(640.f)
						.Visibility_Lambda([this]() {
							return IsShowingVsAiSetup() ? EVisibility::Visible : EVisibility::Collapsed;
						})
						[BuildVsAiCard()]
				]
			+ SOverlay::Slot()
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.Padding(24.f)
				[
					SNew(SBox)
						.WidthOverride(640.f)
						.Visibility_Lambda([this]() {
							return IsInMultiplayerLobby() ? EVisibility::Visible : EVisibility::Collapsed;
						})
						[BuildLobbyCard()]
				]];
	RefreshUi();
}

TSharedRef<SWidget> STacticsMainMenuPanel::MakePlaque(
	const FText& Label, FOnClicked OnClicked, const FString& ControlId, bool bPrimary,
	const FString& Rank, bool bCompact, int32 CompactGroup)
{
	const FString Id = ControlId;
	(void)Rank;
	return SNew(SButton)
		.ButtonStyle(&MenuPlaqueStyle())
		.OnClicked(OnClicked)
		.ButtonColorAndOpacity_Lambda([this, Id, bPrimary, bCompact, CompactGroup]() {
			if (bCompact) {
				const FString& Highlight = CompactGroup == 1 ? SelectedAiDeckKey
					: CompactGroup == 2 ? SelectedDifficulty
					: SelectedDeckKey;
				if (Highlight == Id) {
					return FSlateColor(kBtnPrimary);
				}
			}
			return FSlateColor(bPrimary ? kBtnPrimary : kBtnIdle);
		})
		.ContentPadding(bCompact ? FMargin(8.f, 10.f) : FMargin(16.f, 14.f))
		[
			SNew(STextBlock)
				.Text(Label)
				.Justification(ETextJustify::Center)
				.ColorAndOpacity(FLinearColor::White)
				.Font(TacticsCardText::DesignFont(TEXT("Bold"), bCompact ? 11 : (bPrimary ? 16 : 14)))
		];
}

TSharedRef<SWidget> STacticsMainMenuPanel::BuildTitleCard()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 0.f)
			[SNew(STextBlock)
					.Text(FText::FromString(TEXT("GRIDLOCK")))
					.ColorAndOpacity(kText)
					.Font(TacticsCardText::DesignFont(TEXT("Bold"), 42))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 22.f)
			[SNew(STextBlock)
					.Text(FText::FromString(TEXT("TACTICS")))
					.ColorAndOpacity(kText)
					.Font(TacticsCardText::DesignFont(TEXT("Bold"), 42))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[MakePlaque(FText::FromString(TEXT("Play vs AI")),
				FOnClicked::CreateSP(this, &STacticsMainMenuPanel::OpenVsAiSetup), TEXT("play"), true, TEXT("01"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[MakePlaque(FText::FromString(TEXT("Host LAN")),
				FOnClicked::CreateLambda([this]() {
					bShowHostField = true;
					bShowJoinField = false;
					Invalidate(EInvalidateWidget::LayoutAndVolatility);
					return FReply::Handled();
				}),
				TEXT("host"), false, TEXT("02"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SBox)
					.Visibility_Lambda([this]() { return bShowHostField ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
							[SNew(STextBlock)
									.Text(FText::FromString(TEXT("PORT")))
									.ColorAndOpacity(kMuted)
									.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
							[SNew(SEditableTextBox)
									.Text_Lambda([this]() { return FText::FromString(HostPortText); })
									.OnTextChanged_Lambda([this](const FText& T) { HostPortText = T.ToString(); })
									.HintText(FText::FromString(TEXT("8788")))
									.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 11))]
						+ SVerticalBox::Slot().AutoHeight()
							[MakePlaque(FText::FromString(TEXT("Start hosting")),
								FOnClicked::CreateSP(this, &STacticsMainMenuPanel::StartHost), TEXT("start_host"), true, TEXT("05"))]
					]
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[MakePlaque(FText::FromString(TEXT("Join")),
				FOnClicked::CreateLambda([this]() {
					bShowJoinField = true;
					bShowHostField = false;
					Invalidate(EInvalidateWidget::LayoutAndVolatility);
					return FReply::Handled();
				}),
				TEXT("join"), false, TEXT("03"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(SBox)
					.Visibility_Lambda([this]() { return bShowJoinField ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
							[SNew(STextBlock)
									.Text(FText::FromString(TEXT("PORT")))
									.ColorAndOpacity(kMuted)
									.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
							[SNew(SEditableTextBox)
									.Text_Lambda([this]() { return FText::FromString(JoinPortText); })
									.OnTextChanged_Lambda([this](const FText& T) { JoinPortText = T.ToString(); })
									.HintText(FText::FromString(TEXT("8788")))
									.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 11))]
						+ SVerticalBox::Slot().AutoHeight()
							[SNew(SBox)
								.Visibility_Lambda([this]() {
									return IsJoiningLobby() ? EVisibility::Collapsed : EVisibility::Visible;
								})
								[MakePlaque(FText::FromString(TEXT("Connect")),
									FOnClicked::CreateSP(this, &STacticsMainMenuPanel::StartJoin), TEXT("connect"), false, TEXT("04"))]]
						+ SVerticalBox::Slot().AutoHeight()
							[SNew(SBox)
								.Visibility_Lambda([this]() {
									return IsJoiningLobby() ? EVisibility::Visible : EVisibility::Collapsed;
								})
								[MakePlaque(FText::FromString(TEXT("Cancel")),
									FOnClicked::CreateLambda([this]() {
										if (GameInstance.IsValid()) {
											GameInstance->LeaveMultiplayerLobby();
										}
										StatusMessage.Empty();
										RefreshUi();
										return FReply::Handled();
									}),
									TEXT("cancel_join"), false, TEXT("06"))]]
					]
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[MakePlaque(FText::FromString(TEXT("Options")),
				FOnClicked::CreateSP(this, &STacticsMainMenuPanel::OpenOptions), TEXT("options"), false, TEXT("04"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[MakePlaque(FText::FromString(TEXT("Quit")),
				FOnClicked::CreateSP(this, &STacticsMainMenuPanel::QuitGame), TEXT("quit"), false, TEXT("05"))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
			[SNew(STextBlock)
					.Text_Lambda([this]() { return FText::FromString(StatusMessage); })
					.ColorAndOpacity(kCyan)
					.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 10))
					.AutoWrapText(true)];
}

void STacticsMainMenuPanel::SelectDeckAt(int32 Index)
{
	const TArray<FString> Keys = PresetDeckKeys();
	if (!Keys.IsValidIndex(Index)) {
		return;
	}
	SelectedDeckKey = Keys[Index];
	RebuildDeckOptions();
	RefreshUi();
}

void STacticsMainMenuPanel::SelectAiDeckAt(int32 Index)
{
	const TArray<FString> Keys = PresetDeckKeys();
	if (!Keys.IsValidIndex(Index)) {
		return;
	}
	SelectedAiDeckKey = Keys[Index];
	RefreshUi();
}

void STacticsMainMenuPanel::SelectDifficulty(const FString& Difficulty)
{
	SelectedDifficulty = Difficulty;
	RefreshUi();
}

TSharedRef<SWidget> STacticsMainMenuPanel::BuildDeckChipRow(int32 StartIndex, int32 Count, bool bAiDeck)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	const TArray<FString> Keys = PresetDeckKeys();
	for (int32 i = 0; i < Count; ++i) {
		const int32 Index = StartIndex + i;
		FString Label;
		if (Keys.IsValidIndex(Index)) {
			Label = ThemeChipLabel(Keys[Index]);
		}
		const FString ControlId = Keys.IsValidIndex(Index) ? Keys[Index] : FString();
		Row->AddSlot().FillWidth(1.f).Padding(i == 0 ? 0.f : 6.f, 0.f, 0.f, 0.f)
			[
				MakePlaque(FText::FromString(Label),
					FOnClicked::CreateLambda([this, Index, bAiDeck]() {
						if (bAiDeck) {
							SelectAiDeckAt(Index);
						} else {
							SelectDeckAt(Index);
						}
						return FReply::Handled();
					}),
					ControlId, false, FString(), true, bAiDeck ? 1 : 0)
			];
	}
	return Row;
}

TSharedRef<SWidget> STacticsMainMenuPanel::BuildDeckPicker()
{
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[SNew(STextBlock)
					.Text(FText::FromString(TEXT("MILITIA")))
					.ColorAndOpacity(kOrange)
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[BuildDeckChipRow(0, 3, false)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[SNew(STextBlock)
					.Text(FText::FromString(TEXT("DIESELHEART  ·  EXPERIMENTAL")))
					.ColorAndOpacity(kOrange)
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))]
		+ SVerticalBox::Slot().AutoHeight()[BuildDeckChipRow(3, 3, false)];
}

TSharedRef<SWidget> STacticsMainMenuPanel::BuildAiSetup()
{
	auto DifficultyChip = [this](const FString& Id, const FString& Label) {
		return MakePlaque(FText::FromString(Label),
			FOnClicked::CreateLambda([this, Id]() {
				SelectDifficulty(Id);
				return FReply::Handled();
			}),
			Id, false, FString(), true, 2);
	};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 8.f)
			[SNew(STextBlock)
					.Text(FText::FromString(TEXT("AI DECK")))
					.ColorAndOpacity(kMuted)
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[SNew(STextBlock)
					.Text(FText::FromString(TEXT("MILITIA")))
					.ColorAndOpacity(kOrange)
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)[BuildDeckChipRow(0, 3, true)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
			[SNew(STextBlock)
					.Text(FText::FromString(TEXT("DIESELHEART  ·  EXPERIMENTAL")))
					.ColorAndOpacity(kOrange)
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[BuildDeckChipRow(3, 3, true)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
			[SNew(STextBlock)
					.Text_Lambda([this]() {
						FString Label = ThemeChipLabel(SelectedAiDeckKey);
						if (GameInstance.IsValid()) {
							if (UTacticsDeckLibrarySubsystem* Lib =
									GameInstance->GetSubsystem<UTacticsDeckLibrarySubsystem>()) {
								Label = Lib->GetDeckDisplayName(SelectedAiDeckKey);
							}
						}
						return FText::FromString(FString::Printf(TEXT("AI: %s"), *Label));
					})
					.ColorAndOpacity(kCyan)
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 11))]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 8.f)
			[SNew(STextBlock)
					.Text(FText::FromString(TEXT("DIFFICULTY")))
					.ColorAndOpacity(kMuted)
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))]
		+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
					[DifficultyChip(TEXT("easy"), TEXT("Easy"))]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6.f, 0.f, 0.f, 0.f)
					[DifficultyChip(TEXT("normal"), TEXT("Normal"))]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6.f, 0.f, 0.f, 0.f)
					[DifficultyChip(TEXT("hard"), TEXT("Hard"))]
			];
}

TSharedRef<SWidget> STacticsMainMenuPanel::BuildVsAiCard()
{
	return SNew(SBorder)
		.BorderImage(MenuCardBrush())
		.Padding(FMargin(28.f, 24.f))
		[
			SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
						[SNew(STextBlock)
								.Text(FText::FromString(TEXT("VS AI")))
								.ColorAndOpacity(kOrange)
								.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 10))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
						[SNew(STextBlock)
								.Text(FText::FromString(TEXT("Choose the opponent deck and difficulty.")))
								.ColorAndOpacity(kText)
								.Font(TacticsCardText::DesignFont(TEXT("Bold"), 18))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
						[SNew(STextBlock)
								.Text(FText::FromString(TEXT("YOUR DECK")))
								.ColorAndOpacity(kMuted)
								.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[BuildDeckPicker()]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
						[SNew(STextBlock)
								.Text_Lambda([this]() {
									FString Label = ThemeChipLabel(SelectedDeckKey);
									if (GameInstance.IsValid()) {
										if (UTacticsDeckLibrarySubsystem* Lib =
												GameInstance->GetSubsystem<UTacticsDeckLibrarySubsystem>()) {
											Label = Lib->GetDeckDisplayName(SelectedDeckKey);
										}
									}
									return FText::FromString(FString::Printf(TEXT("You: %s"), *Label));
								})
								.ColorAndOpacity(kCyan)
								.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 11))]
					+ SVerticalBox::Slot().AutoHeight()[BuildAiSetup()]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 16.f, 0.f, 8.f)
						[MakePlaque(FText::FromString(TEXT("Start match")),
							FOnClicked::CreateSP(this, &STacticsMainMenuPanel::StartVsAi), TEXT("start_ai"), true, TEXT("01"))]
					+ SVerticalBox::Slot().AutoHeight()
						[MakePlaque(FText::FromString(TEXT("Back")),
							FOnClicked::CreateSP(this, &STacticsMainMenuPanel::CloseVsAiSetup), TEXT("back_ai"), false, TEXT("02"))]
		];
}

TSharedRef<SWidget> STacticsMainMenuPanel::BuildLobbyCard()
{
	return SNew(SBorder)
		.BorderImage(MenuCardBrush())
		.Padding(FMargin(28.f, 24.f))
		[
			SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
						[SNew(STextBlock)
								.Text(FText::FromString(TEXT("LOBBY")))
								.ColorAndOpacity(kOrange)
								.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 10))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
						[SNew(STextBlock)
								.Text_Lambda([this]() {
									if (!GameInstance.IsValid()) {
										return FText::GetEmpty();
									}
									const UTacticsWebSocketSubsystem* Net = GameInstance->GetSubsystem<UTacticsWebSocketSubsystem>();
									if (!Net) {
										return FText::GetEmpty();
									}
									return FText::FromString(FString::Printf(
										TEXT("%s · port %d"),
										Net->IsHosting() ? TEXT("Hosting LAN") : TEXT("Joined"),
										Net->IsHosting() ? Net->GetListenPort() : FCString::Atoi(*JoinPortText)));
								})
								.ColorAndOpacity(kText)
								.Font(TacticsCardText::DesignFont(TEXT("Bold"), 18))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 16.f)
						[SNew(STextBlock)
								.Text_Lambda([this]() {
									if (!GameInstance.IsValid()) {
										return FText::GetEmpty();
									}
									const UTacticsWebSocketSubsystem* Net = GameInstance->GetSubsystem<UTacticsWebSocketSubsystem>();
									if (!Net) {
										return FText::GetEmpty();
									}
									FString Lines;
									UTacticsDeckLibrarySubsystem* Lib = GameInstance->GetSubsystem<UTacticsDeckLibrarySubsystem>();
									for (const FTacticsLobbySeatState& Seat : Net->GetLobbySeats()) {
										if (Seat.SeatId > 2) {
											continue;
										}
										if (!Seat.bOccupied) {
											Lines += FString::Printf(TEXT("P%d  -  open\n"), Seat.SeatId);
										} else {
											const FString DeckLabel = Seat.DeckName.IsEmpty()
												? FString(TEXT("Deck"))
												: (Lib ? Lib->GetDeckDisplayName(Seat.DeckName) : Seat.DeckName);
											Lines += FString::Printf(TEXT("P%d  %s  ·  %s\n"), Seat.SeatId,
												Seat.bIsHost ? TEXT("Host") : *Seat.DisplayName,
												*DeckLabel);
										}
									}
									return FText::FromString(Lines.TrimEnd());
								})
								.ColorAndOpacity(kText)
								.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 12))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 10.f)
						[SNew(STextBlock)
								.Text(FText::FromString(TEXT("YOUR DECK")))
								.ColorAndOpacity(kMuted)
								.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
								.Visibility_Lambda([this]() {
									return IsInMultiplayerLobby() ? EVisibility::Visible : EVisibility::Collapsed;
								})]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 14.f)
						[SNew(SBox)
							.Visibility_Lambda([this]() {
								return IsInMultiplayerLobby() ? EVisibility::Visible : EVisibility::Collapsed;
							})
							[BuildDeckPicker()]]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
						[
							SNew(SBox)
								.Visibility_Lambda([this]() {
									if (!GameInstance.IsValid()) {
										return EVisibility::Collapsed;
									}
									const UTacticsWebSocketSubsystem* Net = GameInstance->GetSubsystem<UTacticsWebSocketSubsystem>();
									return (Net && Net->IsHosting() && Net->IsInLobby()) ? EVisibility::Visible : EVisibility::Collapsed;
								})
								[MakePlaque(FText::FromString(TEXT("Start match")),
									FOnClicked::CreateLambda([this]() {
										if (!GameInstance.IsValid()) {
											return FReply::Handled();
										}
										UTacticsWebSocketSubsystem* Net = GameInstance->GetSubsystem<UTacticsWebSocketSubsystem>();
										if (!Net) {
											return FReply::Handled();
										}
										ApplySelectedDeckToLobby();
										FString Err;
										if (!Net->HostStartMatchFromLobby(Err)) {
											StatusMessage = Err;
											return FReply::Handled();
										}
										GameInstance->EnterMatchFromLobbyAsHost();
										return FReply::Handled();
									}),
									TEXT("start"), true, TEXT("02"))]
						]
					+ SVerticalBox::Slot().AutoHeight()
						[MakePlaque(FText::FromString(TEXT("Leave")),
							FOnClicked::CreateLambda([this]() {
								if (GameInstance.IsValid()) {
									GameInstance->LeaveMultiplayerLobby();
									RefreshUi();
								}
								return FReply::Handled();
							}),
							TEXT("leave"), false, TEXT("03"))]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
						[SNew(STextBlock)
								.Text_Lambda([this]() { return FText::FromString(StatusMessage); })
								.ColorAndOpacity(kCyan)
								.AutoWrapText(true)
								.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 10))]
		];
}

TArray<FString> STacticsMainMenuPanel::PresetDeckKeys() const
{
	TArray<FString> Out;
	Out.Add(TEXT("militia_starforged"));
	Out.Add(TEXT("militia_shock_and_awe"));
	Out.Add(TEXT("militia_cyberunners"));
	Out.Add(TEXT("dieselheart_endless_assault"));
	Out.Add(TEXT("dieselheart_covering_fire"));
	Out.Add(TEXT("dieselheart_horrors_of_war"));
	return Out;
}

void STacticsMainMenuPanel::RebuildDeckOptions()
{
	const TArray<FString> Keys = PresetDeckKeys();
	if (Keys.Num() == 0) {
		return;
	}
	if (!Keys.Contains(SelectedDeckKey)) {
		SelectedDeckKey = Keys[0];
	}
	if (!GameInstance.IsValid()) {
		return;
	}
	if (UTacticsDeckLibrarySubsystem* Lib = GameInstance->GetSubsystem<UTacticsDeckLibrarySubsystem>()) {
		Lib->SetActiveDeckKey(SelectedDeckKey);
	}
}

void STacticsMainMenuPanel::PushMatchSettings()
{
	if (!GameInstance.IsValid()) {
		return;
	}
	UTacticsGameInstance::FTacticsMatchSettings S;
	S.bTeam2v2 = false;
	S.bObjScanner = true;
	S.bObjOmni = true;
	S.bObjAether = true;
	S.bGiveFieldRequisition = false;
	S.bBindPublic = true;
	S.HostPort = 8788;
	{
		FString PortTrim = HostPortText;
		PortTrim.TrimStartAndEndInline();
		const int32 Parsed = FCString::Atoi(*PortTrim);
		if (Parsed >= 1 && Parsed <= 65535) {
			S.HostPort = Parsed;
		}
	}
	S.OpponentDeckId = SelectedAiDeckKey;
	S.BotDifficulty = SelectedDifficulty;
	GameInstance->SetPendingMatchSettings(S);
}

FReply STacticsMainMenuPanel::OpenVsAiSetup()
{
	bShowVsAiSetup = true;
	bShowJoinField = false;
	bShowHostField = false;
	Invalidate(EInvalidateWidget::LayoutAndVolatility);
	return FReply::Handled();
}

FReply STacticsMainMenuPanel::CloseVsAiSetup()
{
	bShowVsAiSetup = false;
	Invalidate(EInvalidateWidget::LayoutAndVolatility);
	return FReply::Handled();
}

FReply STacticsMainMenuPanel::StartVsAi()
{
	if (GameInstance.IsValid()) {
		RebuildDeckOptions();
		PushMatchSettings();
		GameInstance->StartLocalMatchSolo();
	}
	return FReply::Handled();
}

FReply STacticsMainMenuPanel::StartHost()
{
	FString PortTrim = HostPortText;
	PortTrim.TrimStartAndEndInline();
	const int32 Parsed = FCString::Atoi(*PortTrim);
	if (PortTrim.IsEmpty() || Parsed < 1 || Parsed > 65535) {
		StatusMessage = TEXT("Enter a host port between 1 and 65535.");
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
		return FReply::Handled();
	}
	HostPortText = FString::FromInt(Parsed);
	if (GameInstance.IsValid()) {
		RebuildDeckOptions();
		PushMatchSettings();
		GameInstance->StartHostLobby();
		RefreshUi();
	}
	return FReply::Handled();
}

FReply STacticsMainMenuPanel::StartJoin()
{
	if (IsJoiningLobby()) {
		return FReply::Handled();
	}
	FString PortTrim = JoinPortText;
	PortTrim.TrimStartAndEndInline();
	const int32 Parsed = FCString::Atoi(*PortTrim);
	if (PortTrim.IsEmpty() || Parsed < 1 || Parsed > 65535) {
		StatusMessage = TEXT("Enter a join port between 1 and 65535.");
		Invalidate(EInvalidateWidget::LayoutAndVolatility);
		return FReply::Handled();
	}
	JoinPortText = FString::FromInt(Parsed);
	if (GameInstance.IsValid()) {
		PushMatchSettings();
		StatusMessage = TEXT("Connecting...");
		GameInstance->StartJoinLobby(FString::Printf(TEXT("ws://127.0.0.1:%d/"), Parsed));
		RefreshUi();
	}
	return FReply::Handled();
}

void STacticsMainMenuPanel::ApplySelectedDeckToLobby()
{
	if (!GameInstance.IsValid()) {
		return;
	}
	UTacticsDeckLibrarySubsystem* Lib = GameInstance->GetSubsystem<UTacticsDeckLibrarySubsystem>();
	UTacticsWebSocketSubsystem* Net = GameInstance->GetSubsystem<UTacticsWebSocketSubsystem>();
	if (!Lib || !Net || !Net->IsInLobby()) {
		return;
	}
	RebuildDeckOptions();
	Lib->SetActiveDeckKey(SelectedDeckKey);
	FString Json, Err;
	if (!Lib->ExportDeckJsonByKey(SelectedDeckKey, Json, Err)) {
		StatusMessage = Err;
		return;
	}
	Net->LobbySetDeck(SelectedDeckKey, Json);
	RefreshUi();
}

FReply STacticsMainMenuPanel::OpenOptions()
{
	if (GameInstance.IsValid())
	{
		GameInstance->ToggleOptionsOverlay();
	}
	return FReply::Handled();
}

FReply STacticsMainMenuPanel::QuitGame()
{
	if (GameInstance.IsValid())
	{
		GameInstance->RequestQuitGame();
	}
	return FReply::Handled();
}

void STacticsMainMenuPanel::RefreshUi()
{
	if (!GameInstance.IsValid()) {
		StatusMessage.Empty();
		return;
	}
	RebuildDeckOptions();
	UTacticsDeckLibrarySubsystem* Lib = GameInstance->GetSubsystem<UTacticsDeckLibrarySubsystem>();
	if (const UTacticsWebSocketSubsystem* Net = GameInstance->GetSubsystem<UTacticsWebSocketSubsystem>()) {
		if (IsJoiningLobby()) {
			StatusMessage = Net->GetLobbyStatusMessage();
			if (StatusMessage.IsEmpty()) {
				StatusMessage = TEXT("Connecting…");
			}
			Invalidate(EInvalidateWidget::LayoutAndVolatility);
			return;
		}
		if (Net->IsInLobby()) {
			StatusMessage = Net->GetLobbyStatusMessage();
			const FString JoinErr = Net->GetClientConnectionError();
			if (!JoinErr.IsEmpty()) {
				StatusMessage = JoinErr;
			}
			Invalidate(EInvalidateWidget::LayoutAndVolatility);
			return;
		}
		const FString JoinErr = Net->GetClientConnectionError();
		if (!JoinErr.IsEmpty()) {
			StatusMessage = JoinErr;
			Invalidate(EInvalidateWidget::LayoutAndVolatility);
			return;
		}
	}
	if (Lib) {
		Lib->SetActiveDeckKey(SelectedDeckKey);
		FString Err;
		if (Lib->ApplyActiveDeckForMatch(Err)) {
			StatusMessage.Empty();
		} else {
			StatusMessage = Err;
		}
	}
	Invalidate(EInvalidateWidget::LayoutAndVolatility);
	Invalidate(EInvalidateWidget::Paint);
}

bool STacticsMainMenuPanel::IsInMultiplayerLobby() const
{
	if (!GameInstance.IsValid()) {
		return false;
	}
	const UTacticsWebSocketSubsystem* Net = GameInstance->GetSubsystem<UTacticsWebSocketSubsystem>();
	if (!Net || !Net->IsInLobby()) {
		return false;
	}
	if (Net->IsHosting()) {
		return true;
	}
	if (Net->IsClientConnecting()) {
		return false;
	}
	return Net->IsClientConnectedToHost();
}

bool STacticsMainMenuPanel::IsJoiningLobby() const
{
	if (!GameInstance.IsValid()) {
		return false;
	}
	const UTacticsWebSocketSubsystem* Net = GameInstance->GetSubsystem<UTacticsWebSocketSubsystem>();
	return Net && Net->IsClientConnecting();
}
