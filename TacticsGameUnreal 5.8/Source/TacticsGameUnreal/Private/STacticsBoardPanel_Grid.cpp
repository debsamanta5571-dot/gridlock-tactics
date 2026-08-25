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


TSharedRef<SWidget> STacticsBoardPanel::BuildCellButton(int WorldX, int WorldY)
{
	const FTacticsBoardCellPresentation Cell = Subsystem.IsValid() ? Subsystem->GetCellPresentationAtWorld(WorldX, WorldY) : FTacticsBoardCellPresentation{};
	const bool bSel = Cell.bSelected;
	const bool bPending = Cell.bPendingCliTarget;
	const bool bMoveReach = Cell.bReachableMove;
	const bool bPendingMoveDest = Cell.bPendingMoveDestination;
	const bool bPendingMoveOrigin = Cell.bPendingMoveOrigin;
	const bool bAttackTarget = Cell.bAttackTarget;
	const bool bBoardTargetEnemy = Cell.bBoardTargetEnemy;
	const bool bBoardTargetOther = Cell.bBoardTargetOther;
	const bool bBoardTargetAoE = Cell.bBoardTargetAoE;
	const bool bActionQueueHoverSource = Cell.bActionQueueHoverSource;
	const bool bActionQueueHoverTarget = Cell.bActionQueueHoverTarget;
	const bool bActionQueueHoverAoE = Cell.bActionQueueHoverAoE;
	const bool bAbilityPreview = Subsystem.IsValid() && Subsystem->IsAbilityBoardTargetPreviewActive();
	const bool bAbilityBoardTarget = Cell.bAbilityBoardTarget;
	const bool bAbilityBoardTargetEnemy = Cell.bAbilityBoardTargetEnemy;
	const bool bResolveFlash = Cell.bResolveFlash;
	const float ResolveFlashAlpha = Cell.ResolveFlashAlpha;
	const float ResolveFlashScale = Cell.ResolveFlashScale;
	const FLinearColor ResolveFlashColor = Cell.ResolveFlashColor;
	const bool bAoEHoverPreview = Subsystem.IsValid() && Subsystem->IsBoardTargetAoEHoverPreviewActive();
	const bool bRoad = Cell.bRoad;
	const bool bRough = Cell.bRough;
	const bool bDamagingTerrain = Cell.bDamagingTerrain;
	const bool bVoidTerrain = Cell.bVoidTerrain;
	const bool bAetherTerrain = Cell.bAetherTerrain;
	const bool bScannerTerrain = Cell.bScannerTerrain;
	const bool bOmniEnergyTerrain = Cell.bOmniEnergyTerrain;
	const bool bObjectiveTerrain = bAetherTerrain || bScannerTerrain || bOmniEnergyTerrain;
	const bool bDeployValid = Subsystem.IsValid() && Subsystem->IsDeployPreviewArmed()
		&& (bHandArmedForTile || bReservesArmedForTile)
		&& Subsystem->IsDeployValidCellAtWorld(WorldX, WorldY);
	FString Label = Subsystem.IsValid() ? Cell.Summary : FString(TEXT("?"));
	if (Label.Len() > 18) {
		Label = Label.Left(17) + TEXT("...");
	}
	const bool bShowLabel = Cell.bHasUnit || Cell.bHasControllableUnit || (!Label.IsEmpty() && !Label.Equals(TEXT("empty"), ESearchCase::IgnoreCase));
	const int32 TurnOrderRank = Cell.TurnOrderRank;

	FLinearColor BorderCol = kBoardGridGroutBrown;
	if (bActionQueueHoverSource) {
		BorderCol = FLinearColor(0.95f, 0.78f, 0.2f, 1.f);
	} else if (bSel) {
		BorderCol = FLinearColor(0.95f, 0.75f, 0.15f, 1.f);
	} else if (bActionQueueHoverTarget) {
		BorderCol = FLinearColor(0.95f, 0.12f, 0.08f, 1.f);
	} else if (bActionQueueHoverAoE) {
		BorderCol = FLinearColor(0.98f, 0.52f, 0.1f, 1.f);
	} else if (bPendingMoveDest) {
		BorderCol = FLinearColor(0.15f, 0.88f, 0.95f, 1.f);
	} else if (bPendingMoveOrigin) {
		BorderCol = FLinearColor(0.95f, 0.62f, 0.12f, 1.f);
	} else if (bAoEHoverPreview && bBoardTargetAoE) {
		BorderCol = FLinearColor(0.98f, 0.52f, 0.1f, 1.f);
	} else if (bAbilityBoardTargetEnemy) {
		BorderCol = FLinearColor(0.88f, 0.16f, 0.62f, 1.f);
	} else if (bAbilityBoardTarget) {
		BorderCol = FLinearColor(0.52f, 0.18f, 0.78f, 1.f);
	} else if (bAoEHoverPreview && bBoardTargetOther) {
		BorderCol = FLinearColor(0.2f, 0.46f, 0.95f, 1.f);
	} else if (bAttackTarget || bBoardTargetEnemy) {
		BorderCol = FLinearColor(0.95f, 0.12f, 0.08f, 1.f);
	} else if (bBoardTargetAoE) {
		BorderCol = FLinearColor(0.98f, 0.52f, 0.1f, 1.f);
	} else if (bBoardTargetOther) {
		BorderCol = FLinearColor(0.2f, 0.46f, 0.95f, 1.f);
	} else if (bPending) {
		BorderCol = FLinearColor(0.35f, 0.85f, 0.45f, 1.f);
	} else if (bMoveReach) {
		BorderCol = FLinearColor(0.2f, 0.72f, 0.38f, 1.f);
	} else if (bDeployValid) {
		BorderCol = FLinearColor(0.95f, 0.72f, 0.18f, 1.f);
	}
	auto ObjectiveTerrainBaseColor = [&]() -> FLinearColor {
		if (bAetherTerrain) {
			return FLinearColor(0.34f, 0.16f, 0.48f, 1.f);
		}
		if (bScannerTerrain) {
			return FLinearColor(0.08f, 0.42f, 0.52f, 1.f);
		}
		if (bOmniEnergyTerrain) {
			return FLinearColor(0.52f, 0.42f, 0.08f, 1.f);
		}
		return FLinearColor(0.08f, 0.08f, 0.1f, 1.f);
	};
	FLinearColor ButtonCol = bObjectiveTerrain ? ObjectiveTerrainBaseColor()
		: bVoidTerrain ? FLinearColor(0.02f, 0.02f, 0.06f, 1.f)
		: bDamagingTerrain				? FLinearColor(0.52f, 0.12f, 0.06f, 1.f)
		: bRoad						? FLinearColor(0.48f, 0.38f, 0.2f, 1.f)
		: bRough					? FLinearColor(0.18f, 0.32f, 0.14f, 1.f)
								: FLinearColor(0.08f, 0.08f, 0.1f, 1.f);
	if (bActionQueueHoverSource) {
		ButtonCol = FLinearColor(0.42f, 0.34f, 0.06f, 1.f);
	} else if (bActionQueueHoverTarget) {
		ButtonCol = FLinearColor(0.42f, 0.1f, 0.08f, 1.f);
	} else if (bActionQueueHoverAoE) {
		ButtonCol = bVoidTerrain ? FLinearColor(0.42f, 0.22f, 0.06f, 1.f)
			: bDamagingTerrain ? FLinearColor(0.58f, 0.28f, 0.08f, 1.f)
			: bRoad ? FLinearColor(0.52f, 0.36f, 0.12f, 1.f)
			: bRough ? FLinearColor(0.42f, 0.38f, 0.1f, 1.f)
			: FLinearColor(0.48f, 0.3f, 0.1f, 1.f);
	} else if (bAoEHoverPreview && bBoardTargetAoE) {
		ButtonCol = bVoidTerrain ? FLinearColor(0.42f, 0.22f, 0.06f, 1.f)
			: bDamagingTerrain ? FLinearColor(0.58f, 0.28f, 0.08f, 1.f)
			: bRoad	? FLinearColor(0.52f, 0.36f, 0.12f, 1.f)
			: bRough		? FLinearColor(0.42f, 0.38f, 0.1f, 1.f)
							: FLinearColor(0.48f, 0.3f, 0.1f, 1.f);
	} else if (bAbilityBoardTargetEnemy) {
		ButtonCol = FLinearColor(0.34f, 0.08f, 0.24f, 1.f);
	} else if (bAbilityBoardTarget) {
		ButtonCol = FLinearColor(0.2f, 0.1f, 0.32f, 1.f);
	} else if (bAoEHoverPreview && bBoardTargetOther) {
		ButtonCol = bVoidTerrain ? FLinearColor(0.06f, 0.1f, 0.22f, 1.f)
			: bDamagingTerrain ? FLinearColor(0.42f, 0.18f, 0.12f, 1.f)
			: bRoad	? FLinearColor(0.22f, 0.42f, 0.2f, 1.f)
			: bRough		? FLinearColor(0.14f, 0.36f, 0.18f, 1.f)
							: FLinearColor(0.12f, 0.28f, 0.42f, 1.f);
	} else if (bPendingMoveDest) {
		ButtonCol = FLinearColor(0.08f, 0.38f, 0.42f, 1.f);
	} else if (bPendingMoveOrigin) {
		ButtonCol = FLinearColor(0.42f, 0.28f, 0.06f, 1.f);
	} else if (bMoveReach) {
		if (bObjectiveTerrain) {
			ButtonCol = FMath::Lerp(ObjectiveTerrainBaseColor(), FLinearColor(0.1f, 0.34f, 0.18f, 1.f), 0.35f);
		} else {
			ButtonCol = bVoidTerrain ? FLinearColor(0.05f, 0.08f, 0.16f, 1.f)
				: bDamagingTerrain ? FLinearColor(0.54f, 0.24f, 0.08f, 1.f)
				: bRoad	? FLinearColor(0.3f, 0.58f, 0.16f, 1.f)
				: bRough		? FLinearColor(0.16f, 0.5f, 0.2f, 1.f)
								: FLinearColor(0.1f, 0.34f, 0.18f, 1.f);
		}
	} else if (bDeployValid) {
		ButtonCol = bVoidTerrain ? FLinearColor(0.22f, 0.16f, 0.04f, 1.f)
			: bDamagingTerrain ? FLinearColor(0.48f, 0.32f, 0.08f, 1.f)
			: bRoad	? FLinearColor(0.42f, 0.34f, 0.12f, 1.f)
			: bRough		? FLinearColor(0.28f, 0.38f, 0.1f, 1.f)
							: FLinearColor(0.38f, 0.28f, 0.08f, 1.f);
	}
	if (bResolveFlash && ResolveFlashAlpha > 0.01f) {
		ButtonCol = FMath::Lerp(ButtonCol, ResolveFlashColor, ResolveFlashAlpha * 0.88f);
		BorderCol = FMath::Lerp(BorderCol, ResolveFlashColor, FMath::Min(1.f, ResolveFlashAlpha * 1.1f));
	}

	TSharedRef<SWidget> CellBody = SNullWidget::NullWidget;
	if (bShowLabel) {
		CellBody = SNew(STextBlock)
					   .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 11))
					   .ColorAndOpacity(kCardTextWhite)
					   .Justification(ETextJustify::Center)
					   .Text(FText::FromString(Label));
	}

	if (TurnOrderRank > 0) {
		TSharedRef<SWidget> InnerBody = CellBody;
		CellBody = SNew(SOverlay)
					   + SOverlay::Slot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[InnerBody]
					   + SOverlay::Slot()
							 .HAlign(HAlign_Center)
							 .VAlign(VAlign_Top)
							 .Padding(0.f, 2.f, 0.f, 0.f)
							 [SNew(STextBlock)
								  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 13))
								  .ColorAndOpacity(FLinearColor(0.25f, 0.55f, 1.f, 1.f))
								  .Justification(ETextJustify::Center)
								  .Text(FText::AsNumber(TurnOrderRank))];
	}

	const TSharedRef<SWidget> CellButton = SNew(SButton)
												.ButtonStyle(&BoardCellButtonStyle())
												.ButtonColorAndOpacity(ButtonCol)
												.ForegroundColor(kCardTextWhite)
												.ContentPadding(FMargin(2.f))
												.HAlign(HAlign_Fill)
												.VAlign(VAlign_Fill)
												.OnClicked_Lambda([this, WorldX, WorldY]() { return OnCellClicked(WorldX, WorldY); })
												.OnHovered_Lambda([this, WorldX, WorldY]() { HandleWorldGridCellHovered(WorldX, WorldY); })
												.OnUnhovered_Lambda([this]() { ClearBoardCellHover(); })
												.RenderTransform_Lambda([ResolveFlashScale]() {
													const float S = ResolveFlashScale > 0.01f ? ResolveFlashScale : 1.f;
													return FSlateRenderTransform(FScale2D(S, S));
												})
												.RenderTransformPivot(FVector2D(0.5f, 0.5f))
												[CellBody];

	const TSharedRef<SWidget> Inner = SNew(SBox).WidthOverride(kCellInnerW).HeightOverride(kCellInnerH)[CellButton];

	return SNew(SBox).WidthOverride(kCellW).HeightOverride(kCellH)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder).BorderBackgroundColor(BorderCol).Padding(FMargin(1.f))[Inner]
		];
}

