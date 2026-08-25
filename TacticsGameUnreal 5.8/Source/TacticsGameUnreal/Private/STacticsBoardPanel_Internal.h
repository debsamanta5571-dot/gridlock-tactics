#pragma once

namespace
{
constexpr float kCellW = 96.f;
constexpr float kCellH = 80.f;
/** Gap between adjacent tile widgets so borders/button chrome do not stack. */
constexpr float kCellGap = 2.f;
constexpr float kCellInnerW = kCellW - kCellGap;
constexpr float kCellInnerH = kCellH - kCellGap;

const FButtonStyle& BoardCellButtonStyle()
{
	return FAppStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder");
}
constexpr FLinearColor kUiBlack(0.f, 0.f, 0.f, 1.f);
/** Brown grout between 2D board cells (matches 3D grid lines). */
constexpr FLinearColor kBoardGridGroutBrown(0.46f, 0.28f, 0.12f, 1.f);
constexpr FLinearColor kCardTextWhite(1.f, 1.f, 1.f, 1.f);
constexpr float kHandCardArtW = 212.f;
constexpr float kHandCardArtH = 292.f;
constexpr float kHandCardGlossaryW = 200.f;
constexpr float kHandCardActionIconStackSize = 50.f;
constexpr float kHandCardActionIconStackGap = 0.f;
constexpr int32 kHandCardActionIconMaxStack = 3;
constexpr float kHandCardAbilityUseIconSize = 28.f;
constexpr float kHandCardAbilityUsePipGap = 3.f;
constexpr FLinearColor kAbilityUseTrayBg(0.05f, 0.04f, 0.08f, 0.82f);
constexpr FLinearColor kAbilityUseTrayArmedBg(0.14f, 0.05f, 0.12f, 0.9f);
constexpr FLinearColor kAbilityUseTrayArmedBorder(0.78f, 0.22f, 0.62f, 0.95f);
constexpr FLinearColor kAbilityUseSpentTint(1.f, 1.f, 1.f, 0.38f);
constexpr float kHandCardPanelW = 580.f;
/** Rules scroller scrollbar reserve so prose wrap matches visible width. */
constexpr float kHandCardDetailRulesScrollbarReserve = 16.f;
/** Clickable ability pill chrome: SBorder 2px + SButton 8px content padding per side. */
constexpr float kHandCardAbilityPillClickableChromeW = 20.f;
constexpr float kHandCardDescWrapWidth = kHandCardPanelW - kHandCardDetailRulesScrollbarReserve;
constexpr float kHandCardDescPillWrapWidth = kHandCardDescWrapWidth - kHandCardAbilityPillClickableChromeW;
constexpr int32 kHandCardDescFontSize = 13;
constexpr int32 kHandCardGlossaryFontSize = 11;
constexpr float kHandCardKindRowHeight = 28.f;
constexpr float kReservesCardKindRowHeight = 26.f;
/** Fixed detail-panel chrome so selecting different cards does not resize or jump the overlay. */
constexpr float kHandCardDetailTitleH = 40.f;
constexpr float kHandCardDetailCostRowH = 28.f;
constexpr float kHandCardActionIconAreaH = kHandCardActionIconStackSize * static_cast<float>(kHandCardActionIconMaxStack);
constexpr float kHandCardDetailStatsRowH = 50.f;
/** Width reserved on the stat row when board unit action icon stacks are visible (3×50 + gaps). */
constexpr float kHandCardDetailActionIconsRowReserve =
	kHandCardActionIconStackSize * 3.f + 6.f * 2.f;
constexpr float kHandCardDetailDividerBlockH = 9.f;
constexpr float kHandCardDetailAbilityHotbarH = 56.f;
constexpr float kHandCardDetailRulesH = 228.f;
constexpr float kHandCardDetailPanelH = kHandCardDetailTitleH + 6.f + kHandCardArtH + 8.f + kHandCardDetailCostRowH + 7.f
	+ kHandCardDetailStatsRowH + 7.f + kHandCardDetailDividerBlockH + 8.f + kHandCardDetailRulesH + 6.f
	+ kHandCardDetailAbilityHotbarH;
constexpr int32 kHandCardActiveEffectFontSize = 11;
constexpr FLinearColor kCardRulesDividerColor(1.f, 1.f, 1.f, 0.14f);

const FSlateBrush* CardChromeFillBrush()
{
	return FCoreStyle::Get().GetBrush("WhiteBrush");
}

TSharedRef<SWidget> WrapIconArtToolTip(TSharedRef<SWidget> Inner, const FString& ArtId)
{
	const TSharedPtr<IToolTip> HoverTip = TacticsCardText::MakeGlossaryHoverToolTip(
		IconGlossaryTooltipForArtId(ArtId));
	if (!HoverTip.IsValid()) {
		return Inner;
	}
	return SNew(SBox)
		.ToolTip(HoverTip)
		[
			Inner
		];
}

TSharedRef<SWidget> BuildAbilityUsePip(const FSlateBrush* Brush, const bool bSpent, const FString& ArtId)
{
	return WrapIconArtToolTip(
		SNew(SBox)
			.WidthOverride(kHandCardAbilityUseIconSize)
			.HeightOverride(kHandCardAbilityUseIconSize)
			[
				SNew(SImage)
					.Image(Brush)
					.ColorAndOpacity(bSpent ? kAbilityUseSpentTint : FLinearColor::White)
			],
		ArtId);
}

TSharedRef<SWidget> BuildAbilityUseIconStack(const int32 UsesRemaining, const int32 UsesMax)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	if (UsesMax <= 0) {
		return Row;
	}
	const FVector2D IconSize(kHandCardAbilityUseIconSize, kHandCardAbilityUseIconSize);
	const int32 Spent = FMath::Clamp(UsesMax - UsesRemaining, 0, UsesMax);
	const FSlateBrush* ReadyBrush = TacticsCardArtUi::GetCardArtBrush(TEXT("ui/actions/ability_ready"), IconSize);
	const FSlateBrush* UsedBrush = TacticsCardArtUi::GetCardArtBrush(TEXT("ui/actions/ability_used"), IconSize);
	for (int32 i = 0; i < UsesRemaining; ++i) {
		Row->AddSlot()
			.AutoWidth()
			.Padding(i > 0 ? kHandCardAbilityUsePipGap : 0.f, 0.f, 0.f, 0.f)
			.VAlign(VAlign_Center)
			[
				BuildAbilityUsePip(ReadyBrush, false, TEXT("ui/actions/ability_ready"))
			];
	}
	for (int32 i = 0; i < Spent; ++i) {
		Row->AddSlot()
			.AutoWidth()
			.Padding((UsesRemaining > 0 || i > 0) ? kHandCardAbilityUsePipGap : 0.f, 0.f, 0.f, 0.f)
			.VAlign(VAlign_Center)
			[
				BuildAbilityUsePip(UsedBrush, true, TEXT("ui/actions/ability_used"))
			];
	}
	return Row;
}

