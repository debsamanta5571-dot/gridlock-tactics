#include "STacticsBoardPanel.h"
#include "Framework/Application/SlateApplication.h"

#include "Engine/GameInstance.h"
#include "Tactics3DBoardGameMode.h"
#include "Tactics3DWorldBoardActor.h"
#include "TacticsGameInstance.h"
#include "TacticsCardArtUi.h"
#include "TacticsMatchSubsystem.h"
#include "tactics/apps/sandbox_match.hpp"
#include "TacticsWebSocketSubsystem.h"
#include "TacticsCardText.h"
#include "TacticsGlossaryMarkup.h"
#include "TacticsAbilityVisualFeedback.h"
#include "TacticsAbilityVisualGroup.h"
#include "TacticsAbilityResolveFlash.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Styling/AppStyle.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Misc/ConfigCacheIni.h"
#include "InputCoreTypes.h"
#include "Widgets/Layout/SWrapBox.h"

#include "STacticsBoardPanel_Internal.h"


void STacticsBoardPanel::RebuildSpellStack()
{
	if (!SpellStackScroll.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	SpellStackScroll->ClearChildren();
	TArray<FTacticsStackItemUi> Items;
	Subsystem->GetSpellStackUiItems(Items);
	if (Items.IsEmpty()) {
		const FLinearColor StackLineColor = Subsystem->Uses3DBoardTiles() ? kCardTextWhite : kUiBlack;
		SpellStackScroll->AddSlot().Padding(0.f, 3.f)
			[SNew(STextBlock)
					.WrapTextAt(236.f)
					.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
					.ColorAndOpacity(StackLineColor)
					.Text(FText::FromString(TEXT("(stack empty)")))];
		return;
	}
	for (const FTacticsStackItemUi& Item : Items) {
		SpellStackScroll->AddSlot().Padding(0.f, 4.f)[BuildStackCard(Item)];
	}
}

void STacticsBoardPanel::RebuildActionQueue()
{
	if (!ActionQueueScroll.IsValid() || !Subsystem.IsValid()) return;
	ActionQueueScroll->ClearChildren();

	TArray<FTacticsActionQueueEntryUi> Entries;
	Subsystem->GetActionQueueUiEntries(Entries);

	const FLinearColor TextColor = Subsystem->Uses3DBoardTiles() ? kCardTextWhite : kUiBlack;

	if (Entries.IsEmpty()) {
		ActionQueueScroll->AddSlot().Padding(0.f, 3.f)
			[SNew(STextBlock)
				.WrapTextAt(230.f)
				.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
				.ColorAndOpacity(TextColor)
				.Text(FText::FromString(TEXT("(nothing queued)")))];
		return;
	}

	const int32 PinnedQueueIndex = Subsystem.IsValid() ? Subsystem->GetPinnedActionQueueIndex() : -1;

	for (const FTacticsActionQueueEntryUi& Entry : Entries) {
		FLinearColor KindColor = TextColor;
		if (Entry.Kind == TEXT("attack")) KindColor = FLinearColor(1.f, 0.55f, 0.1f, 1.f);
		else if (Entry.Kind == TEXT("reaction")) KindColor = FLinearColor(0.4f, 0.8f, 1.f, 1.f);

		const FString OwnerStr = Entry.ControllerPlayerId > 0
			? FString::Printf(TEXT(" (P%d)"), Entry.ControllerPlayerId)
			: FString();
		const FString MainLine = Entry.Label + OwnerStr;

		const bool bCanCounterStackSpell = IsArmedStackTargetEffect()
			&& !Entry.ItemId.IsEmpty()
			&& (Entry.ItemId.StartsWith(TEXT("batch_")) || Entry.ItemId.StartsWith(TEXT("stack_")))
			&& CanArmedStackTargetSourceType(Entry.SourceType);
		const bool bCanCounterStackAbility = bAbilityArmedForTile && IsArmedStackTargetEffect()
			&& !Entry.ItemId.IsEmpty()
			&& (Entry.ItemId.StartsWith(TEXT("batch_")) || Entry.ItemId.StartsWith(TEXT("stack_")))
			&& CanArmedStackTargetSourceType(Entry.SourceType);
		const bool bCanCounter = bCanCounterStackSpell || bCanCounterStackAbility;
		const bool bCanCancelOwn = Subsystem.IsValid()
			&& !Entry.ItemId.IsEmpty()
			&& Subsystem->CanControlledPlayerCancelBatchItem(Entry.ItemId, Entry.ControllerPlayerId);
		if (bCanCounter) {
			KindColor = bCanCounterStackAbility ? FLinearColor(0.78f, 0.22f, 0.62f, 1.f) : FLinearColor(0.95f, 0.75f, 0.15f, 1.f);
		} else if (bCanCancelOwn) {
			KindColor = FLinearColor(0.75f, 0.9f, 0.55f, 1.f);
		}

		auto MakeEntryBody = [MainLine, Entry, KindColor, TextColor]() {
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
					[SNew(STextBlock)
						.WrapTextAt(230.f)
						.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
						.ColorAndOpacity(KindColor)
						.Text(FText::FromString(MainLine))]
				+ SVerticalBox::Slot().AutoHeight()
					[SNew(STextBlock)
						.WrapTextAt(230.f)
						.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 8))
						.ColorAndOpacity(TextColor)
						.Text(FText::FromString(Entry.Detail))];
		};

		const int32 QueueIndex = Entry.QueueIndex;
		const bool bPinnedRow = PinnedQueueIndex == QueueIndex;
		const auto OnQueueRowHovered = [this, QueueIndex]() {
			if (Subsystem.IsValid() && QueueIndex >= 0 && !Subsystem->IsActionQueuePreviewPinned()) {
				Subsystem->SetActionQueueHoverIndex(QueueIndex);
				RefreshActionQueueHoverOverlay();
			}
		};
		const auto OnQueueRowUnhovered = [this]() {
			if (Subsystem.IsValid() && !Subsystem->IsActionQueuePreviewPinned()) {
				Subsystem->ClearActionQueueHoverPreview();
				RefreshActionQueueHoverOverlay();
			}
		};
		const auto OnQueueRowPinToggle = [this, QueueIndex]() {
			if (Subsystem.IsValid() && QueueIndex >= 0) {
				Subsystem->ToggleActionQueuePinIndex(QueueIndex);
				RefreshActionQueueHoverOverlay();
			}
		};
		const auto WrapQueueRowRightClickPin = [OnQueueRowPinToggle](TSharedRef<SWidget> RowWidget) -> TSharedRef<SWidget> {
			return SNew(SBorder)
				.BorderImage(FCoreStyle::Get().GetBrush("NoBorder"))
				.Padding(0.f)
				.OnMouseButtonDown_Lambda([OnQueueRowPinToggle](const FGeometry&, const FPointerEvent& MouseEvent) {
					if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton) {
						OnQueueRowPinToggle();
						return FReply::Handled();
					}
					return FReply::Unhandled();
				})
				[
					RowWidget
				];
		};
		const FLinearColor RowButtonColor = bPinnedRow
			? FLinearColor(0.18f, 0.22f, 0.34f, 1.f)
			: FLinearColor(0.12f, 0.12f, 0.14f, 1.f);

		if (bCanCounter) {
			ActionQueueScroll->AddSlot().Padding(0.f, 3.f)
				[
					WrapQueueRowRightClickPin(
					SNew(SButton)
						.ButtonColorAndOpacity(RowButtonColor)
						.ContentPadding(FMargin(4.f, 2.f))
						.OnHovered_Lambda(OnQueueRowHovered)
						.OnUnhovered_Lambda(OnQueueRowUnhovered)
						.OnClicked_Lambda([this, ItemId = Entry.ItemId]() {
							if (bHandArmedForTile) {
								const int32 Idx = DeployHandIndex1Based;
								ClearPlayArmingState();
								RunCli(FString::Printf(TEXT("cast %d stack %s"), Idx, *ItemId));
							} else if (bReservesArmedForTile) {
								const int32 Idx = DeployReservesIndex1Based;
								ClearPlayArmingState();
								RunCli(FString::Printf(TEXT("cast_reserve %d stack %s"), Idx, *ItemId));
							} else if (bAbilityArmedForTile) {
								const FString Line = BuildArmedAbilityStackCliLine(ItemId);
								const FString KeyCopy = ArmedAbilityKey;
								RunAbilityCli(Line, KeyCopy, {});
								ClearPlayArmingState();
							}
							return FReply::Handled();
						})
						[MakeEntryBody()])
				];
		} else if (bCanCancelOwn) {
			ActionQueueScroll->AddSlot().Padding(0.f, 3.f)
				[
					WrapQueueRowRightClickPin(
					SNew(SButton)
						.ButtonColorAndOpacity(RowButtonColor)
						.ContentPadding(FMargin(4.f, 2.f))
						.OnHovered_Lambda(OnQueueRowHovered)
						.OnUnhovered_Lambda(OnQueueRowUnhovered)
						.OnClicked_Lambda([this, ItemId = Entry.ItemId]() {
							RunCli(FString::Printf(TEXT("batch_cancel %s"), *ItemId));
							LastCliOutput = TEXT("Queued action cancelled - card and energy refunded. Re-arm to retarget.");
							RefreshStatusText();
							Refresh();
							return FReply::Handled();
						})
						[MakeEntryBody()])
				];
		} else {
			const FLinearColor IdleRowColor = bPinnedRow
				? FLinearColor(0.14f, 0.18f, 0.28f, 0.55f)
				: FLinearColor::Transparent;
			ActionQueueScroll->AddSlot().Padding(0.f, 3.f)
				[
					SNew(SButton)
						.ButtonStyle(&BoardCellButtonStyle())
						.ButtonColorAndOpacity(IdleRowColor)
						.ContentPadding(FMargin(3.f, 2.f))
						.OnHovered_Lambda(OnQueueRowHovered)
						.OnUnhovered_Lambda(OnQueueRowUnhovered)
						.OnClicked_Lambda([OnQueueRowPinToggle]() {
							OnQueueRowPinToggle();
							return FReply::Handled();
						})
						[MakeEntryBody()]
				];
		}
	}
	RefreshActionQueueHoverOverlay();
}