/** Draws 2D unit portraits on the Slate grid. Skipped when the 3D board is in use. */
void STacticsBoardPanel::RebuildBoardUnitArtOverlay()
{
	if (Subsystem.IsValid() && Subsystem->Uses3DBoardTiles()) {
		return;
	}
	if (!BoardUnitArtOverlay.IsValid() || !BoardGridSizer.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	BoardUnitArtOverlay->ClearChildren();

	int32 MinX = 0;
	int32 MinY = 0;
	int32 W = 0;
	int32 H = 0;
	if (!Subsystem->GetMergedBounds(MinX, MinY, W, H) || W < 1 || H < 1) {
		return;
	}

	const int32 MaxY = MinY + H - 1;
	const float StepX = kCellW + kCellGap;
	const float StepY = kCellH + kCellGap;
	const float TotalW = static_cast<float>(W) * StepX - kCellGap;
	const float TotalH = static_cast<float>(H) * StepY - kCellGap;
	BoardGridSizer->SetWidthOverride(TotalW);
	BoardGridSizer->SetHeightOverride(TotalH);

	TArray<FTacticsBoardUnitPose> Poses;
	Subsystem->GatherBoardUnitPoses(Poses);
	for (const FTacticsBoardUnitPose& Pose : Poses) {
		if (Pose.bIsBase || Pose.ArtId.IsEmpty()) {
			continue;
		}
		const int32 SpanX = FMath::Max(1, Pose.FootprintSpanX);
		const int32 SpanY = FMath::Max(1, Pose.FootprintSpanY);
		const float ArtW = static_cast<float>(SpanX) * StepX - kCellGap;
		const float ArtH = static_cast<float>(SpanY) * StepY - kCellGap;
		const FSlateBrush* Brush = TacticsCardArtUi::GetCardArtBrush(Pose.ArtId, FVector2D(ArtW, ArtH));
		if (!Brush) {
			continue;
		}
		const float CenterCx = Pose.GridCenterX - static_cast<float>(MinX);
		const float CenterCy = static_cast<float>(MaxY) - Pose.GridCenterY;
		const float PadL = CenterCx * StepX + kCellW * 0.5f - ArtW * 0.5f;
		const float PadT = CenterCy * StepY + kCellH * 0.5f - ArtH * 0.5f;
		BoardUnitArtOverlay->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(FMargin(PadL, PadT, 0.f, 0.f))
			[
				SNew(SBox).WidthOverride(ArtW).HeightOverride(ArtH)
					[
						SNew(SImage).Image(Brush)
					]
			];
	}
}

void STacticsBoardPanel::RebuildBoardDamagePopupOverlay()
{
	if (!BoardDamagePopupOverlay.IsValid() || !BoardGridSizer.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	if (Subsystem->Uses3DBoardTiles()) {
		BoardDamagePopupOverlay->ClearChildren();
		return;
	}
	BoardDamagePopupOverlay->ClearChildren();

	const TArray<FTacticsAbilityDamagePopup>& Popups = Subsystem->GetActiveAbilityDamagePopups();
	if (Popups.IsEmpty()) {
		return;
	}

	int32 MinX = 0;
	int32 MinY = 0;
	int32 W = 0;
	int32 H = 0;
	if (!Subsystem->GetMergedBounds(MinX, MinY, W, H) || W < 1 || H < 1) {
		return;
	}

	const int32 MaxY = MinY + H - 1;
	const float StepX = kCellW + kCellGap;
	const float StepY = kCellH + kCellGap;
	const float TotalW = static_cast<float>(W) * StepX - kCellGap;
	const float TotalH = static_cast<float>(H) * StepY - kCellGap;
	BoardGridSizer->SetWidthOverride(TotalW);
	BoardGridSizer->SetHeightOverride(TotalH);

	TArray<FTacticsBoardUnitPose> Poses;
	Subsystem->GatherBoardUnitPoses(Poses);
	const double Now = FPlatformTime::Seconds();

	for (const FTacticsAbilityDamagePopup& Popup : Popups) {
		float Alpha = 0.f;
		float Scale = 0.f;
		float OffsetY = 0.f;
		Popup.GetVisual(Now, Alpha, Scale, OffsetY);
		if (Alpha <= 0.01f) {
			continue;
		}

		float GridCenterX = static_cast<float>(Popup.Cell.X);
		float GridCenterY = static_cast<float>(Popup.Cell.Y);
		int32 SpanX = 1;
		int32 SpanY = 1;
		if (!Popup.EntityId.IsEmpty()) {
			for (const FTacticsBoardUnitPose& Pose : Poses) {
				if (Pose.EntityId == Popup.EntityId) {
					GridCenterX = Pose.GridCenterX;
					GridCenterY = Pose.GridCenterY;
					SpanX = FMath::Max(1, Pose.FootprintSpanX);
					SpanY = FMath::Max(1, Pose.FootprintSpanY);
					break;
				}
			}
		}

		const float ArtH = static_cast<float>(SpanY) * StepY - kCellGap;
		const float CenterCx = GridCenterX - static_cast<float>(MinX);
		const float CenterCy = static_cast<float>(MaxY) - GridCenterY;
		const float PopCenterX = CenterCx * StepX + kCellW * 0.5f;
		const float PopCenterY = FMath::Max(4.f, CenterCy * StepY + kCellH * 0.5f - ArtH * 0.55f + OffsetY);
		const FString Text = Popup.GetDisplayText();
		const FLinearColor Color = Popup.GetDisplayColor(Alpha);
		const FLinearColor ShadowColor = FLinearColor(0.f, 0.f, 0.f, Alpha * 0.85f);
		const float FontSize = Popup.GetSlateFontSize();
		const float BoxHalfWidth = FMath::Clamp(static_cast<float>(Text.Len()) * 4.5f, 48.f, 120.f);

		BoardDamagePopupOverlay->AddSlot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(FMargin(PopCenterX - BoxHalfWidth, PopCenterY, 0.f, 0.f))
			[
				SNew(SBox)
					.WidthOverride(BoxHalfWidth * 2.f)
					.HAlign(HAlign_Center)
					.VAlign(VAlign_Center)
					[
						SNew(SOverlay)
							+ SOverlay::Slot()
								  .HAlign(HAlign_Center)
								  .VAlign(VAlign_Center)
								  .Padding(FMargin(1.f, 1.f, 0.f, 0.f))
								  [
									  SNew(STextBlock)
										  .Font(TacticsCardText::DesignFont(TEXT("Bold"), FontSize))
										  .ColorAndOpacity(FSlateColor(ShadowColor))
										  .Justification(ETextJustify::Center)
										  .RenderTransform(FSlateRenderTransform(FScale2D(Scale, Scale)))
										  .RenderTransformPivot(FVector2D(0.5f, 0.5f))
										  .Text(FText::FromString(Text))
								  ]
							+ SOverlay::Slot()
								  .HAlign(HAlign_Center)
								  .VAlign(VAlign_Center)
								  [
									  SNew(STextBlock)
										  .Font(TacticsCardText::DesignFont(TEXT("Bold"), FontSize))
										  .ColorAndOpacity(FSlateColor(Color))
										  .Justification(ETextJustify::Center)
										  .RenderTransform(FSlateRenderTransform(FScale2D(Scale, Scale)))
										  .RenderTransformPivot(FVector2D(0.5f, 0.5f))
										  .Text(FText::FromString(Text))
								  ]
					]
			];
	}
}

void STacticsBoardPanel::RefreshAbilityDamagePopups()
{
	if (bMatchUiHidden || !Subsystem.IsValid() || !Subsystem->IsMatchReady()) {
		return;
	}
	if (Subsystem->Uses3DBoardTiles()) {
		if (BoardDamagePopupOverlay.IsValid()) {
			BoardDamagePopupOverlay->ClearChildren();
		}
		return;
	}
	RebuildBoardDamagePopupOverlay();
	Invalidate(EInvalidateWidget::Paint);
}


void STacticsBoardPanel::EnsureBoardGridBuilt()
{
	if (!BoardCellsOverlay.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	if (Subsystem->Uses3DBoardTiles()) {
		return;
	}
	int32 MinX = 0, MinY = 0, W = 0, H = 0;
	if (!Subsystem->GetMergedBounds(MinX, MinY, W, H) || W < 1 || H < 1) {
		return;
	}
	const int32 MaxY = MinY + H - 1;
	if (BoardCellWidgets.Num() > 0 && CachedGridMinX == MinX && CachedGridMaxY == MaxY && CachedGridW == W && CachedGridH == H) {
		return;
	}
	BoardCellWidgets.Empty();
	BoardCellsOverlay->ClearChildren();
	CachedGridMinX = MinX;
	CachedGridMaxY = MaxY;
	CachedGridW = W;
	CachedGridH = H;
	RebuildBoardGrid2D();
	for (int32 Cy = 0; Cy < H; ++Cy) {
		for (int32 Cx = 0; Cx < W; ++Cx) {
			const int32 WorldX = MinX + Cx;
			const int32 WorldY = MaxY - Cy;
			FBoardCellWidgetCache Cache;
			Cache.Widget = BuildCellButton(WorldX, WorldY);
			const FTacticsBoardCellPresentation Cell = Subsystem->GetCellPresentationAtWorld(WorldX, WorldY);
			const bool bDeployValid = Subsystem->IsDeployPreviewArmed()
				&& (bHandArmedForTile || bReservesArmedForTile)
				&& Subsystem->IsDeployValidCellAtWorld(WorldX, WorldY);
			const bool bAoEHoverPreview = Subsystem->IsBoardTargetAoEHoverPreviewActive();
			Cache.PresentationHash = HashCellPresentation(Cell, bDeployValid, bAoEHoverPreview);
			BoardCellWidgets.Add(FIntPoint(WorldX, WorldY), Cache);
		}
	}
}

void STacticsBoardPanel::RefreshBoardGrid2DPresentations()
{
	if (!BoardCellsOverlay.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	if (Subsystem->Uses3DBoardTiles()) {
		BoardCellsOverlay->ClearChildren();
		return;
	}
	EnsureBoardGridBuilt();
	BoardCellsOverlay->ClearChildren();
	int32 MinX = 0, MinY = 0, W = 0, H = 0;
	if (!Subsystem->GetMergedBounds(MinX, MinY, W, H) || W < 1 || H < 1) {
		return;
	}
	const int32 MaxY = MinY + H - 1;
	const float StepX = kCellW + kCellGap;
	const float StepY = kCellH + kCellGap;
	const float TotalW = static_cast<float>(W) * StepX - kCellGap;
	const float TotalH = static_cast<float>(H) * StepY - kCellGap;
	if (BoardGridSizer.IsValid()) {
		BoardGridSizer->SetWidthOverride(TotalW);
		BoardGridSizer->SetHeightOverride(TotalH);
	}
	const bool bAoEHoverPreview = Subsystem->IsBoardTargetAoEHoverPreviewActive();
	for (int32 Cy = 0; Cy < H; ++Cy) {
		for (int32 Cx = 0; Cx < W; ++Cx) {
			const int32 WorldX = MinX + Cx;
			const int32 WorldY = MaxY - Cy;
			if (!Subsystem->HasBoardCellAtWorld(WorldX, WorldY)) {
				continue;
			}
			const FTacticsBoardCellPresentation Cell = Subsystem->GetCellPresentationAtWorld(WorldX, WorldY);
			const bool bDeployValid = Subsystem->IsDeployPreviewArmed()
				&& (bHandArmedForTile || bReservesArmedForTile)
				&& Subsystem->IsDeployValidCellAtWorld(WorldX, WorldY);
			const uint32 NextHash = HashCellPresentation(Cell, bDeployValid, bAoEHoverPreview);
			FBoardCellWidgetCache* Cache = BoardCellWidgets.Find(FIntPoint(WorldX, WorldY));
			if (!Cache) {
				FBoardCellWidgetCache NewCache;
				NewCache.Widget = BuildCellButton(WorldX, WorldY);
				NewCache.PresentationHash = NextHash;
				BoardCellWidgets.Add(FIntPoint(WorldX, WorldY), NewCache);
				Cache = BoardCellWidgets.Find(FIntPoint(WorldX, WorldY));
			} else if (!Cache->Widget.IsValid() || Cache->PresentationHash != NextHash) {
				Cache->Widget = BuildCellButton(WorldX, WorldY);
				Cache->PresentationHash = NextHash;
			}
			const float PadL = static_cast<float>(Cx) * StepX;
			const float PadT = static_cast<float>(Cy) * StepY;
			BoardCellsOverlay->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Top)
				.Padding(FMargin(PadL, PadT, 0.f, 0.f))
				[
					Cache->Widget.ToSharedRef()
				];
		}
	}
}

void STacticsBoardPanel::RebuildBoardGrid2D()
{
	if (!BoardCellsOverlay.IsValid() || !BoardGridSizer.IsValid() || !Subsystem.IsValid()) {
		return;
	}

	int MinX = 0, MinY = 0, W = 0, H = 0;
	if (!Subsystem->GetMergedBounds(MinX, MinY, W, H) || W < 1 || H < 1) {
		return;
	}

	const int MaxY = MinY + H - 1;
	const float StepX = kCellW + kCellGap;
	const float StepY = kCellH + kCellGap;
	const float TotalW = static_cast<float>(W) * StepX - kCellGap;
	const float TotalH = static_cast<float>(H) * StepY - kCellGap;
	BoardGridSizer->SetWidthOverride(TotalW);
	BoardGridSizer->SetHeightOverride(TotalH);

	for (int Cy = 0; Cy < H; ++Cy) {
		for (int Cx = 0; Cx < W; ++Cx) {
			const int WorldX = MinX + Cx;
			const int WorldY = MaxY - Cy;
			if (!Subsystem->HasBoardCellAtWorld(WorldX, WorldY)) {
				continue;
			}
			const float PadL = static_cast<float>(Cx) * StepX;
			const float PadT = static_cast<float>(Cy) * StepY;
			BoardCellsOverlay->AddSlot()
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Top)
				.Padding(FMargin(PadL, PadT, 0.f, 0.f))
				[
					BuildCellButton(WorldX, WorldY)
				];
		}
	}
}

bool STacticsBoardPanel::WantsDirectionalAreaHoverPreview() const
{
	if (!Subsystem.IsValid()) {
		return false;
	}
	if (!Subsystem->IsBoardTargetAoEHoverPreviewActive()) {
		return false;
	}
	return bAbilityArmedForTile || bHandArmedForTile || bReservesArmedForTile;
}

void STacticsBoardPanel::HandleWorldGridCellHovered(int WorldX, int WorldY)
{
	HandleBoardCellHover(WorldX, WorldY);
}

void STacticsBoardPanel::InspectBoardUnitAtWorld(int WorldX, int WorldY)
{
	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady() || !Subsystem->HasUnitAtWorld(WorldX, WorldY)) {
		return;
	}
	bDetailPanelShowsHandCard = false;
	SelectedTerritoryRepIndex = -1;
	FString C, R;
	if (!Subsystem->GetCliCellTokens(WorldX, WorldY, C, R)) {
		LastCliOutput = TEXT("That cell is outside the CLI grid.");
		RefreshStatusText();
		Refresh();
		return;
	}
	if (CanControlledSeatTakeActions() && Subsystem->IsAnyMainPhase() && Subsystem->HasControllableUnitAtWorld(WorldX, WorldY)) {
		RunCli(FString::Printf(TEXT("select %s %s"), *C, *R));
		return;
	}
	FString SelectMessage;
	Subsystem->TrySelectWorld(WorldX, WorldY, SelectMessage);
	LastCliOutput = SelectMessage;
	RefreshStatusText();
	Refresh();
}

void STacticsBoardPanel::HandleWorldGridCellClicked(int WorldX, int WorldY, const bool bShiftHeld)
{
	if (Subsystem.IsValid() && Subsystem->IsMatchReady() && !CanControlledSeatTakeActions()
		&& !Subsystem->IsAwaitingHandDiscard() && !Subsystem->IsAwaitingScan() && !Subsystem->IsAwaitingTerritoryLoot()
		&& !Subsystem->IsControlledPlayerAwaitingTerritoryTarget()) {
		if (Subsystem->HasUnitAtWorld(WorldX, WorldY)) {
			InspectBoardUnitAtWorld(WorldX, WorldY);
			return;
		}
		ShowFailureAlert(TEXT("Not your turn - switch Control seat or wait for priority."));
		return;
	}

	if (Subsystem.IsValid()) {
		Subsystem->SetPendingCliWorldCell(WorldX, WorldY);
	}

	// Conquering Territories: a placed territory's enter/groundwork effect is waiting for a target
	// unit - this click picks it.
	if (Subsystem.IsValid() && Subsystem->IsControlledPlayerAwaitingTerritoryTarget()) {
		FString C, R;
		if (Subsystem->GetCliCellTokens(WorldX, WorldY, C, R)) {
			RunCli(FString::Printf(TEXT("land_target %s %s"), *C, *R));
			return;
		}
		LastCliOutput = TEXT("Territory target: click a valid board cell.");
		RefreshStatusText();
		Refresh();
		return;
	}

	// Conquering Territories: a targeted "use land" ability was armed - this click is its target.
	if (bLandAbilityArmedForTile && Subsystem.IsValid() && Subsystem->IsMatchReady()) {
		FString C, R;
		if (Subsystem->GetCliCellTokens(WorldX, WorldY, C, R)) {
			const int32 T = ArmedLandTerritoryIndex;
			const int32 A = ArmedLandAbilityIndex;
			bLandAbilityArmedForTile = false;
			ArmedLandTerritoryIndex = -1;
			ArmedLandAbilityIndex = -1;
			RunCli(FString::Printf(TEXT("use_land %d %d %s %s"), T + 1, A + 1, *C, *R));
			return;
		}
		LastCliOutput = TEXT("Use Land: click a valid board cell to target.");
		RefreshStatusText();
		Refresh();
		return;
	}

	if (bAbilityArmedForTile && Subsystem.IsValid() && Subsystem->IsMatchReady() && !Subsystem->IsAwaitingHandDiscard() && !Subsystem->IsAwaitingScan() && !Subsystem->IsAwaitingTerritoryLoot()) {
		bool bRequiresStack = false;
		if (Subsystem->TryGetSelectedAbilityRequiresStackTarget(ArmedAbilityKey, bRequiresStack) && bRequiresStack) {
			LastCliOutput = TEXT("Counter target: click a highlighted batch queue entry on the right.");
			RefreshStatusText();
			Refresh();
			return;
		}
		if (!Subsystem->IsBoardTargetOtherHighlightAtWorld(WorldX, WorldY)
			&& !Subsystem->IsBoardTargetEnemyHighlightAtWorld(WorldX, WorldY)) {
			LastCliOutput = TEXT("Ability: click a highlighted cell to target.");
			RefreshStatusText();
			Refresh();
			return;
		}
		FString C, R;
		if (Subsystem->GetCliCellTokens(WorldX, WorldY, C, R)) {
			const FString KeyCopy = ArmedAbilityKey;
			bAbilityArmedForTile = false;
			ArmedAbilityKey.Empty();
			HideAbilityDescription();
			if (IsArmedXCostAbility() && ArmedXCostAmount <= 0) {
				LastCliOutput = FString::Printf(TEXT("Need more %s energy for minimum X=%d."),
					*ArmedXCostEnergyType, ArmedXCostMin);
				RefreshStatusText();
				Refresh();
				return;
			}
			RunAbilityCli(AppendArmedXCostCliSuffix(FString::Printf(TEXT("ability %s %s %s"), *KeyCopy, *C, *R)), KeyCopy,
				{FIntPoint(WorldX, WorldY)});
			return;
		}
		LastCliOutput = TEXT("That cell is outside the CLI grid for ability targeting.");
		RefreshStatusText();
		Refresh();
		return;
	}

	if ((bHandArmedForTile || bReservesArmedForTile) && bFocusSpellAwaitingCaster && Subsystem.IsValid() && Subsystem->IsMatchReady()
		&& !Subsystem->IsAwaitingHandDiscard() && !Subsystem->IsAwaitingScan() && !Subsystem->IsAwaitingTerritoryLoot()) {
		if (Subsystem->HasControllableUnitAtWorld(WorldX, WorldY)) {
			FString SelectMessage;
			if (!Subsystem->TrySelectWorld(WorldX, WorldY, SelectMessage)) {
				LastCliOutput = SelectMessage;
				RefreshStatusText();
				Refresh();
				return;
			}
			const bool bPreviewOk = bReservesArmedForTile
				? Subsystem->SetBoardTargetPreviewForReservesCard(DeployReservesIndex1Based)
				: Subsystem->SetBoardTargetPreviewForHandCard(DeployHandIndex1Based);
			if (!bPreviewOk) {
				LastCliOutput = TEXT("That unit cannot cast this Focus spell.");
				RefreshStatusText();
				Refresh();
				return;
			}
			bFocusSpellAwaitingCaster = false;
			LastCliOutput = IsArmedDirectionalFocusSpell()
				? TEXT("Caster selected - click a highlighted direction cell (blue); orange shows the helix footprint.")
				: TEXT("Caster selected - click a highlighted enemy in range and line of sight.");
			RefreshStatusText();
			Refresh();
			return;
		}
		LastCliOutput = TEXT("Focus spell: click a highlighted unit to choose the caster.");
		RefreshStatusText();
		Refresh();
		return;
	}

	if (bReservesArmedForTile && Subsystem.IsValid() && Subsystem->IsMatchReady() && !Subsystem->IsAwaitingHandDiscard() && !Subsystem->IsAwaitingScan() && !Subsystem->IsAwaitingTerritoryLoot()) {
		if (!Subsystem->CanControlledPlayerActInMainPhase()) {
			LastCliOutput = TEXT("Reserves can only be played on your turn.");
			ClearPlayArmingState();
			RefreshStatusText();
			Refresh();
			return;
		}
		const int32 Rn = Subsystem->GetControlledReservesCount();
		if (Rn >= 1) {
			DeployReservesIndex1Based = FMath::Clamp(DeployReservesIndex1Based, 1, Rn);
			FString Name, CardKind, Cost, Rules;
			if (Subsystem->TryGetReservesCardUi(DeployReservesIndex1Based, Name, CardKind, Cost, Rules)) {
				if (CardKind.Equals(TEXT("unit"), ESearchCase::IgnoreCase) || CardKind.Equals(TEXT("building"), ESearchCase::IgnoreCase)
					|| CardKind.Equals(TEXT("obstacle"), ESearchCase::IgnoreCase)) {
					if (Subsystem->HasUnitAtWorld(WorldX, WorldY)) {
						InspectBoardUnitAtWorld(WorldX, WorldY);
						return;
					}
					FString C, R;
					if (Subsystem->GetCliCellTokens(WorldX, WorldY, C, R)) {
						ClearPlayArmingState();
						RunCli(FString::Printf(TEXT("deploy_reserve %d %s %s"), DeployReservesIndex1Based, *C, *R));
						return;
					}
					LastCliOutput = TEXT("That cell is outside the CLI grid for deploy.");
					ClearPlayArmingState();
					RefreshStatusText();
					Refresh();
					return;
				}
				if (CardKind.Equals(TEXT("spell"), ESearchCase::IgnoreCase)) {
					if (IsArmedXCostSpell() && ArmedXCostAmount <= 0) {
						LastCliOutput = FString::Printf(
							TEXT("Not enough %s energy to pay the minimum X (%d) for this spell."),
							*ArmedXCostEnergyType,
							ArmedXCostMin);
						RefreshStatusText();
						Refresh();
						return;
					}
					bool bRequiresStack = false;
					if (Subsystem->TryGetReservesSpellRequiresStackTarget(DeployReservesIndex1Based, bRequiresStack) && bRequiresStack) {
						LastCliOutput = TEXT("Counter target: click a highlighted batch queue entry on the right.");
						RefreshStatusText();
						Refresh();
						return;
					}
					bool bRequiresCell = true;
					const bool bSpellMeta = Subsystem->TryGetReservesSpellRequiresBoardCell(DeployReservesIndex1Based, bRequiresCell);
					if (bSpellMeta && !bRequiresCell) {
						RunArmedCastCli(FString::Printf(TEXT("cast_reserve %d"), DeployReservesIndex1Based));
						return;
					}
					if (IsArmedFocusSpell()) {
						if (bFocusSpellAwaitingCaster || !Subsystem->HasUnitSelected() || !Subsystem->IsSelectedUnitControlled()) {
							LastCliOutput = TEXT("Select a unit with Insatiable Focus to cast this damaging spell first.");
							RefreshStatusText();
							Refresh();
							return;
						}
					}
					if (IsArmedPushDirectionSpell()) {
						FString C, R;
						if (!Subsystem->GetCliCellTokens(WorldX, WorldY, C, R)) {
							LastCliOutput = TEXT("That cell is outside the CLI grid for cast.");
							ClearPlayArmingState();
							RefreshStatusText();
							Refresh();
							return;
						}
						if (bPushSpellAwaitingUnit) {
							if (!Subsystem->IsBoardTargetEnemyHighlightAtWorld(WorldX, WorldY)
									&& !Subsystem->IsBoardTargetOtherHighlightAtWorld(WorldX, WorldY)) {
								LastCliOutput = TEXT("Push spell: click a highlighted unit to push.");
								RefreshStatusText();
								Refresh();
								return;
							}
							if (!Subsystem->SetBoardPushDirectionPreviewForReservesCard(DeployReservesIndex1Based, WorldX, WorldY)) {
								LastCliOutput = TEXT("That unit is not a legal push target.");
								RefreshStatusText();
								Refresh();
								return;
							}
							bPushSpellAwaitingUnit = false;
							PushSpellTargetWorldX = WorldX;
							PushSpellTargetWorldY = WorldY;
							LastCliOutput = TEXT("Unit selected - click a highlighted cardinal direction (orange shows push path).");
							RefreshStatusText();
							Refresh();
							return;
						}
						if (!Subsystem->IsBoardTargetOtherHighlightAtWorld(WorldX, WorldY)) {
							LastCliOutput = TEXT("Push spell: click a highlighted direction cell adjacent to the target.");
							RefreshStatusText();
							Refresh();
							return;
						}
						FString TC, TR;
						if (!Subsystem->GetCliCellTokens(PushSpellTargetWorldX, PushSpellTargetWorldY, TC, TR)) {
							LastCliOutput = TEXT("Push target cell is no longer valid.");
							ClearPlayArmingState();
							RefreshStatusText();
							Refresh();
							return;
						}
						RunArmedCastCli(FString::Printf(
							TEXT("cast_reserve %d %s %s %s %s"), DeployReservesIndex1Based, *TC, *TR, *C, *R));
						return;
					}
					FString C, R;
					if (Subsystem->GetCliCellTokens(WorldX, WorldY, C, R)) {
						if (TryHandleMulticastCellPick(C, R, true)) {
							return;
						}
						RunArmedCastCli(FString::Printf(TEXT("cast_reserve %d %s %s"), DeployReservesIndex1Based, *C, *R));
						return;
					}
					LastCliOutput = TEXT("That cell is outside the CLI grid for cast.");
					ClearPlayArmingState();
					RefreshStatusText();
					Refresh();
					return;
				}
			}
		}
		LastCliOutput = TEXT("Armed reserves card is not playable on a tile.");
		ClearPlayArmingState();
	}

	if (bHandArmedForTile && bModalSpellPickerActive && Subsystem.IsValid() && Subsystem->IsMatchReady()) {
		return;
	}
	if (bHandArmedForTile && Subsystem.IsValid() && Subsystem->IsMatchReady() && !Subsystem->IsAwaitingHandDiscard() && !Subsystem->IsAwaitingScan() && !Subsystem->IsAwaitingTerritoryLoot()) {
		const int32 N = Subsystem->GetControlledHandCount();
		if (N >= 1) {
			DeployHandIndex1Based = FMath::Clamp(DeployHandIndex1Based, 1, N);
			FString Name, CardKind, Cost, Rules;
			if (Subsystem->TryGetHandCardUi(DeployHandIndex1Based, Name, CardKind, Cost, Rules)) {
				if (CardKind.Equals(TEXT("unit"), ESearchCase::IgnoreCase) || CardKind.Equals(TEXT("building"), ESearchCase::IgnoreCase)
					|| CardKind.Equals(TEXT("obstacle"), ESearchCase::IgnoreCase)) {
					if (Subsystem->HasUnitAtWorld(WorldX, WorldY)) {
						InspectBoardUnitAtWorld(WorldX, WorldY);
						return;
					}
					FString C, R;
					if (Subsystem->GetCliCellTokens(WorldX, WorldY, C, R)) {
						ClearPlayArmingState();
						RunCli(FString::Printf(TEXT("deploy %d %s %s"), DeployHandIndex1Based, *C, *R));
						return;
					}
					LastCliOutput = TEXT("That cell is outside the CLI grid for deploy.");
					ClearPlayArmingState();
					RefreshStatusText();
					Refresh();
					return;
				}
				if (CardKind.Equals(TEXT("spell"), ESearchCase::IgnoreCase)) {
					if (IsArmedXCostSpell() && ArmedXCostAmount <= 0) {
						LastCliOutput = FString::Printf(
							TEXT("Not enough %s energy to pay the minimum X (%d) for this spell."),
							*ArmedXCostEnergyType,
							ArmedXCostMin);
						RefreshStatusText();
						Refresh();
						return;
					}
					bool bRequiresStack = false;
					if (Subsystem->TryGetHandSpellRequiresStackTarget(DeployHandIndex1Based, bRequiresStack) && bRequiresStack) {
						LastCliOutput = TEXT("Counter target: click a highlighted batch queue entry on the right.");
						RefreshStatusText();
						Refresh();
						return;
					}
					bool bRequiresCell = true;
					const bool bSpellMeta = GetArmedSpellRequiresBoardCell(bRequiresCell);
					if (bSpellMeta && !bRequiresCell) {
						RunArmedCastCli(FString::Printf(TEXT("cast %d"), DeployHandIndex1Based));
						return;
					}
					if (IsArmedFocusSpell()) {
						if (bFocusSpellAwaitingCaster || !Subsystem->HasUnitSelected() || !Subsystem->IsSelectedUnitControlled()) {
							LastCliOutput = TEXT("Select one of your units to cast this spell first.");
							RefreshStatusText();
							Refresh();
							return;
						}
					}
					if (IsArmedPushDirectionSpell()) {
						FString C, R;
						if (!Subsystem->GetCliCellTokens(WorldX, WorldY, C, R)) {
							LastCliOutput = TEXT("That cell is outside the CLI grid for cast.");
							ClearPlayArmingState();
							RefreshStatusText();
							Refresh();
							return;
						}
						if (bPushSpellAwaitingUnit) {
							if (!Subsystem->IsBoardTargetEnemyHighlightAtWorld(WorldX, WorldY)
									&& !Subsystem->IsBoardTargetOtherHighlightAtWorld(WorldX, WorldY)) {
								LastCliOutput = TEXT("Push spell: click a highlighted unit to push.");
								RefreshStatusText();
								Refresh();
								return;
							}
							if (!Subsystem->SetBoardPushDirectionPreviewForHandCard(DeployHandIndex1Based, WorldX, WorldY)) {
								LastCliOutput = TEXT("That unit is not a legal push target.");
								RefreshStatusText();
								Refresh();
								return;
							}
							bPushSpellAwaitingUnit = false;
							PushSpellTargetWorldX = WorldX;
							PushSpellTargetWorldY = WorldY;
							LastCliOutput = TEXT("Unit selected - click a highlighted cardinal direction (orange shows push path).");
							RefreshStatusText();
							Refresh();
							return;
						}
						if (!Subsystem->IsBoardTargetOtherHighlightAtWorld(WorldX, WorldY)) {
							LastCliOutput = TEXT("Push spell: click a highlighted direction cell adjacent to the target.");
							RefreshStatusText();
							Refresh();
							return;
						}
						FString TC, TR;
						if (!Subsystem->GetCliCellTokens(PushSpellTargetWorldX, PushSpellTargetWorldY, TC, TR)) {
							LastCliOutput = TEXT("Push target cell is no longer valid.");
							ClearPlayArmingState();
							RefreshStatusText();
							Refresh();
							return;
						}
						RunArmedCastCli(FString::Printf(
							TEXT("cast %d %s %s %s %s"), DeployHandIndex1Based, *TC, *TR, *C, *R));
						return;
					}
					FString C, R;
					if (Subsystem->GetCliCellTokens(WorldX, WorldY, C, R)) {
						if (TryHandleMulticastCellPick(C, R, false)) {
							return;
						}
						RunArmedCastCli(FString::Printf(TEXT("cast %d %s %s"), DeployHandIndex1Based, *C, *R));
						return;
					}
					LastCliOutput = TEXT("That cell is outside the CLI grid for cast.");
					ClearPlayArmingState();
					RefreshStatusText();
					Refresh();
					return;
				}
			}
		}
		LastCliOutput = TEXT("Armed card is not playable on a tile - pick a unit, building, or spell in hand.");
		ClearPlayArmingState();
	}

	if (bShiftHeld && DispatchMovePreviewForWorldCell(WorldX, WorldY)) {
		return;
	}

	if (TryDispatchAttackAtWorldCell(WorldX, WorldY)) {
		return;
	}

	if (Subsystem.IsValid() && Subsystem->IsMatchReady() && !Subsystem->IsAwaitingHandDiscard() && !Subsystem->IsAwaitingScan() && !Subsystem->IsAwaitingTerritoryLoot()
		&& Subsystem->HasUnitAtWorld(WorldX, WorldY)) {
		InspectBoardUnitAtWorld(WorldX, WorldY);
		return;
	}

	if (DispatchMovePreviewForWorldCell(WorldX, WorldY)) {
		return;
	}

	if (Subsystem.IsValid() && !bAbilityArmedForTile && !bLandAbilityArmedForTile && !bHandArmedForTile
		&& !bReservesArmedForTile) {
		bDetailPanelShowsHandCard = false;
		FString Ignored;
		Subsystem->TrySelectWorld(WorldX, WorldY, Ignored);
	}

	RefreshStatusText();
	Refresh();
}

void STacticsBoardPanel::HandleWorldGridCellSecondaryClicked(int WorldX, int WorldY)
{
	if (Subsystem.IsValid() && Subsystem->IsMatchReady() && !CanControlledSeatTakeActions()
		&& !Subsystem->IsAwaitingHandDiscard() && !Subsystem->IsAwaitingScan() && !Subsystem->IsAwaitingTerritoryLoot()) {
		ShowFailureAlert(TEXT("Not your turn - switch Control seat or wait for priority."));
		return;
	}
	if (TryDispatchAttackAtWorldCell(WorldX, WorldY)) {
		return;
	}
	HandleShortcutKey(EKeys::RightMouseButton);
}

FReply STacticsBoardPanel::OnCellClicked(int WorldX, int WorldY)
{
	const bool bShift = FSlateApplication::Get().GetModifierKeys().IsShiftDown();
	HandleWorldGridCellClicked(WorldX, WorldY, bShift);
	return FReply::Handled();
}

void STacticsBoardPanel::RebuildHandBar()
{
	if (!HandScroll.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	HandScroll->ClearChildren();
	const bool bDiscard = Subsystem->IsAwaitingHandDiscard();
	const bool bTerritoryLoot = Subsystem->IsAwaitingTerritoryLoot();
	if (bDiscard || bTerritoryLoot) {
		ClearPlayArmingState();
	}
	const int32 N = Subsystem->GetControlledHandCount();
	if (N < 1) {
		return;
	}
	DeployHandIndex1Based = FMath::Clamp(DeployHandIndex1Based, 1, N);

	// Hand indices are 1-based and match deck.hand order (draw order).
	for (int32 Idx = 1; Idx <= N; ++Idx) {
		FString Name, CardKind, Cost, RulesUnused;
		if (!Subsystem->TryGetHandCardUi(Idx, Name, CardKind, Cost, RulesUnused)) {
			continue;
		}
		if (Name.Len() > 18) {
			Name = Name.Left(17) + TEXT("…");
		}
		const bool bPicked = (Idx == DeployHandIndex1Based);
		const bool bUnit = CardKind.Equals(TEXT("unit"), ESearchCase::IgnoreCase);
		const bool bObstacle = CardKind.Equals(TEXT("obstacle"), ESearchCase::IgnoreCase);
		FLinearColor Frame = bTerritoryLoot ? FLinearColor(0.38f, 0.28f, 0.10f, 1.f)
									  : bDiscard ? FLinearColor(0.42f, 0.14f, 0.12f, 1.f)
									  : bObstacle ? FLinearColor(0.38f, 0.30f, 0.22f, 1.f)
									  : (bUnit ? FLinearColor(0.48f, 0.32f, 0.14f, 1.f) : FLinearColor(0.12f, 0.28f, 0.52f, 1.f));
		if (bPicked) {
			Frame += FLinearColor(0.25f, 0.22f, 0.08f, 0.f);
		}
		if (bPicked && bHandArmedForTile && !bDiscard) {
			Frame += FLinearColor(0.35f, 0.55f, 0.2f, 0.f);
		}

		const FString TopKind = bTerritoryLoot ? TEXT("LOOT") : bDiscard ? TEXT("DISCARD") : CardKind.ToUpper();
		// Spells carry a speed (reflex/channeled/blazing) - show it as an inline icon next to the kind label.
		FString TopKindMarkup = TacticsCardText::MarkupEnergyTokens(TopKind);
		if (!bDiscard && CardKind.Equals(TEXT("spell"), ESearchCase::IgnoreCase)) {
			FString SpeedTag;
			if (Subsystem->TryGetHandSpellSpeedTag(Idx, SpeedTag) && !SpeedTag.IsEmpty()) {
				TopKindMarkup += TEXT(" ") + TacticsCardText::IconMarkup(SpeedTag);
			}
		}

		HandScroll->AddSlot()
			.Padding(FMargin(5.f, 0.f, 5.f, 0.f))
			[
				SNew(SBox).WidthOverride(152.f).HeightOverride(kHandStripCardHeight)
					[
						SNew(SBorder).BorderBackgroundColor(Frame).Padding(FMargin(2.f))
							[
								SNew(SButton)
									.ButtonColorAndOpacity(FLinearColor(0.08f, 0.08f, 0.1f, 1.f))
									.ForegroundColor(kCardTextWhite)
									.ContentPadding(FMargin(6.f))
									.OnClicked_Lambda([this, Idx, bDiscard]() {
										ActivateHandCardAtIndex(Idx);
										return FReply::Handled();
									})
									[
										SNew(SVerticalBox)
										+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
											  [
												  SNew(SBox)
													  .HeightOverride(kHandCardKindRowHeight)
													  .VAlign(VAlign_Center)
													  [
														  SNew(SRichTextBlock)
															  .TextStyle(&TacticsCardText::EnergyTextStyle(11, kCardTextWhite))
															  .DecoratorStyleSet(&FCoreStyle::Get())
															  .Decorators(TacticsCardText::MakeEnergyDecorators(11))
															  .Text(FText::FromString(TopKindMarkup))
													  ]
											  ]
										+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
											  [SNew(STextBlock)
												   .WrapTextAt(132.f)
												   .Font(TacticsCardText::DesignFont(TEXT("Bold"), 14))
												   .ColorAndOpacity(kCardTextWhite)
												   .Text(FText::FromString(Name))]
										+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Bottom)
											  [SNew(SRichTextBlock)
												   .WrapTextAt(132.f)
												   .TextStyle(&TacticsCardText::EnergyTextStyle(10, kCardTextWhite))
												   .DecoratorStyleSet(&FCoreStyle::Get())
												   .Decorators(TacticsCardText::MakeEnergyDecorators(10))
												   .Text(FText::FromString(TacticsCardText::MarkupEnergyTokens(Cost)))]
									]
							]
					]
			];
	}
}

void STacticsBoardPanel::RebuildEnergyCountersBar()
{
	if (!EnergyCountersBox.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	EnergyCountersBox->ClearChildren();

	TArray<FString> Labels;
	TArray<int32> FreeFloating;
	TArray<int32> TaggedFloating;
	TArray<int32> FromZones;
	Subsystem->GetControlledAvailableEnergyCounters(Labels, FreeFloating, TaggedFloating, FromZones);

	if (Labels.Num() == 0) {
		EnergyCountersBox->AddSlot().AutoHeight()
			[
				SNew(STextBlock)
					.Text(FText::FromString(TEXT("None")))
					.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 8))
					.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.f))
			];
		return;
	}

	auto ColorForType = [](const FString& Raw) -> FLinearColor {
		if (Raw.Equals(TEXT("red"), ESearchCase::IgnoreCase))        { return FLinearColor(0.92f, 0.32f, 0.30f, 1.f); }
		if (Raw.Equals(TEXT("turquoise"), ESearchCase::IgnoreCase))  { return FLinearColor(0.25f, 0.80f, 0.80f, 1.f); }
		if (Raw.Equals(TEXT("ingenuity"), ESearchCase::IgnoreCase))  { return FLinearColor(0.95f, 0.60f, 0.20f, 1.f); }
		if (Raw.Equals(TEXT("purple"), ESearchCase::IgnoreCase))     { return FLinearColor(0.72f, 0.45f, 0.95f, 1.f); }
		if (Raw.Equals(TEXT("gallantry"), ESearchCase::IgnoreCase))  { return FLinearColor(0.45f, 0.85f, 0.45f, 1.f); }
		if (Raw.Equals(TEXT("omni"), ESearchCase::IgnoreCase))       { return FLinearColor(0.95f, 0.92f, 0.55f, 1.f); }
		return FLinearColor(0.82f, 0.82f, 0.82f, 1.f); // neutral / unknown
	};

	auto DisplayName = [](const FString& Raw) -> FString {
		if (Raw.Equals(TEXT("ingenuity"), ESearchCase::IgnoreCase)) { return TEXT("Orange"); }
		if (Raw.Equals(TEXT("gallantry"), ESearchCase::IgnoreCase)) { return TEXT("Green"); }
		FString Out = Raw;
		if (Out.Len() > 0) {
			Out = Out.Left(1).ToUpper() + Out.Mid(1);
		}
		return Out;
	};

	for (int32 i = 0; i < Labels.Num(); ++i) {
		const int32 Fr = FreeFloating.IsValidIndex(i) ? FreeFloating[i] : 0;
		const int32 Tg = TaggedFloating.IsValidIndex(i) ? TaggedFloating[i] : 0;
		const int32 Zn = FromZones.IsValidIndex(i) ? FromZones[i] : 0;
		const int32 Total = Fr + Tg + Zn;
		if (Total <= 0) {
			continue;
		}
		const FLinearColor Col = ColorForType(Labels[i]);
		const FString Tooltip = FString::Printf(
			TEXT("%s energy:\n  %d free (any action)\n  %d restricted (spells/abilities only - from Tithe etc.)\n  %d from untapped territories"),
			*DisplayName(Labels[i]), Fr, Tg, Zn);

		EnergyCountersBox->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
			[
				SNew(SHorizontalBox)
					.ToolTipText(FText::FromString(Tooltip))
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock)
							.Text(FText::FromString(DisplayName(Labels[i])))
							.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
							.ColorAndOpacity(Col)
					]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(6.f, 0.f, 0.f, 0.f)
					[
						SNew(STextBlock)
							.Text(FText::FromString(FString::Printf(TEXT("%d"), Total)))
							.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 11))
							.ColorAndOpacity(kCardTextWhite)
					]
			];
	}

	if (EnergyCountersBox->NumSlots() == 0) {
		EnergyCountersBox->AddSlot().AutoHeight()
			[
				SNew(STextBlock)
					.Text(FText::FromString(TEXT("0")))
					.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
					.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.f))
			];
	}
}