TSharedRef<SWidget> BuildAbilityUseMeterTray(const int32 UsesRemaining, const int32 UsesMax, const bool bArmed)
{
	return SNew(SBorder)
		.BorderBackgroundColor(bArmed ? kAbilityUseTrayArmedBorder : FLinearColor(0.f, 0.f, 0.f, 0.f))
		.Padding(bArmed ? FMargin(1.f) : FMargin(0.f))
		[
			SNew(SBorder)
				.BorderBackgroundColor(bArmed ? kAbilityUseTrayArmedBg : kAbilityUseTrayBg)
				.Padding(FMargin(5.f, 2.f))
				[
					BuildAbilityUseIconStack(UsesRemaining, UsesMax)
				]
		];
}

TSharedRef<SWidget> BuildAbilityPillHeaderRow(const FString& MetadataStrip, const int32 UsesRemaining, const int32 UsesMax,
	const bool bShowUses, const bool bArmed, const int32 MetadataFontSize)
{
	const bool bShowMetadata = !MetadataStrip.IsEmpty();
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	if (bShowMetadata) {
		Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				TacticsCardText::BuildMetadataPillWidget(MetadataStrip, MetadataFontSize, kCardTextWhite)
			];
	}
	Row->AddSlot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			SNullWidget::NullWidget
		];
	if (bShowUses) {
		Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Right)
			[
				BuildAbilityUseMeterTray(UsesRemaining, UsesMax, bArmed)
			];
	}
	return Row;
}