TSharedRef<SWidget> BuildAttackCompareColumn(const FString& Title, const FString& StatsMarkup, const FString& SubLabel)
{
	const FLinearColor TitleColor(0.82f, 0.86f, 0.92f, 1.f);
	const FLinearColor SubColor(0.68f, 0.72f, 0.78f, 1.f);
	TSharedRef<SVerticalBox> Col = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(Title))
					.Font(TacticsCardText::DesignFont(TEXT("Bold"), kQueueHoverAttackCompareFontSize))
					.ColorAndOpacity(TitleColor)
					.Justification(ETextJustify::Center)
			]
		+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SRichTextBlock)
					.WrapTextAt(kQueueHoverAttackCompareColW)
					.Justification(ETextJustify::Center)
					.LineHeightPercentage(1.2f)
					.TextStyle(&TacticsCardText::EnergyTextStyle(kQueueHoverAttackCompareFontSize, kCardTextWhite))
					.DecoratorStyleSet(&FCoreStyle::Get())
					.Decorators(TacticsCardText::MakeEnergyDecorators(kQueueHoverAttackCompareFontSize,
						TacticsCardText::ESpeedTooltipSubject::Attack))
					.Text(FText::FromString(TacticsCardText::MarkupEnergyTokens(StatsMarkup)))
			];
	if (!SubLabel.IsEmpty()) {
		Col->AddSlot().AutoHeight().Padding(0.f, 3.f, 0.f, 0.f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(SubLabel))
					.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
					.ColorAndOpacity(SubColor)
					.Justification(ETextJustify::Center)
			];
	}
	return SNew(SBox)
		.WidthOverride(kQueueHoverAttackCompareColW)
		[
			Col
		];
}