void STacticsBoardPanel::RebuildReservesBar()
{
	if (!ReservesScroll.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	ReservesScroll->ClearChildren();
	const int32 N = Subsystem->GetControlledReservesCount();
	if (N < 1) {
		DeployReservesIndex1Based = 0;
		ReservesScroll->AddSlot()
			.Padding(FMargin(0.f, 0.f, 0.f, 5.f))
			[
				SNew(STextBlock)
					.Font(TacticsCardText::DefaultFont(TEXT("Italic"), 9))
					.WrapTextAt(kReservesColumnW - 16.f)
					.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.f))
					.Text(FText::FromString(TEXT("No reserves.")))
			];
		return;
	}
	DeployReservesIndex1Based = FMath::Clamp(DeployReservesIndex1Based, 1, N);

	// Sort reserves display order by total energy cost (cheapest first).
	TArray<int32> SortedReservesIndices;
	SortedReservesIndices.Reserve(N);
	for (int32 i = 1; i <= N; ++i) { SortedReservesIndices.Add(i); }
	SortedReservesIndices.StableSort([this](int32 A, int32 B) {
		return Subsystem->GetReservesCardTotalCost(A) < Subsystem->GetReservesCardTotalCost(B);
	});

	for (int32 Idx : SortedReservesIndices) {
		FString Name, CardKind, Cost, RulesUnused;
		if (!Subsystem->TryGetReservesCardUi(Idx, Name, CardKind, Cost, RulesUnused)) {
			continue;
		}
		if (Name.Len() > 14) {
			Name = Name.Left(13) + TEXT("…");
		}
		const bool bPicked = (Idx == DeployReservesIndex1Based);
		const bool bUnit = CardKind.Equals(TEXT("unit"), ESearchCase::IgnoreCase);
		FLinearColor Frame(0.22f, 0.18f, 0.10f, 1.f);
		if (bUnit) {
			Frame = FLinearColor(0.36f, 0.26f, 0.10f, 1.f);
		} else {
			Frame = FLinearColor(0.14f, 0.22f, 0.38f, 1.f);
		}
		if (bPicked) {
			Frame += FLinearColor(0.2f, 0.18f, 0.06f, 0.f);
		}
		if (bPicked && bReservesArmedForTile) {
			Frame += FLinearColor(0.35f, 0.55f, 0.2f, 0.f);
		}

		// Spells carry a speed (reflex/channeled/blazing) - show it as an inline icon next to the RES label.
		FString ResKindMarkup = TEXT("RES");
		if (!bUnit) {
			FString SpeedTag;
			if (Subsystem->TryGetReservesSpellSpeedTag(Idx, SpeedTag) && !SpeedTag.IsEmpty()) {
				ResKindMarkup += TEXT(" ") + TacticsCardText::IconMarkup(SpeedTag);
			}
		}

		ReservesScroll->AddSlot()
			.Padding(FMargin(0.f, 0.f, 0.f, 5.f))
			[
				SNew(SBox).WidthOverride(124.f).HeightOverride(152.f)
					[
						SNew(SBorder).BorderBackgroundColor(Frame).Padding(FMargin(2.f))
							[
								SNew(SButton)
									.ButtonColorAndOpacity(FLinearColor(0.08f, 0.08f, 0.1f, 1.f))
									.ForegroundColor(kCardTextWhite)
									.ContentPadding(FMargin(5.f))
									.OnClicked_Lambda([this, Idx, CardKind]() {
										// Acting out of turn pops the alert instead of silently arming the card.
										if (IsWaitingForControlledSeatTurn()) {
											ShowFailureAlert(
												TEXT("Not your turn - wait for your turn or switch Control seat."));
											return FReply::Handled();
										}
										DeployReservesIndex1Based = Idx;
										bReservesArmedForTile = true;
										bDetailPanelShowsHandCard = true;
										bHandArmedForTile = false;
										bAbilityArmedForTile = false;
										ArmedAbilityKey.Empty();
										ArmedMulticastTargetTotal = 0;
										ArmedMulticastTargetsPicked = 0;
										ArmedMulticastCliTokens.Empty();
										HideAbilityDescription();
										if (Subsystem.IsValid()) {
											if (CardKind.Equals(TEXT("spell"), ESearchCase::IgnoreCase)) {
												bool bModal = false;
												if (Subsystem->TryGetReservesSpellIsModal(Idx, bModal) && bModal) {
													bModalSpellPickerActive = true;
													bModalSpellFromReserves = true;
													ArmedSpellModeIndex = -1;
													LastCliOutput = TEXT("Choose a mode - click one of the card copies below.");
													RefreshStatusText();
													Refresh();
													return FReply::Handled();
												}
												int32 Mc = 1;
												bool bPerCopy = false;
												if (Subsystem->TryGetReservesSpellMulticastInfo(Idx, Mc, bPerCopy) && bPerCopy && Mc > 1) {
													ArmedMulticastTargetTotal = Mc;
												}
												InitArmedXCostForReservesSpell(Idx);
												if (IsArmedFocusSpell()) {
													bFocusSpellAwaitingCaster = true;
													bPushSpellAwaitingUnit = false;
													Subsystem->ClearSelectedUnitOnly();
													Subsystem->SetBoardFocusCasterSelectionPreviewForReservesCard(Idx);
													LastCliOutput = IsArmedXCostSpell()
														? FString::Printf(
															TEXT(
																"Reserves: spell armed - set X (bottom right, currently %d), click a highlighted unit to cast from, then click a target."),
															ArmedXCostAmount)
														: TEXT(
															"Reserves: spell armed - click a highlighted unit to cast from, then click a target.");
												} else {
													bool bPushDirection = false;
													bFocusSpellAwaitingCaster = false;
													bPushSpellAwaitingUnit =
														Subsystem->TryGetReservesSpellUsesPushDirectionAim(Idx, bPushDirection) && bPushDirection;
													PushSpellTargetWorldX = -1;
													PushSpellTargetWorldY = -1;
													Subsystem->SetBoardTargetPreviewForReservesCard(Idx);
													LastCliOutput = bPushSpellAwaitingUnit
														? TEXT(
															"Reserves: Push spell armed - click a unit to push, then choose a cardinal direction.")
														: TEXT(
															"Reserves armed - click a highlighted tile to cast (your turn only).");
												}
											} else if (CardKind.Equals(TEXT("unit"), ESearchCase::IgnoreCase)
												|| CardKind.Equals(TEXT("building"), ESearchCase::IgnoreCase)
												|| CardKind.Equals(TEXT("obstacle"), ESearchCase::IgnoreCase)) {
												bFocusSpellAwaitingCaster = false;
												Subsystem->SetDeployPreviewForReservesCard(Idx);
												LastCliOutput = TEXT(
													"Reserves: unit armed - click a highlighted tile to deploy (your turn only).");
											} else {
												bFocusSpellAwaitingCaster = false;
												Subsystem->ClearBoardTargetPreview();
												Subsystem->ClearDeployPreview();
												LastCliOutput = TEXT(
													"Reserves armed - click the board on your turn.");
											}
										}
										RefreshStatusText();
										Refresh();
										return FReply::Handled();
									})
									[
										SNew(SVerticalBox)
										+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
											  [
												  SNew(SBox)
													  .HeightOverride(kReservesCardKindRowHeight)
													  .VAlign(VAlign_Center)
													  [
														  SNew(SRichTextBlock)
															  .TextStyle(&TacticsCardText::EnergyTextStyle(10, kCardTextWhite))
															  .DecoratorStyleSet(&FCoreStyle::Get())
															  .Decorators(TacticsCardText::MakeEnergyDecorators(10))
															  .Text(FText::FromString(ResKindMarkup))
													  ]
											  ]
										+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
											  [SNew(STextBlock)
												   .WrapTextAt(108.f)
												   .Font(TacticsCardText::DesignFont(TEXT("Bold"), 13))
												   .ColorAndOpacity(kCardTextWhite)
												   .Text(FText::FromString(Name))]
										+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Bottom)
											  [SNew(SRichTextBlock)
												   .WrapTextAt(108.f)
												   .TextStyle(&TacticsCardText::EnergyTextStyle(9, kCardTextWhite))
												   .DecoratorStyleSet(&FCoreStyle::Get())
												   .Decorators(TacticsCardText::MakeEnergyDecorators(9))
												   .Text(FText::FromString(TacticsCardText::MarkupEnergyTokens(Cost)))]
									]
							]
					]
			];
	}
}