void RebuildUnitActionIconStack(const TSharedPtr<SVerticalBox>& Box, const TArray<FString>& ArtIds)
{
	if (!Box.IsValid()) {
		return;
	}
	Box->ClearChildren();
	if (ArtIds.IsEmpty()) {
		return;
	}
	const FVector2D IconSize(kHandCardActionIconStackSize, kHandCardActionIconStackSize);
	Box->AddSlot()
		.FillHeight(1.f)
		[
			SNullWidget::NullWidget
		];
	for (int32 i = 0; i < ArtIds.Num(); ++i) {
		const FString& ArtId = ArtIds[i];
		const FSlateBrush* Brush = ArtId.IsEmpty() ? nullptr : TacticsCardArtUi::GetCardArtBrush(ArtId, IconSize);
		const bool bStackedAbove = i > 0;
		Box->AddSlot()
			.AutoHeight()
			.Padding(0.f, bStackedAbove ? kHandCardActionIconStackGap : 0.f, 0.f, 0.f)
			[
				WrapIconArtToolTip(
					SNew(SBox)
						.WidthOverride(kHandCardActionIconStackSize)
						.HeightOverride(kHandCardActionIconStackSize)
						[
							SNew(SImage)
								.Image(Brush)
						],
					ArtId)
			];
	}
}

constexpr int32 kUnitHoverEffectFontSize = 10;
constexpr float kUnitHoverPanelWidth = 148.f;
constexpr float kUnitHoverBelowArtGap2D = 36.f;
constexpr float kUnitHoverBelowArt2DScale = 0.32f;
/** Base screen gap below projected sprite bottom, plus a fraction of on-screen sprite height. */
constexpr float kUnitHoverBelowArtGap3DScreen = 80.f;
constexpr float kUnitHoverBelowArtScreenHeightScale = 0.55f;

TSharedRef<SWidget> BuildUnitHoverEffectPill(const FTacticsActiveEffectEntry& Entry)
{
	const FLinearColor BorderColor = Entry.bNegative ? FLinearColor(0.52f, 0.22f, 0.20f, 1.f)
		: Entry.bPositive ? FLinearColor(0.18f, 0.40f, 0.30f, 1.f)
		: FLinearColor(0.28f, 0.32f, 0.38f, 1.f);
	const FLinearColor TextColor = Entry.bNegative ? FLinearColor(0.96f, 0.74f, 0.70f, 1.f)
		: Entry.bPositive ? FLinearColor(0.74f, 0.94f, 0.84f, 1.f)
		: FLinearColor(0.86f, 0.88f, 0.92f, 1.f);
	return SNew(SBorder)
		.BorderBackgroundColor(BorderColor)
		.Padding(FMargin(1.f))
		.ToolTip(TacticsCardText::MakeGlossaryHoverToolTip(Entry.Body))
		[
			SNew(SBorder)
				.BorderBackgroundColor(FLinearColor(0.08f, 0.09f, 0.12f, 1.f))
				.Padding(FMargin(5.f, 2.f))
				[
					SNew(STextBlock)
						.Text(FText::FromString(Entry.Name))
						.Font(TacticsCardText::DesignFont(TEXT("Bold"), kUnitHoverEffectFontSize))
						.ColorAndOpacity(TextColor)
						.Justification(ETextJustify::Center)
				]
		];
}