TSharedRef<SWidget> STacticsBoardPanel::BuildActionQueueHoverPreviewCard(const FTacticsActionQueueHoverCardUi& Card) const
{
	FString Label = Card.Label;
	if (Label.Len() > 20) {
		Label = Label.Left(19) + TEXT("…");
	}
	const FVector2D ArtSize(kQueueHoverPreviewCardW, kQueueHoverPreviewCardH);
	const FSlateBrush* Brush = Card.ArtId.IsEmpty() ? nullptr : TacticsCardArtUi::GetCardArtBrush(Card.ArtId, ArtSize);
	const FLinearColor Frame = Card.TypeLine.Equals(TEXT("UNIT"), ESearchCase::IgnoreCase)
		? FLinearColor(0.48f, 0.32f, 0.14f, 1.f)
		: Card.TypeLine.Equals(TEXT("ABILITY"), ESearchCase::IgnoreCase)
			? FLinearColor(0.42f, 0.18f, 0.52f, 1.f)
			: FLinearColor(0.12f, 0.28f, 0.52f, 1.f);

	TSharedRef<SWidget> ArtBody = Brush
		? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Brush))
		: StaticCastSharedRef<SWidget>(
			  SNew(SBox)
				  .HAlign(HAlign_Center)
				  .VAlign(VAlign_Center)
				  [
					  SNew(STextBlock)
						  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 10))
						  .ColorAndOpacity(kCardTextWhite)
						  .Justification(ETextJustify::Center)
						  .Text(FText::FromString(Label))
				  ]);

	return SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(STextBlock)
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 8))
					.ColorAndOpacity(kCardTextWhite)
					.Justification(ETextJustify::Center)
					.Text(FText::FromString(Card.TypeLine))
			]
		+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).WidthOverride(kQueueHoverPreviewCardW).HeightOverride(kQueueHoverPreviewCardH)
					[
						SNew(SBorder).BorderBackgroundColor(Frame).Padding(FMargin(2.f))
							[
								SNew(SBorder).BorderBackgroundColor(FLinearColor(0.04f, 0.04f, 0.06f, 1.f))
									[
										ArtBody
									]
							]
					]
			]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 4.f, 0.f, 0.f)
			[
				SNew(STextBlock)
					.WrapTextAt(kQueueHoverPreviewCardW)
					.Font(TacticsCardText::DesignFont(TEXT("Bold"), 10))
					.ColorAndOpacity(kCardTextWhite)
					.Justification(ETextJustify::Center)
					.Text(FText::FromString(Label))
			];
}