void STacticsBoardPanel::RebuildScanChoiceBar()
{
	if (!HandScroll.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	ClearPlayArmingState();
	HandScroll->ClearChildren();
	const int32 N = Subsystem->GetPendingScanPeekCount();
	if (N < 1) {
		return;
	}

	for (int32 Idx = 1; Idx <= N; ++Idx) {
		FString Name, CardKind, Cost, CostUnused;
		if (!Subsystem->TryGetScanPeekCardUi(Idx, Name, CardKind, Cost)) {
			continue;
		}
		if (Name.Len() > 18) {
			Name = Name.Left(17) + TEXT("…");
		}
		if (Cost.Len() > 52) {
			Cost = Cost.Left(51) + TEXT("…");
		}
		// The selected-but-unconfirmed scanned card gets a bright frame so the player can see what they
		// are about to discard while they read it in the left panel.
		const bool bArmed = (ArmedScanCardIndex == Idx);
		const FLinearColor Frame = bArmed ? FLinearColor(0.55f, 0.42f, 0.95f, 1.f)
										  : FLinearColor(0.22f, 0.16f, 0.48f, 1.f);

		HandScroll->AddSlot()
			.Padding(FMargin(5.f, 0.f, 5.f, 0.f))
			[
				SNew(SBox).WidthOverride(118.f).HeightOverride(152.f)
					[
						SNew(SBorder).BorderBackgroundColor(Frame).Padding(FMargin(2.f))
							[
								SNew(SButton)
									.ButtonColorAndOpacity(FLinearColor(0.08f, 0.08f, 0.1f, 1.f))
									.ForegroundColor(kCardTextWhite)
									.ContentPadding(FMargin(6.f))
									.OnClicked_Lambda([this, Idx]() {
										// Select (arm) the card to read it first; a second click on the same
										// card, or the Discard button, actually discards it. Scanning is
										// look-then-decide, so instant-discard-on-click was too easy to misfire.
										if (ArmedScanCardIndex == Idx) {
											ArmedScanCardIndex = -1;
											RunCli(FString::Printf(TEXT("scan_discard %d"), Idx));
										} else {
											ArmedScanCardIndex = Idx;
											RefreshStatusText();
											Refresh();
										}
										return FReply::Handled();
									})
									[
										SNew(SVerticalBox)
										+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
											  [SNew(STextBlock)
												   .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
												   .ColorAndOpacity(kCardTextWhite)
												   .Text(FText::FromString(TEXT("SCAN")))]
										+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
											  [SNew(STextBlock)
												   .WrapTextAt(100.f)
												   .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 11))
												   .ColorAndOpacity(kCardTextWhite)
												   .Text(FText::FromString(Name))]
										+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Bottom)
											  [SNew(STextBlock)
												   .WrapTextAt(100.f)
												   .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 8))
												   .ColorAndOpacity(kCardTextWhite)
												   .Text(FText::FromString(Cost.IsEmpty() ? CardKind : Cost))]
									]
							]
					]
			];
	}
}