TSharedRef<SWidget> BuildUnitHoverPanelContent(const FTacticsUnitHoverPresentation& Hover)
{
	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox);
	const bool bHasFooterContent = !Hover.Effects.IsEmpty() || !Hover.ActionHint.IsEmpty();
	Column->AddSlot()
		.AutoHeight()
		.Padding(0.f, 0.f, 0.f, !Hover.OwnerLine.IsEmpty() || bHasFooterContent ? 4.f : 0.f)
		[
			SNew(STextBlock)
				.Text(FText::FromString(Hover.HeaderLine))
				.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 12))
				.ColorAndOpacity(kCardTextWhite)
				.Justification(ETextJustify::Center)
				.WrapTextAt(kUnitHoverPanelWidth)
		];
	if (!Hover.OwnerLine.IsEmpty()) {
		Column->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, bHasFooterContent ? 4.f : 0.f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(Hover.OwnerLine))
					.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 10))
					.ColorAndOpacity(FLinearColor(0.82f, 0.86f, 0.92f, 1.f))
					.Justification(ETextJustify::Center)
					.WrapTextAt(kUnitHoverPanelWidth)
			];
	}
	if (!Hover.ActionHint.IsEmpty()) {
		Column->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, Hover.Effects.IsEmpty() ? 0.f : 4.f)
			[
				SNew(STextBlock)
					.Text(FText::FromString(Hover.ActionHint))
					.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 10))
					.ColorAndOpacity(FLinearColor(0.78f, 0.82f, 0.88f, 1.f))
					.Justification(ETextJustify::Center)
					.WrapTextAt(kUnitHoverPanelWidth)
			];
	}
	for (const FTacticsActiveEffectEntry& Entry : Hover.Effects) {
		if (Entry.Name.IsEmpty()) {
			continue;
		}
		Column->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 3.f)
			[
				BuildUnitHoverEffectPill(Entry)
			];
	}
	return SNew(SBorder)
		.BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
		.BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor())
		.Padding(FMargin(10.f, 6.f))
		[
			Column
		];
}

void RebuildActiveEffectsSidebar(const TSharedPtr<SVerticalBox>& Box, const TArray<FTacticsActiveEffectEntry>& Entries)
{
	if (!Box.IsValid()) {
		return;
	}
	Box->ClearChildren();
	if (Entries.IsEmpty()) {
		return;
	}
	for (int32 i = 0; i < Entries.Num(); ++i) {
		const FTacticsActiveEffectEntry& Entry = Entries[i];
		if (Entry.Name.IsEmpty()) {
			continue;
		}
		const FLinearColor BorderColor = Entry.bNegative ? FLinearColor(0.52f, 0.22f, 0.20f, 1.f)
			: Entry.bPositive ? FLinearColor(0.18f, 0.40f, 0.30f, 1.f)
			: FLinearColor(0.28f, 0.32f, 0.38f, 1.f);
		const FLinearColor TextColor = Entry.bNegative ? FLinearColor(0.96f, 0.74f, 0.70f, 1.f)
			: Entry.bPositive ? FLinearColor(0.74f, 0.94f, 0.84f, 1.f)
			: FLinearColor(0.86f, 0.88f, 0.92f, 1.f);
		Box->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(SBorder)
					.BorderBackgroundColor(BorderColor)
					.Padding(FMargin(1.f))
					.ToolTip(TacticsCardText::MakeGlossaryHoverToolTip(Entry.Body))
					[
						SNew(SBorder)
							.BorderBackgroundColor(FLinearColor(0.08f, 0.09f, 0.12f, 1.f))
							.Padding(FMargin(6.f, 2.f))
							[
								SNew(STextBlock)
									.Text(FText::FromString(Entry.Name))
									.Font(TacticsCardText::DesignFont(TEXT("Bold"), kHandCardActiveEffectFontSize))
									.ColorAndOpacity(TextColor)
									.WrapTextAt(kHandCardGlossaryW - 16.f)
							]
					]
			];
	}
}

TArray<FTacticsGlossaryNameBody> BuildGlossaryNameBodies(const TArray<FTacticsCardGlossaryEntry>& GlossaryEntries,
	const TArray<FTacticsActiveEffectEntry>& ActiveEffects)
{
	TArray<FTacticsGlossaryNameBody> Source;
	Source.Reserve(GlossaryEntries.Num() + ActiveEffects.Num());
	for (const FTacticsCardGlossaryEntry& Entry : GlossaryEntries) {
		if (!Entry.Name.IsEmpty() && !Entry.Body.IsEmpty()) {
			Source.Add({Entry.Key, Entry.Name, Entry.Body});
		}
	}
	for (const FTacticsActiveEffectEntry& Entry : ActiveEffects) {
		if (!Entry.Name.IsEmpty() && !Entry.Body.IsEmpty()) {
			Source.Add({Entry.Key, Entry.Name, Entry.Body});
		}
	}
	return Source;
}