void STacticsBoardPanel::RefreshActionQueueHoverOverlay()
{
	if (!ActionQueueHoverOverlay.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	FTacticsActionQueueHoverOverlayUi Preview;
	FString Banner;
	bool bHasPreview = false;
	if (Subsystem->TryGetActionQueueHoverOverlay(Preview) && Preview.bShowPreview) {
		bHasPreview = true;
	} else if (Subsystem->TryGetActiveOpponentPlayPresentation(Preview, Banner) && Preview.bShowPreview) {
		bHasPreview = true;
	}
	if (!bHasPreview) {
		ActionQueueHoverOverlay->SetContent(SNullWidget::NullWidget);
		ActionQueueHoverOverlay->SetVisibility(EVisibility::Collapsed);
		return;
	}

	const bool bPinned = Subsystem->IsActionQueuePreviewPinned();

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	if (!Banner.IsEmpty()) {
		Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(STextBlock)
					.Font(TacticsCardText::DesignFont(TEXT("Bold"), 14))
					.ColorAndOpacity(FLinearColor(0.95f, 0.86f, 0.55f, 1.f))
					.Justification(ETextJustify::Center)
					.Text(FText::FromString(Banner))
			];
	}
	if (bPinned) {
		Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
			[
				SNew(STextBlock)
					.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 10))
					.ColorAndOpacity(FLinearColor(0.72f, 0.78f, 0.9f, 1.f))
					.Justification(ETextJustify::Center)
					.Text(FText::FromString(TEXT("Pinned - click the queue row again to dismiss")))
			];
	}

	TSharedRef<SHorizontalBox> CardRow = SNew(SHorizontalBox);
	auto AddHoverArrow = [&CardRow](const float LeftPad) {
		CardRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(LeftPad, 0.f)
			[
				SNew(STextBlock)
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 36))
					.ColorAndOpacity(FLinearColor(0.95f, 0.82f, 0.2f, 1.f))
					.Text(FText::FromString(TEXT("\u2192")))
			];
	};
	auto AddHoverPlus = [&CardRow](const float LeftPad) {
		CardRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(LeftPad, 0.f)
			[
				SNew(STextBlock)
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 28))
					.ColorAndOpacity(FLinearColor(0.78f, 0.82f, 0.9f, 1.f))
					.Text(FText::FromString(TEXT("+")))
			];
	};
	auto AddHoverCard = [&CardRow, this](const FTacticsActionQueueHoverCardUi& Card, const float LeftPad) {
		CardRow->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(LeftPad, 0.f)
			[
				BuildActionQueueHoverPreviewCard(Card)
			];
	};

	bool bSourceSegmentStarted = false;
	if (Preview.FocusCaster.bVisible) {
		AddHoverCard(Preview.FocusCaster, 0.f);
		bSourceSegmentStarted = true;
	}
	if (Preview.Source.bVisible) {
		if (bSourceSegmentStarted) {
			AddHoverPlus(10.f);
		}
		AddHoverCard(Preview.Source, bSourceSegmentStarted ? 10.f : 0.f);
		bSourceSegmentStarted = true;
	}

	TArray<const FTacticsActionQueueHoverCardUi*> VisibleTargets;
	VisibleTargets.Reserve(Preview.Targets.Num());
	for (const FTacticsActionQueueHoverCardUi& TargetCard : Preview.Targets) {
		if (TargetCard.bVisible) {
			VisibleTargets.Add(&TargetCard);
		}
	}
	if (!VisibleTargets.IsEmpty() && bSourceSegmentStarted) {
		AddHoverArrow(16.f);
	}
	for (int32 TargetIdx = 0; TargetIdx < VisibleTargets.Num(); ++TargetIdx) {
		if (TargetIdx > 0) {
			AddHoverPlus(10.f);
		}
		AddHoverCard(*VisibleTargets[TargetIdx], TargetIdx > 0 ? 10.f : 0.f);
	}

	const bool bAbilitySource = Preview.Source.TypeLine.Equals(TEXT("ABILITY"), ESearchCase::IgnoreCase);
	Body->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, kActionQueueHoverBelowCardRowGap)
		[
			SNew(SBox)
				.MaxDesiredWidth(kActionQueueHoverScrollMaxWidth)
				.HeightOverride(kActionQueueHoverCardRowScrollH)
				[
					SNew(SScrollBox)
						.Orientation(Orient_Horizontal)
						.ScrollBarAlwaysVisible(true)
						+ SScrollBox::Slot()
							.Padding(0.f, 0.f, 0.f, 4.f)
							[
								CardRow
							]
				]
		];

	if (Preview.bIsAttack && Preview.AttackCompare.bShow) {
		Body->AddSlot().AutoHeight().Padding(0.f, kActionQueueHoverSectionGap, 0.f, 0.f)
			[
				SNew(SBorder)
					.BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
					.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.08f, 0.92f))
					.Padding(FMargin(10.f, 8.f))
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center)
							[
								BuildAttackCompareColumn(TEXT("Attacker"), Preview.AttackCompare.AttackerStatsMarkup, FString())
							]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(8.f, 0.f)
							[
								SNew(STextBlock)
									.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 18))
									.ColorAndOpacity(FLinearColor(0.72f, 0.76f, 0.82f, 1.f))
									.Text(FText::FromString(TEXT("vs")))
							]
						+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Center)
							[
								BuildAttackCompareColumn(TEXT("Defender"), Preview.AttackCompare.TargetStatsMarkup,
									Preview.AttackCompare.TargetCounterLabel)
							]
					]
			];
	}

	if (!Preview.Source.Description.IsEmpty()) {
		const bool bAdvancedGlossary = Subsystem->IsShowingAdvancedCardText();
		const TArray<FTacticsGlossaryNameBody> GlossaryMarkupEntries =
			BuildGlossaryNameBodies(Preview.SourceGlossary, TArray<FTacticsActiveEffectEntry>{});
		const FString DescMarkup = TacticsCardText::MarkupDescriptionText(
			TacticsCardText::FormatDescriptionProse(TacticsCardText::PrepareCardRulesTextForDisplay(Preview.Source.Description)),
			GlossaryMarkupEntries, bAdvancedGlossary);
		Body->AddSlot().AutoHeight().Padding(0.f, kActionQueueHoverSectionGap, 0.f, 0.f)
			[
				SNew(SBorder)
					.BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
					.BorderBackgroundColor(FLinearColor(0.05f, 0.05f, 0.08f, 0.92f))
					.Padding(FMargin(10.f, 8.f))
					[
						SNew(SRichTextBlock)
							.WrapTextAt(kActionQueueHoverScrollMaxWidth - 28.f)
							.LineHeightPercentage(1.2f)
							.TextStyle(&TacticsCardText::EnergyTextStyle(kQueueHoverPreviewDescFontSize, kCardTextWhite))
							.DecoratorStyleSet(&FCoreStyle::Get())
							.Decorators(TacticsCardText::MakeEnergyDecorators(kQueueHoverPreviewDescFontSize,
								bAbilitySource ? TacticsCardText::ESpeedTooltipSubject::Ability
											   : TacticsCardText::ESpeedTooltipSubject::Spell))
							.Text(FText::FromString(DescMarkup))
					]
			];
	}

	ActionQueueHoverOverlay->SetContent(
		SNew(SBorder)
			.BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
			.BorderBackgroundColor(FLinearColor(0.02f, 0.02f, 0.05f, 0.88f))
			.Padding(FMargin(14.f, 12.f))
			[
				Body
			]);
	ActionQueueHoverOverlay->SetVisibility(bPinned ? EVisibility::Visible : EVisibility::HitTestInvisible);
}