void STacticsBoardPanel::RebuildZoneChoiceBar()
{
	if (!HandScroll.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	ClearPlayArmingState();
	HandScroll->ClearChildren();
	const int32 N = Subsystem->GetPendingEnergyZoneChoiceCount();
	if (N < 1) {
		return;
	}

	for (int32 Idx = 1; Idx <= N; ++Idx) {
		FString ZName, Produces, ArtId;
		if (!Subsystem->TryGetEnergyZoneChoiceUi(Idx, ZName, Produces, ArtId)) {
			continue;
		}
		if (ZName.Len() > 18) {
			ZName = ZName.Left(17) + TEXT("…");
		}
		if (Produces.Len() > 52) {
			Produces = Produces.Left(51) + TEXT("…");
		}
		// The selected-but-unconfirmed choice gets a bright frame so the player can see what is armed
		// while they read it, before pressing Confirm.
		const bool bArmed = (ArmedZoneChoiceIndex == Idx);
		const FLinearColor Frame = bArmed ? FLinearColor(0.30f, 0.85f, 0.45f, 1.f)
										  : FLinearColor(0.14f, 0.38f, 0.22f, 1.f);
		const FVector2D ArtSize(132.f, 96.f);
		const FSlateBrush* ArtBrush = ArtId.IsEmpty() ? nullptr : TacticsCardArtUi::GetCardArtBrush(ArtId, ArtSize);
		TSharedRef<SWidget> ArtBody = ArtBrush
			? StaticCastSharedRef<SWidget>(SNew(SImage).Image(ArtBrush))
			: StaticCastSharedRef<SWidget>(
				  SNew(SBox)
					  .HAlign(HAlign_Center)
					  .VAlign(VAlign_Center)
					  [
						  SNew(STextBlock)
							  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 11))
							  .ColorAndOpacity(kCardTextWhite)
							  .Justification(ETextJustify::Center)
							  .Text(FText::FromString(ZName))
					  ]);

		// Match the normal hand-card footprint (152 × kHandStripCardHeight) so Conquering
		// Territories cards are the same size as unit/spell cards, not smaller.
		HandScroll->AddSlot()
			.Padding(FMargin(5.f, 0.f, 5.f, 0.f))
			[
				SNew(SBox).WidthOverride(152.f).HeightOverride(kHandStripCardHeight)
					[
						SNew(SBorder).BorderBackgroundColor(Frame).Padding(FMargin(2.f))
							[
								SNew(SButton)
									.ButtonColorAndOpacity(FLinearColor(0.08f, 0.08f, 0.1f, 1.f))
									.ForegroundColor(kCardTextWhite)
									.ContentPadding(FMargin(4.f))
									.OnClicked_Lambda([this, Idx]() {
										// Select (arm) the territory instead of placing it immediately, so the
										// player can read its description first. A second click on the same card,
										// or the "Confirm Territory" button, actually places it (zonepick).
										if (ArmedZoneChoiceIndex == Idx) {
											ArmedZoneChoiceIndex = -1;
											RunCli(FString::Printf(TEXT("zonepick %d"), Idx));
										} else {
											ArmedZoneChoiceIndex = Idx;
											RefreshStatusText();
											Refresh();
										}
										return FReply::Handled();
									})
									[
										SNew(SVerticalBox)
										+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
											  [SNew(STextBlock)
												   .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
												   .ColorAndOpacity(kCardTextWhite)
												   .Text(FText::FromString(TEXT("TERRITORY")))]
										+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 3.f)
											  [
												  SNew(SBox).WidthOverride(ArtSize.X).HeightOverride(ArtSize.Y)
													  [
														  SNew(SBorder).BorderBackgroundColor(FLinearColor(0.04f, 0.04f, 0.06f, 1.f))
															  [ArtBody]
													  ]
											  ]
										+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
											  [SNew(STextBlock)
												   .WrapTextAt(132.f)
												   .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 11))
												   .ColorAndOpacity(kCardTextWhite)
												   .Text(FText::FromString(ZName))]
										+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Bottom)
											  [SNew(STextBlock)
												   .WrapTextAt(132.f)
												   .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 8))
												   .ColorAndOpacity(kCardTextWhite)
												   .Text(FText::FromString(Produces.IsEmpty() ? TEXT("(use land)") : Produces))]
									]
							]
					]
			];
	}
}