void RebuildHandCardGlossaryVBox(const TSharedPtr<SVerticalBox>& Box, const TArray<FTacticsCardGlossaryEntry>& Entries,
	const TArray<FTacticsGlossaryNameBody>& GlossaryMarkupEntries, const bool bAdvancedGlossary)
{
	if (!Box.IsValid()) {
		return;
	}
	Box->ClearChildren();
	if (Entries.IsEmpty()) {
		return;
	}
	for (int32 i = 0; i < Entries.Num(); ++i) {
		const FTacticsCardGlossaryEntry& Entry = Entries[i];
		if (Entry.Name.IsEmpty() || Entry.Body.IsEmpty()) {
			continue;
		}
		const FString BodyProse = TacticsCardText::FormatDescriptionProse(Entry.Body);
		Box->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, i + 1 < Entries.Num() ? 8.f : 0.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
					[
						SNew(STextBlock)
							.Text(FText::FromString(Entry.Name))
							.Font(TacticsCardText::DesignFont(TEXT("Bold"), kHandCardGlossaryFontSize))
							.ColorAndOpacity(FLinearColor(0.95f, 0.86f, 0.55f, 1.f))
							.WrapTextAt(kHandCardGlossaryW)
					]
				+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SRichTextBlock)
							.WrapTextAt(kHandCardGlossaryW)
							.LineHeightPercentage(1.15f)
							.TextStyle(&TacticsCardText::EnergyTextStyle(kHandCardGlossaryFontSize,
								FLinearColor(0.82f, 0.86f, 0.92f, 1.f)))
							.DecoratorStyleSet(&FCoreStyle::Get())
							.Decorators(TacticsCardText::MakeEnergyDecorators(kHandCardGlossaryFontSize))
							.Text(FText::FromString(
								TacticsCardText::MarkupDescriptionText(BodyProse, GlossaryMarkupEntries, bAdvancedGlossary)))
					]
			];
	}
}



TSharedRef<SButton> MakeCmdButton(const FText& Label, FOnClicked OnClicked)
{
	return SNew(SButton)
		.ForegroundColor(kCardTextWhite)
		.ContentPadding(FMargin(14.f, 8.f))
		.OnClicked(OnClicked)
		[
			SNew(STextBlock)
				.Text(Label)
				.Justification(ETextJustify::Center)
				.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 12))
		];
}

/** Viewport margins so 3D board tiles stay visible under overlay chrome (hand ~168px + padding). */
constexpr float k3DBoardSafeBottom = 214.f;
constexpr float k3DBoardSafeTop = 120.f;
/** Left status / action column width; board safe inset adds k3DBoardGapFromChrome after it. */
constexpr float k3DLeftChromeWidth = 500.f;
constexpr float k3DBoardGapFromChrome = 36.f;
/** Reserves sit in a collapsible left rail. When expanded they clear the (optional) card-detail
 *  panel; when collapsed only a slim edge chip is reserved. */
constexpr float kReservesColumnW = 140.f;
constexpr float kRailChipW = 38.f;
constexpr float kReservesColumnLeftBesideDetail = 16.f + kHandCardPanelW + 8.f;
/** @deprecated Prefer GetDynamicBoardSafeLeft - kept as the worst-case expanded left inset. */
constexpr float kReservesColumnLeft = kReservesColumnLeftBesideDetail;
constexpr float k3DBoardSafeLeft = kReservesColumnLeft + kReservesColumnW + k3DBoardGapFromChrome;
constexpr float kHandStripCardHeight = 196.f;
constexpr float kQueueHoverPreviewCardW = 118.f;
constexpr float kQueueHoverPreviewCardH = 164.f;
constexpr float kQueueHoverPreviewDescWrapW = 220.f;
/** Batch-queue hover preview sits below the queue chrome; header + scroll cap drive dynamic top inset. */
constexpr float kActionQueueHoverQueueChromeTop = 6.f;
constexpr float kActionQueueHoverQueuePanelHeaderH = 92.f;
constexpr float kActionQueueHoverQueueScrollMaxH = 420.f;
constexpr float kActionQueueHoverQueueRowH = 36.f;
constexpr float kActionQueueHoverBelowQueueGap = 14.f;
/** Type chip + art + wrapped name label, then horizontal scrollbar beneath (not over names). */
constexpr float kQueueHoverPreviewTypeLabelH = 18.f;
constexpr float kQueueHoverPreviewNameLabelH = 36.f;
constexpr float kActionQueueHoverScrollBarH = 16.f;
constexpr float kActionQueueHoverCardRowScrollH = kQueueHoverPreviewTypeLabelH + kQueueHoverPreviewCardH
	+ kQueueHoverPreviewNameLabelH + kActionQueueHoverScrollBarH;