void STacticsBoardPanel::TryPresentNextPassiveAttackViz()
{
	if (!Subsystem.IsValid() || !Subsystem->HasPendingPassiveAttackVizEvents()) {
		return;
	}
	FCombatEncounter Enc;
	if (!Subsystem->TryConsumePassiveAttackVizEvent(Enc)) {
		return;
	}
	bPassiveVizDraining = true;
	bBattleVizAwaitingResume = false;
	bBattleVizShowingResult = false;
	Refresh();
	TArray<FCombatEncounter> Encounters;
	Encounters.Add(Enc);
	ShowBattleVisualization(Encounters);
	RefreshStatusText();
}

void STacticsBoardPanel::PresentPausedCombatOrAbilityVisualization()
{
	if (bPresentingPausedCombatViz) {
		return;
	}
	TGuardValue<bool> ReentryGuard(bPresentingPausedCombatViz, true);

	if (!Subsystem.IsValid() || !Subsystem->IsCombatVisualizationPaused()) {
		return;
	}
	if (bBattleVizAwaitingResume || bBattleVizShowingResult || Subsystem->HasActiveAbilityResolvePresentation()) {
		return;
	}
	if (Subsystem->HasPendingAbilityResolveVisualization()) {
		// Begin on-board presentation first - SetCombatScreenActive(false) tears down the attack
		// viz curtain and calls HideCombatViewportCurtain → Refresh; doing that before TryBegin
		// re-entered Refresh → PresentPaused in an infinite loop (stack overflow).
		if (Subsystem->TryBeginAbilityResolvePresentation()) {
			SetCombatScreenActive(false);
			return;
		}
		// Presentation failed (empty preview, apply error, etc.) - skip viz instead of
		// leaving combat_viz paused forever (blocks pass / all CLI).
		FString ResumeOut;
		Subsystem->ResumeCombatVisualization(ResumeOut);
		if (!ResumeOut.IsEmpty()) {
			LastCliOutput = ResumeOut;
		}
		SetCombatScreenActive(false);
		if (Subsystem->IsCombatVisualizationPaused()) {
			PresentPausedCombatOrAbilityVisualization();
		}
		return;
	}
	// Wait for ability damage popups to finish animating before hiding the board for attack viz.
	if (!Subsystem->GetActiveAbilityDamagePopups().IsEmpty()) {
		return;
	}
	TryPresentCombatVisualizationPause();
}

void STacticsBoardPanel::TryPresentCombatVisualizationPause()
{
	if (!Subsystem.IsValid() || !Subsystem->IsCombatVisualizationPaused()) {
		return;
	}
	if (!Subsystem->CaptureCombatVizPauseEncounter(ActiveCombatEncounter)) {
		// Attacker/defender gone (e.g. killed by an earlier queued resolution) - skip viz and
		// advance the batch instead of leaving combat_viz paused forever.
		FString ResumeOut;
		Subsystem->ResumeCombatVisualization(ResumeOut);
		if (!ResumeOut.IsEmpty()) {
			LastCliOutput = ResumeOut;
		}
		Refresh();
		if (Subsystem->IsCombatVisualizationPaused()) {
			PresentPausedCombatOrAbilityVisualization();
		}
		return;
	}
	bBattleVizShowingResult = false;
	Refresh();
	TArray<FCombatEncounter> Encounters;
	Encounters.Add(ActiveCombatEncounter);
	ShowBattleVisualization(Encounters);
	bBattleVizAwaitingResume = true;
	RefreshStatusText();
}

void STacticsBoardPanel::HandleBattleVisualizationDismissed()
{
	if (!Subsystem.IsValid()) {
		return;
	}
	UTacticsGameInstance* GI = Cast<UTacticsGameInstance>(Subsystem->GetGameInstance());

	if (bBattleVizAwaitingResume) {
		bBattleVizAwaitingResume = false;
		// Block PresentPaused during ResumeCombatVisualization's BroadcastRefresh - the core may
		// re-pause for the next queued attack before we show this fight's resolved outcome.
		bBattleVizShowingResult = true;
		FString ResumeOut;
		Subsystem->ResumeCombatVisualization(ResumeOut);
		if (!ResumeOut.IsEmpty()) {
			LastCliOutput = ResumeOut;
		}
		TArray<FCombatEncounter> Encounters;
		Encounters.Add(ActiveCombatEncounter);
		Subsystem->FillCombatEncountersAfter(Encounters);
		ActiveCombatEncounter = Encounters[0];
		if (GI) {
			GI->UpdateBattleVisualizationEncounters(Encounters);
		}
		RefreshStatusText();
		return;
	}

	if (bBattleVizShowingResult) {
		bBattleVizShowingResult = false;
		if (Subsystem->IsCombatVisualizationPaused()) {
			if (GI) {
				GI->CloseBattleVisualizationView(false);
			}
			PresentPausedCombatOrAbilityVisualization();
			return;
		}
		if (Subsystem->HasPendingPassiveAttackVizEvents()) {
			if (GI) {
				GI->CloseBattleVisualizationView(false);
			}
			TryPresentNextPassiveAttackViz();
			return;
		}
	}

	if (bPassiveVizDraining) {
		bPassiveVizDraining = false;
		if (Subsystem->HasPendingPassiveAttackVizEvents()) {
			TryPresentNextPassiveAttackViz();
			return;
		}
	}

	if (GI) {
		GI->ExitBattleVisualization();
	} else {
		SetCombatScreenActive(false);
	}
	Refresh();
}