void STacticsBoardPanel::RebuildTerritoryBar()
{
	if (!TerritoryBar.IsValid()) {
		return;
	}
	TerritoryBar->ClearChildren();
	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady()) {
		return;
	}
	// Show the interactive "use land" buttons whenever the controlled player can act: their own main /
	// attack phase (special + energy abilities), or a reaction window where they hold priority (energy
	// abilities only - special abilities are channeled and stay disabled via bUsableNow). We always LIST
	// every territory regardless of phase so the right-side rail shows all their lands.
	const bool bCanUseLands = Subsystem->CanControlledPlayerActInMainPhase()
		|| (Subsystem->IsAnyReactionWindowPhase() && Subsystem->CanControlledPlayerPassPriority());

	// Docked in the width-bounded right rail - keep everything vertical + text-wrapped so nothing
	// overflows onto the board. `LabelWrap` bounds the button text to the panel width.
	const float PanelW = Uses3DBoardChrome() ? kEnergyReservesPanelWidth : 300.f;
	const float LabelWrap = FMath::Max(120.f, PanelW - 28.f);

	// Stack identical territories: same name + same depleted state collapse into one entry with a
	// ×count badge. Depleted copies form their own group so they land in the separate "Depleted"
	// section below. `RepIndex` is a representative territory used for `use_land` (prefer one with a
	// charge available so the ability buttons are live).
	struct FTerritoryGroup {
		FString Name;
		FString ArtId;
		int32 AbilityCount = 0;
		bool bDepleted = false;
		int32 Count = 0;
		int32 RepIndex = -1;
	};
	TArray<FTerritoryGroup> Groups;
	TMap<FString, int32> GroupLookup;

	const int32 TerritoryCount = Subsystem->GetControlledTerritoryCount();
	for (int32 T = 0; T < TerritoryCount; ++T) {
		FString TName, TArtId;
		bool bDepleted = false;
		bool bUseAvailable = false;
		if (!Subsystem->TryGetTerritoryUi(T, TName, bDepleted, bUseAvailable, TArtId)) {
			continue;
		}
		const FString Key = TName + (bDepleted ? TEXT("|D") : TEXT("|A"));
		int32 Gi;
		if (const int32* Existing = GroupLookup.Find(Key)) {
			Gi = *Existing;
		} else {
			Gi = Groups.Num();
			FTerritoryGroup NG;
			NG.Name = TName;
			NG.ArtId = TArtId;
			NG.bDepleted = bDepleted;
			NG.AbilityCount = Subsystem->GetTerritoryLandAbilityCount(T);
			Groups.Add(NG);
			GroupLookup.Add(Key, Gi);
		}
		FTerritoryGroup& G = Groups[Gi];
		++G.Count;
		// Use-availability is uniform within a (name, depleted) group - depleted⇔charge spent - so the
		// first member is a fine representative for `use_land`; the button's own bUsableNow is authoritative.
		if (G.RepIndex < 0) {
			G.RepIndex = T;
		}
	}

	// Build one stacked entry (header + abilities only when selected).
	auto AddGroupWidget = [&](const FTerritoryGroup& G) {
		const TSharedRef<SVerticalBox> Entry = SNew(SVerticalBox);
		const bool bSelected = (SelectedTerritoryRepIndex == G.RepIndex);

		const FVector2D ThumbSize(18.f, 18.f);
		const FSlateBrush* ThumbBrush =
			G.ArtId.IsEmpty() ? nullptr : TacticsCardArtUi::GetCardArtBrush(G.ArtId, ThumbSize);
		const TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox);
		if (ThumbBrush) {
			Header->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(FMargin(0.f, 0.f, 4.f, 0.f))
				[SNew(SBox).WidthOverride(ThumbSize.X).HeightOverride(ThumbSize.Y)[SNew(SImage).Image(ThumbBrush)]];
		}
		FString Title = G.Name;
		if (G.Count > 1) {
			Title += FString::Printf(TEXT("  x%d"), G.Count);
		}
		if (bSelected && !G.bDepleted) {
			Title += TEXT("  ◂");
		}
		Header->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
			[SNew(STextBlock)
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
					.WrapTextAt(LabelWrap)
					.ColorAndOpacity(G.bDepleted ? FLinearColor(0.55f, 0.55f, 0.55f, 1.f) : kCardTextWhite)
					.Text(FText::FromString(Title))];

		Entry->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 2.f))
			[SNew(SButton)
					.ButtonColorAndOpacity(bSelected
						? FLinearColor(0.18f, 0.28f, 0.20f, 1.f)
						: FLinearColor(0.08f, 0.08f, 0.09f, 0.85f))
					.ForegroundColor(kCardTextWhite)
					.ContentPadding(FMargin(2.f, 2.f))
					.ToolTipText(FText::FromString(
						G.bDepleted ? TEXT("Depleted - refreshes on your next turn.")
									: TEXT("Select to show land abilities.")))
					.OnClicked_Lambda([this, Rep = G.RepIndex]() {
						SelectedTerritoryRepIndex = (SelectedTerritoryRepIndex == Rep) ? -1 : Rep;
						SetTerritoriesRailExpanded(true, true);
						Refresh();
						return FReply::Handled();
					})
					[Header]];

		if (G.bDepleted) {
			Entry->AddSlot().AutoHeight()
				[SNew(STextBlock)
						.Font(TacticsCardText::DefaultFont(TEXT("Italic"), 8))
						.WrapTextAt(LabelWrap)
						.ColorAndOpacity(FLinearColor(0.55f, 0.55f, 0.55f, 1.f))
						.Text(FText::FromString(TEXT("Refreshes at your next turn.")))];
		} else if (bSelected) {
			const int32 T = G.RepIndex;
			const int32 AbilityCount = bCanUseLands ? G.AbilityCount : 0;
			for (int32 A = 0; A < AbilityCount; ++A) {
				FString AName, SpeedTok, CostLine, EffectLine;
				bool bUsableNow = false;
				bool bNeedsTarget = false;
				if (!Subsystem->TryGetLandAbilityUi(T, A, AName, SpeedTok, CostLine, EffectLine, bUsableNow, bNeedsTarget)) {
					continue;
				}
				FString Label = SpeedTok.IsEmpty() ? AName : (SpeedTok + TEXT(" ") + AName);
				if (!CostLine.IsEmpty()) {
					Label += TEXT("  ") + CostLine;
				}
				if (!EffectLine.IsEmpty()) {
					Label += FString::Printf(TEXT("  - %s"), *EffectLine);
				}
				if (bNeedsTarget) {
					Label += TEXT("  (target)");
				}

				Entry->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 2.f))
					[SNew(SButton)
							.IsEnabled(bUsableNow)
							.HAlign(HAlign_Fill)
							.ButtonColorAndOpacity(FLinearColor(0.10f, 0.24f, 0.16f, 1.f))
							.ForegroundColor(kCardTextWhite)
							.ContentPadding(FMargin(4.f, 2.f))
							.OnClicked_Lambda([this, T, A, bNeedsTarget, Label]() {
								ClearPlayArmingState();
								ArmedLandTerritoryIndex = T;
								ArmedLandAbilityIndex = A;
								ArmedLandDescription = Label;
								SelectedTerritoryRepIndex = T;
								SetTerritoriesRailExpanded(true, true);
								if (bNeedsTarget) {
									bLandAbilityArmedForTile = true;
									LastCliOutput = FString::Printf(
										TEXT("Use Land armed: %s. Click a target unit, or Cancel."), *Label);
								} else {
									bLandAbilityArmedNoTarget = true;
									LastCliOutput = FString::Printf(
										TEXT("Use Land armed: %s. Press Confirm use land, or Cancel."), *Label);
								}
								RefreshStatusText();
								Refresh();
								return FReply::Handled();
							})
							[SNew(SRichTextBlock)
									.WrapTextAt(LabelWrap)
									.TextStyle(&TacticsCardText::EnergyTextStyle(8, kCardTextWhite))
									.DecoratorStyleSet(&FCoreStyle::Get())
									.Decorators(TacticsCardText::MakeEnergyDecorators(
										8, TacticsCardText::ESpeedTooltipSubject::Ability))
									.Text(FText::FromString(TacticsCardText::MarkupEnergyTokens(Label)))]];
			}

			if (!bCanUseLands && G.AbilityCount > 0) {
				Entry->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 2.f))
					[SNew(STextBlock)
							.Font(TacticsCardText::DefaultFont(TEXT("Italic"), 8))
							.WrapTextAt(LabelWrap)
							.ColorAndOpacity(FLinearColor(0.62f, 0.62f, 0.62f, 1.f))
							.Text(FText::FromString(TEXT("Lands are idle until you have priority to act.")))];
			} else if (bCanUseLands && G.AbilityCount == 0) {
				Entry->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 2.f))
					[SNew(STextBlock)
							.Font(TacticsCardText::DefaultFont(TEXT("Italic"), 8))
							.WrapTextAt(LabelWrap)
							.ColorAndOpacity(FLinearColor(0.62f, 0.62f, 0.62f, 1.f))
							.Text(FText::FromString(TEXT("No activated land abilities.")))];
			}
		}

		TerritoryBar->AddSlot().AutoHeight().Padding(FMargin(0.f, 0.f, 0.f, 3.f))
			[SNew(SBox).MaxDesiredWidth(PanelW)
					[SNew(SBorder)
							.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.55f))
							.Padding(FMargin(4.f, 3.f))
							[Entry]]];
	};

	if (Groups.Num() == 0) {
		SelectedTerritoryRepIndex = -1;
	} else if (SelectedTerritoryRepIndex >= 0) {
		bool bStillValid = false;
		for (const FTerritoryGroup& G : Groups) {
			if (G.RepIndex == SelectedTerritoryRepIndex) {
				bStillValid = true;
				break;
			}
		}
		if (!bStillValid) {
			SelectedTerritoryRepIndex = -1;
		}
	}

	// Active stacks first.
	for (const FTerritoryGroup& G : Groups) {
		if (!G.bDepleted) {
			AddGroupWidget(G);
		}
	}
	// Depleted stacks under their own heading.
	bool bAnyDepleted = false;
	for (const FTerritoryGroup& G : Groups) {
		if (G.bDepleted) {
			bAnyDepleted = true;
			break;
		}
	}
	if (bAnyDepleted) {
		TerritoryBar->AddSlot().AutoHeight().Padding(FMargin(0.f, 4.f, 0.f, 2.f))
			[SNew(STextBlock)
					.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 8))
					.ColorAndOpacity(FLinearColor(0.6f, 0.6f, 0.6f, 1.f))
					.Text(FText::FromString(TEXT("DEPLETED")))];
		for (const FTerritoryGroup& G : Groups) {
			if (G.bDepleted) {
				AddGroupWidget(G);
			}
		}
	}

	if (Groups.Num() == 0) {
		TerritoryBar->AddSlot().AutoHeight()
			[SNew(STextBlock)
					.Font(TacticsCardText::DefaultFont(TEXT("Italic"), 9))
					.WrapTextAt(LabelWrap)
					.ColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.f))
					.Text(FText::FromString(TEXT("No territories yet. Conquer lands during the Energy phase.")))];
	}
}