constexpr float kActionQueueHoverBelowCardRowGap = 14.f;
constexpr float kActionQueueHoverSectionGap = 18.f;
constexpr int32 kQueueHoverPreviewDescFontSize = 11;
constexpr float kQueueHoverAttackCompareColW = 168.f;
constexpr int32 kQueueHoverAttackCompareFontSize = 11;
// Right-side Territories rail (collapsible). When collapsed only kRailChipW is reserved beside the
// action-queue / stats columns. (Reserves live on the left.)
constexpr float kEnergyReservesPanelWidth = 168.f;
constexpr float kActionQueuePanelWidth = 236.f;
constexpr float kRightSidePanelWidth = 264.f;
constexpr float kAbilityDescriptionPanelWidth = 220.f;
constexpr float kAbilityButtonsWrapWidth = 128.f;
constexpr float kSidePanelGap = 8.f;
constexpr float kActionQueueHoverScrollMaxWidth =
	kActionQueuePanelWidth + kRightSidePanelWidth + kSidePanelGap;
/** Right rail (left→right): territories (or chip), batch queue, stats/abilities - worst-case inset. */
constexpr float k3DBoardSafeRight = kEnergyReservesPanelWidth + kSidePanelGap + kActionQueuePanelWidth
	+ kSidePanelGap + kRightSidePanelWidth + 10.f;
constexpr float kEnergyHudMaxWidth = 420.f;
constexpr float k3DLeftLogMaxHeight = 280.f;
constexpr TCHAR kBoardUiPrefsSection[] = TEXT("TacticsBoardUi");

uint32 HashCellPresentation(const FTacticsBoardCellPresentation& Cell, const bool bDeployValid, const bool bAoEHoverPreview)
{
	uint32 Hash = GetTypeHash(Cell.Summary);
	Hash = HashCombine(Hash, GetTypeHash(Cell.TurnOrderRank));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bSelected));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bPendingCliTarget));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bReachableMove));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bPendingMoveDestination));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bPendingMoveOrigin));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bAttackTarget));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bBoardTargetEnemy));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bBoardTargetOther));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bBoardTargetAoE));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bActionQueueHoverSource));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bActionQueueHoverTarget));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bActionQueueHoverAoE));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bAbilityBoardTarget));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bAbilityBoardTargetEnemy));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bResolveFlash));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Cell.ResolveFlashAlpha * 20.f)));
	Hash = HashCombine(Hash, GetTypeHash(FMath::RoundToInt(Cell.ResolveFlashScale * 10.f)));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bRoad));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bRough));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bDamagingTerrain));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bVoidTerrain));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bAetherTerrain));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bScannerTerrain));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bOmniEnergyTerrain));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bHasUnit));
	Hash = HashCombine(Hash, GetTypeHash(Cell.bHasControllableUnit));
	Hash = HashCombine(Hash, GetTypeHash(bDeployValid));
	Hash = HashCombine(Hash, GetTypeHash(bAoEHoverPreview));
	return Hash;
}

bool IsCursorInsideWidget(const TSharedPtr<SWidget>& Widget)
{
	if (!Widget.IsValid() || !FSlateApplication::IsInitialized()) {
		return false;
	}
	const FGeometry& Geo = Widget->GetCachedGeometry();
	const FVector2D Size = Geo.GetLocalSize();
	if (Size.X <= KINDA_SMALL_NUMBER || Size.Y <= KINDA_SMALL_NUMBER) {
		return false;
	}
	const FVector2D LocalCursor = Geo.AbsoluteToLocal(FSlateApplication::Get().GetCursorPos());
	return LocalCursor.X >= 0.f && LocalCursor.Y >= 0.f
		&& LocalCursor.X <= Size.X && LocalCursor.Y <= Size.Y;
}

}  // namespace