void STacticsBoardPanel::SetMatchUiHidden(const bool bHidden)
{
	bMatchUiHidden = bHidden;
	SetVisibility(bHidden ? EVisibility::Collapsed : EVisibility::Visible);
}

void STacticsBoardPanel::SetCombatScreenActive(const bool bActive)
{
	const bool bWasActive = bCombatScreenActive;
	bCombatScreenActive = bActive;
	if (!Subsystem.IsValid()) {
		return;
	}
	UTacticsGameInstance* GI = Cast<UTacticsGameInstance>(Subsystem->GetGameInstance());
	if (!GI) {
		return;
	}
	if (!bActive && bWasActive) {
		GI->HideCombatViewportCurtain();
	}
	UWorld* World = GI->GetWorld();
	if (!World) {
		return;
	}
	if (ATactics3DBoardGameMode* GM = Cast<ATactics3DBoardGameMode>(World->GetAuthGameMode())) {
		if (ATactics3DWorldBoardActor* Board = GM->WorldBoardActor) {
			Board->SetActorHiddenInGame(bActive);
		}
	}
}

void STacticsBoardPanel::ShowBattleVisualization(const TArray<FCombatEncounter>& Encounters)
{
	if (Encounters.Num() == 0 || !Subsystem.IsValid()) {
		return;
	}
	UTacticsGameInstance* GI = Cast<UTacticsGameInstance>(Subsystem->GetGameInstance());
	if (!GI) {
		return;
	}

	// Hide the 3D match board while the battle stage is shown.
	SetCombatScreenActive(true);

	GI->EnterBattleVisualization(
		Encounters,
		FSimpleDelegate::CreateSP(this, &STacticsBoardPanel::HandleBattleVisualizationDismissed));
}

void STacticsBoardPanel::ShowAbilityDescription(const FString& AbilityKey)
{
	if (!AbilityDescriptionChrome.IsValid() || !AbilityDescriptionText.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	FString Desc;
	if (Subsystem->TryGetAbilityDescriptionForKey(AbilityKey, Desc) && !Desc.IsEmpty()) {
		const int32 Sep = Desc.Find(TEXT("\n\n"));
		if (Sep != INDEX_NONE) {
			Desc = Desc.Left(Sep + 2) + TacticsCardText::StripLeadingSpeedWordPrefix(Desc.Mid(Sep + 2));
		} else {
			Desc = TacticsCardText::StripLeadingSpeedWordPrefix(Desc);
		}
		TArray<FTacticsCardGlossaryEntry> GlossaryEntries;
		Subsystem->GetSelectedUnitCardGlossaryEntries(GlossaryEntries);
		TArray<FTacticsActiveEffectEntry> ActiveEffects;
		Subsystem->GetSelectedUnitActiveEffectEntries(ActiveEffects);
		const bool bAdvancedGlossary = Subsystem->IsShowingAdvancedCardText();
		const TArray<FTacticsGlossaryNameBody> GlossaryMarkupEntries = BuildGlossaryNameBodies(GlossaryEntries, ActiveEffects);
		AbilityDescriptionText->SetDecorators(
			TacticsCardText::MakeEnergyDecorators(10, TacticsCardText::ESpeedTooltipSubject::Ability));
		AbilityDescriptionText->SetText(FText::FromString(TacticsCardText::MarkupDescriptionText(
			TacticsCardText::FormatDescriptionProse(TacticsCardText::PrepareCardRulesTextForDisplay(Desc)),
			GlossaryMarkupEntries, bAdvancedGlossary)));
		AbilityDescriptionChrome->SetVisibility(EVisibility::Visible);
	}
}

void STacticsBoardPanel::HideAbilityDescription()
{
	if (AbilityDescriptionChrome.IsValid()) {
		AbilityDescriptionChrome->SetVisibility(EVisibility::Collapsed);
	}
	if (AbilityDescriptionText.IsValid()) {
		AbilityDescriptionText->SetText(FText::GetEmpty());
	}
}

void STacticsBoardPanel::RebuildDiscardPilePanel()
{
	if (!DiscardPileScroll.IsValid()) {
		return;
	}
	DiscardPileScroll->ClearChildren();
	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady() || !bShowDiscardPilePanel) {
		return;
	}
	const FLinearColor TextColor = GetChromeTextColor();
	const int32 PlayerCount = Subsystem->GetMatchPlayerCount();
	int32 TotalCards = 0;
	for (int32 Seat = 1; Seat <= PlayerCount; ++Seat) {
		TotalCards += Subsystem->GetPlayerDiscardCount(Seat);
	}
	if (TotalCards < 1) {
		DiscardPileScroll->AddSlot().Padding(0.f, 2.f)
			[SNew(STextBlock)
					.WrapTextAt(236.f)
					.Font(TacticsCardText::DesignFont(TEXT("Regular"), 8))
					.ColorAndOpacity(TextColor)
					.Text(FText::FromString(
						TEXT("(empty - your spells go here; killed units go in that player's pile)")))];
		return;
	}
	for (int32 Seat = 1; Seat <= PlayerCount; ++Seat) {
		const int32 N = Subsystem->GetPlayerDiscardCount(Seat);
		if (N < 1) {
			continue;
		}
		DiscardPileScroll->AddSlot().Padding(0.f, 6.f, 0.f, 4.f)
			[SNew(STextBlock)
					.WrapTextAt(236.f)
					.Font(TacticsCardText::DesignFont(TEXT("Bold"), 9))
					.ColorAndOpacity(TextColor)
					.Text(FText::FromString(FString::Printf(TEXT("P%d discard"), Seat)))];
		for (int32 Idx = 1; Idx <= N; ++Idx) {
			FString Name, CardKind, Rules;
			if (!Subsystem->TryGetPlayerDiscardCardUi(Seat, Idx, Name, CardKind, Rules)) {
				continue;
			}
			if (Name.Len() > 22) {
				Name = Name.Left(21) + TEXT("…");
			}
			const bool bUnit = CardKind.Equals(TEXT("unit"), ESearchCase::IgnoreCase);
			const bool bSpell = CardKind.Equals(TEXT("spell"), ESearchCase::IgnoreCase);
			const FLinearColor Frame = bSpell ? FLinearColor(0.12f, 0.28f, 0.52f, 1.f)
											  : bUnit ? FLinearColor(0.48f, 0.32f, 0.14f, 1.f)
													  : FLinearColor(0.28f, 0.28f, 0.32f, 1.f);
			DiscardPileScroll->AddSlot().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(SBorder).BorderBackgroundColor(Frame).Padding(FMargin(6.f, 4.f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
								  [SNew(STextBlock)
									   .WrapTextAt(220.f)
									   .Font(TacticsCardText::DesignFont(TEXT("Bold"), 9))
									   .ColorAndOpacity(TextColor)
									   .Text(FText::FromString(FString::Printf(TEXT("%s · %s"), *Name, *CardKind.ToUpper())))]
							+ SVerticalBox::Slot().AutoHeight()
								  [SNew(STextBlock)
									   .WrapTextAt(220.f)
									   .Font(TacticsCardText::DesignFont(TEXT("Regular"), 8))
									   .ColorAndOpacity(TextColor)
									   .Text(FText::FromString(Rules))]
						]
				];
		}
	}
}