TSharedRef<SWidget> STacticsBoardPanel::BuildStackCard(const FTacticsStackItemUi& Item)
{
	FString SourceName = Item.SourceName;
	if (SourceName.Len() > 26) {
		SourceName = SourceName.Left(25) + TEXT("...");
	}
	FString EffectLine = Item.EffectKey;
	if (EffectLine.Len() > 34) {
		EffectLine = EffectLine.Left(33) + TEXT("...");
	}
	FString TargetLine = Item.TargetLine;
	if (TargetLine.Len() > 38) {
		TargetLine = TargetLine.Left(37) + TEXT("...");
	}

	const bool bCanTargetStack = IsArmedStackTargetEffect() && (Item.StackId.StartsWith(TEXT("batch_")) || Item.StackId.StartsWith(TEXT("stack_"))) && CanArmedStackTargetSourceType(Item.SourceType);
	const FString TypeLine = FString::Printf(TEXT("%s ON STACK"), *Item.SourceType.ToUpper());
	const FString MetaLine = FString::Printf(TEXT("P%d  |  %s  |  %s"), Item.ControllerPlayerId, *Item.Speed.ToUpper(), *Item.StackId);
	FLinearColor Frame = Item.SourceType.Equals(TEXT("attack"), ESearchCase::IgnoreCase)
							 ? FLinearColor(0.48f, 0.24f, 0.10f, 1.f)
							 : Item.SourceType.Equals(TEXT("ability"), ESearchCase::IgnoreCase)
							 ? FLinearColor(0.42f, 0.18f, 0.52f, 1.f)
							 : FLinearColor(0.12f, 0.28f, 0.52f, 1.f);
	if (bCanTargetStack) {
		Frame = FLinearColor(0.55f, 0.42f, 0.08f, 1.f);
	}

	auto MakeBody = [TypeLine, SourceName, MetaLine, EffectLine, TargetLine]() -> TSharedRef<SWidget> {
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				  [SNew(STextBlock)
					   .Font(TacticsCardText::DesignFont(TEXT("Bold"), 8))
					   .ColorAndOpacity(kCardTextWhite)
					   .Text(FText::FromString(TypeLine))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				  [SNew(STextBlock)
					   .WrapTextAt(214.f)
					   .Font(TacticsCardText::DesignFont(TEXT("Bold"), 12))
					   .ColorAndOpacity(kCardTextWhite)
					   .Text(FText::FromString(SourceName))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				  [SNew(STextBlock)
					   .WrapTextAt(214.f)
					   .Font(TacticsCardText::DesignFont(TEXT("Regular"), 8))
					   .ColorAndOpacity(kCardTextWhite)
					   .Text(FText::FromString(MetaLine))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
				  [SNew(STextBlock)
					   .WrapTextAt(214.f)
					   .Font(TacticsCardText::DesignFont(TEXT("Italic"), 8))
					   .ColorAndOpacity(kCardTextWhite)
					   .Text(FText::FromString(EffectLine))]
			+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Bottom)
				  [SNew(STextBlock)
					   .WrapTextAt(214.f)
					   .Font(TacticsCardText::DesignFont(TEXT("Regular"), 8))
					   .ColorAndOpacity(kCardTextWhite)
					   .Text(FText::FromString(TargetLine))];
	};

	if (bCanTargetStack) {
		return SNew(SBox).WidthOverride(238.f).HeightOverride(132.f)
			[SNew(SBorder).BorderBackgroundColor(Frame).Padding(FMargin(2.f))
					[SNew(SButton)
							.ButtonColorAndOpacity(FLinearColor(0.08f, 0.08f, 0.1f, 1.f))
							.ForegroundColor(kCardTextWhite)
							.ContentPadding(FMargin(8.f, 6.f))
							.OnClicked_Lambda([this, StackId = Item.StackId]() {
								if (bHandArmedForTile) {
									const int32 Idx = DeployHandIndex1Based;
									ClearPlayArmingState();
									RunCli(FString::Printf(TEXT("cast %d stack %s"), Idx, *StackId));
								} else if (bReservesArmedForTile) {
									const int32 Idx = DeployReservesIndex1Based;
									ClearPlayArmingState();
									RunCli(FString::Printf(TEXT("cast_reserve %d stack %s"), Idx, *StackId));
								} else if (bAbilityArmedForTile) {
									const FString Line = BuildArmedAbilityStackCliLine(StackId);
									const FString KeyCopy = ArmedAbilityKey;
									RunAbilityCli(Line, KeyCopy, {});
									ClearPlayArmingState();
								}
								return FReply::Handled();
							})[MakeBody()]]];
	}

	return SNew(SBox).WidthOverride(238.f).HeightOverride(132.f)
		[SNew(SBorder).BorderBackgroundColor(Frame).Padding(FMargin(2.f))
				[SNew(SBorder).BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.1f, 1.f)).Padding(FMargin(8.f, 6.f))[MakeBody()]]];
}