void STacticsBoardPanel::RebuildPurgatoryPanel()
{
	if (!PurgatoryScroll.IsValid()) {
		return;
	}
	PurgatoryScroll->ClearChildren();
	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady() || !bShowPurgatoryPanel) {
		return;
	}
	const FLinearColor TextColor = GetChromeTextColor();
	const int32 PlayerCount = Subsystem->GetMatchPlayerCount();
	int32 TotalCards = 0;
	for (int32 Seat = 1; Seat <= PlayerCount; ++Seat) {
		TotalCards += Subsystem->GetPlayerPurgatoryCount(Seat);
	}
	if (TotalCards < 1) {
		PurgatoryScroll->AddSlot().Padding(0.f, 2.f)
			[SNew(STextBlock)
					.WrapTextAt(236.f)
					.Font(TacticsCardText::DesignFont(TEXT("Regular"), 8))
					.ColorAndOpacity(TextColor)
					.Text(FText::FromString(
						TEXT("(empty - cards removed by exile-like effects; not drawable)")))];
		return;
	}
	for (int32 Seat = 1; Seat <= PlayerCount; ++Seat) {
		const int32 N = Subsystem->GetPlayerPurgatoryCount(Seat);
		if (N < 1) {
			continue;
		}
		PurgatoryScroll->AddSlot().Padding(0.f, 6.f, 0.f, 4.f)
			[SNew(STextBlock)
					.WrapTextAt(236.f)
					.Font(TacticsCardText::DesignFont(TEXT("Bold"), 9))
					.ColorAndOpacity(TextColor)
					.Text(FText::FromString(FString::Printf(TEXT("P%d purgatory"), Seat)))];
		for (int32 Idx = 1; Idx <= N; ++Idx) {
			FString Name, CardKind, Rules;
			if (!Subsystem->TryGetPlayerPurgatoryCardUi(Seat, Idx, Name, CardKind, Rules)) {
				continue;
			}
			if (Name.Len() > 22) {
				Name = Name.Left(21) + TEXT("…");
			}
			const bool bUnit = CardKind.Equals(TEXT("unit"), ESearchCase::IgnoreCase);
			const bool bSpell = CardKind.Equals(TEXT("spell"), ESearchCase::IgnoreCase);
			const FLinearColor Frame = bSpell ? FLinearColor(0.18f, 0.14f, 0.42f, 1.f)
											  : bUnit ? FLinearColor(0.38f, 0.22f, 0.12f, 1.f)
													  : FLinearColor(0.22f, 0.22f, 0.28f, 1.f);
			PurgatoryScroll->AddSlot().Padding(0.f, 0.f, 0.f, 6.f)
				[
					SNew(SBorder).BorderBackgroundColor(Frame).Padding(FMargin(6.f, 4.f))
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
								  [SNew(STextBlock)
									   .WrapTextAt(220.f)
									   .Font(TacticsCardText::DesignFont(TEXT("Bold"), 9))
									   .ColorAndOpacity(TextColor)
									   .Text(FText::FromString(FString::Printf(TEXT("%s · %s"), *Name, *CardKind.ToUpper())))]
							+ SVerticalBox::Slot().AutoHeight()
								  [SNew(STextBlock)
									   .WrapTextAt(220.f)
									   .Font(TacticsCardText::DesignFont(TEXT("Regular"), 8))
									   .ColorAndOpacity(TextColor)
									   .Text(FText::FromString(Rules))]
						]
				];
		}
	}
}

void STacticsBoardPanel::RefreshBoardTargetHighlights()
{
	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady() || !BoardCellsOverlay.IsValid()) {
		return;
	}
	EnsureBoardGridBuilt();
	RefreshBoardGrid2DPresentations();
	if (BoardUnitArtOverlay.IsValid()) {
		BoardUnitArtOverlay->ClearChildren();
	}
	RebuildBoardUnitArtOverlay();
	RebuildBoardDamagePopupOverlay();
	if (CenterGridChrome.IsValid()) {
		CenterGridChrome->Invalidate(EInvalidateWidget::Visibility);
	}
	Invalidate(EInvalidateWidget::Paint);
}

void STacticsBoardPanel::Refresh()
{
	if (bMatchUiHidden) {
		return;
	}
	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady()) {
		if (HandScroll.IsValid()) {
			HandScroll->ClearChildren();
		}
		if (ReservesScroll.IsValid()) {
			ReservesScroll->ClearChildren();
		}
		RebuildDiscardPilePanel();
		RebuildPurgatoryPanel();
		RebuildSeatSwitcher();
		RebuildDiscardModal();
		RebuildModalSpellPicker();
		RebuildActionBar();
		RebuildBottomRightBar();
		if (CenterGridChrome.IsValid()) {
			CenterGridChrome->Invalidate(EInvalidateWidget::Visibility);
		}
		return;
	}
	if (!BoardCellsOverlay.IsValid()) {
		RebuildDiscardPilePanel();
		RebuildPurgatoryPanel();
		RebuildSeatSwitcher();
		RebuildDiscardModal();
		RebuildModalSpellPicker();
		RebuildActionBar();
		RebuildBottomRightBar();
		if (CenterGridChrome.IsValid()) {
			CenterGridChrome->Invalidate(EInvalidateWidget::Visibility);
		}
		return;
	}
	if (BoardUnitArtOverlay.IsValid()) {
		BoardUnitArtOverlay->ClearChildren();
	}

	// Bounds-cached grid rebuild + per-cell presentation-hash diffing (see RefreshBoardTargetHighlights),
	// not an unconditional wipe -- a full BoardCellWidgets.Empty() here defeated both caches on every
	// action-triggered Refresh(), rebuilding all ~96 cell widgets even when nothing changed.
	Subsystem->SyncActionQueueHoverAfterQueueChange();
	RefreshActionQueueHoverOverlay();
	EnsureBoardGridBuilt();
	RefreshBoardGrid2DPresentations();
	RebuildBoardUnitArtOverlay();
	RebuildBoardDamagePopupOverlay();

	if (!Subsystem->IsAwaitingHandDiscard() && !Subsystem->IsAwaitingTerritoryLoot()) {
		bDiscardHandCardSelected = false;
	}
	// Drop any stale armed territory-placement selection once the Energy-phase choice is gone.
	if (Subsystem->GetPendingEnergyZoneChoiceCount() < 1) {
		ArmedZoneChoiceIndex = -1;
	}
	// Same for the armed scan card once the scan is over.
	if (!Subsystem->IsAwaitingScan()) {
		ArmedScanCardIndex = -1;
	}
	if (Subsystem->IsAwaitingHandDiscard() || Subsystem->IsAwaitingTerritoryLoot()) {
		ClearPlayArmingState();
		RebuildHandBar();
		RebuildReservesBar();
	} else if (Subsystem->IsAwaitingScan()) {
		RebuildScanChoiceBar();
		RebuildReservesBar();
	} else if (Subsystem->GetPendingEnergyZoneChoiceCount() > 0) {
		RebuildZoneChoiceBar();
		RebuildReservesBar();
	} else {
		RebuildHandBar();
		RebuildReservesBar();
	}
	SyncCollapsibleRails();
	RebuildTerritoryBar();
	RebuildEnergyCountersBar();
	RebuildActionQueue();
	if (!bAbilityArmedForTile || ArmedAbilityKey.IsEmpty()) {
		HideAbilityDescription();
	}
	RebuildDiscardPilePanel();
	RebuildPurgatoryPanel();
	RebuildSeatSwitcher();
	RebuildDiscardModal();
	RebuildModalSpellPicker();
	RebuildActionBar();
	RebuildBottomRightBar();
	RefreshStatusText();
	if (WaitingTurnOverlay.IsValid()) {
		WaitingTurnOverlay->Invalidate(EInvalidateWidget::Visibility);
	}
	if (CenterGridChrome.IsValid()) {
		CenterGridChrome->Invalidate(EInvalidateWidget::Visibility);
	}
	if (Subsystem->IsCombatVisualizationPaused() && !bBattleVizAwaitingResume && !bBattleVizShowingResult
		&& !Subsystem->HasActiveAbilityResolvePresentation()) {
		PresentPausedCombatOrAbilityVisualization();
	}
	Invalidate(EInvalidateWidget::LayoutAndVolatility);
}

void STacticsBoardPanel::RefreshBoardUi()
{
	Refresh();
}
