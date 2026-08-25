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


void STacticsBoardPanel::Construct(const FArguments& InArgs)
{
	sActiveInstance = this;
	Subsystem = InArgs._Subsystem;
	LoadBoardUiPreferences();

	if (Subsystem.IsValid()) {
		LocalDemoPlayerCount = FMath::Clamp(Subsystem->GetMatchPlayerCount(), 1, 12);
		BoardChangedHandle = Subsystem->OnBoardChanged.AddSP(StaticCastSharedRef<STacticsBoardPanel>(AsShared()), &STacticsBoardPanel::Refresh);
		AbilityDamagePopupsChangedHandle = Subsystem->OnAbilityDamagePopupsChanged.AddSP(
			StaticCastSharedRef<STacticsBoardPanel>(AsShared()), &STacticsBoardPanel::RefreshAbilityDamagePopups);
		TargetPreviewChangedHandle = Subsystem->OnTargetPreviewChanged.AddSP(
			StaticCastSharedRef<STacticsBoardPanel>(AsShared()), &STacticsBoardPanel::RefreshBoardTargetHighlights);
		UTacticsWebSocketSubsystem* Net = nullptr;
		if (ResolveWebSocketNet(Net)) {
			CliAckHandle = Net->OnCliAckFromHost.AddSP(StaticCastSharedRef<STacticsBoardPanel>(AsShared()), &STacticsBoardPanel::OnHostCliAckFromNet);
		}
	}

	ChildSlot
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SAssignNew(RootOverlay, SOverlay)
			+ SOverlay::Slot()
				  .HAlign(HAlign_Left)
				  .VAlign(VAlign_Top)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetTopLeftChromePadding))
				  [
					  SNew(SBorder)
						  .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 1.f))
						  .Padding(FMargin(8.f))
						  [
							  SNew(SBox)
								  .MaxDesiredWidth_Lambda([this]() -> FOptionalSize {
									  return Uses3DBoardChrome() ? k3DLeftChromeWidth : FOptionalSize();
								  })
								  [
									  SNew(SVerticalBox)
									  + SVerticalBox::Slot()
											.AutoHeight()
											.Padding(0.f, 0.f, 0.f, 6.f)
											[SNew(STextBlock)
												 .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 13))
												 .ColorAndOpacity_Lambda([this]() { return GetChromeTextColor(); })
												 .Text(FText::FromString(TEXT("Tactics board")))]
									  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
											[
												SNew(SBorder)
													.BorderBackgroundColor(FLinearColor(0.94f, 0.94f, 0.96f, 1.f))
													.Padding(FMargin(8.f))
													[
														SNew(SBox)
															.MaxDesiredHeight_Lambda([this]() {
																return Uses3DBoardChrome() ? 96.f : 48.f;
															})
															[
																SNew(SScrollBox).Orientation(Orient_Horizontal)
																+ SScrollBox::Slot()[SAssignNew(ActionBar, SHorizontalBox)]
															]
													]
											]
									  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
											[SNew(STextBlock)
												 .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
												 .ColorAndOpacity_Lambda([this]() { return GetChromeTextColor(); })
												 .WrapTextAt_Lambda([this]() {
													 return Uses3DBoardChrome() ? k3DLeftChromeWidth : 680.f;
												 })
												 .Text(FText::FromString(
													 TEXT("Reserves open on the left; Available Energy sits above Territories on the right. Tap a land to show abilities. Main: hand strip; select a unit and click an ability in its card description to cast.")))]
									  + SVerticalBox::Slot()
											.AutoHeight()
											.Padding(0.f, 0.f, 0.f, 6.f)
											[
												SNew(SBox)
													.Visibility_Lambda([this]() {
														return (bShowDevTools && bShowConsoleLog)
															? EVisibility::Visible
															: EVisibility::Collapsed;
													})
													.MaxDesiredHeight_Lambda([this]() {
														return Uses3DBoardChrome() ? k3DLeftLogMaxHeight : 140.f;
													})
													[
														SNew(SScrollBox)
														+ SScrollBox::Slot()
															  [
																  SNew(SVerticalBox)
																  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
																		[SAssignNew(TipsText, STextBlock)
																			 .WrapTextAt_Lambda([this]() {
																				 return Uses3DBoardChrome() ? k3DLeftChromeWidth : 680.f;
																			 })
																			 .Font(TacticsCardText::DebugFont(9))
																			 .ColorAndOpacity_Lambda([this]() {
																				 return Uses3DBoardChrome()
																						? FLinearColor(0.82f, 0.86f, 0.92f, 1.f)
																						: FLinearColor(0.12f, 0.16f, 0.28f, 1.f);
																			 })
																			 .Text(FText::FromString(
																				 TEXT("Tips: addfloat neutral 1 before cheap spells/abilities. End main phase to commit the batch queue. Balance: TacticsData/ability_catalog.json and passive_catalog.json merge over built-ins.")))]
																  + SVerticalBox::Slot().AutoHeight()
																		[SAssignNew(StatusText, STextBlock)
																			 .WrapTextAt_Lambda([this]() {
																				 return Uses3DBoardChrome() ? k3DLeftChromeWidth : 680.f;
																			 })
																			 .Font(TacticsCardText::DebugFont(10))
																			 .ColorAndOpacity_Lambda([this]() { return GetChromeTextColor(); })]
															  ]
													]
											]
								  ]
						  ]
				  ]
			+ SOverlay::Slot()
				  .ZOrder(25)
				  .HAlign(HAlign_Center)
				  .VAlign(VAlign_Top)
				  .Padding(FMargin(0.f, 8.f, 0.f, 0.f))
				  [
					  SNew(SBorder)
						  .BorderBackgroundColor_Lambda([this]() { return GetNetworkStatusPillColor(); })
						  .Padding(FMargin(10.f, 4.f))
						  [
							  SAssignNew(NetworkStatusPillText, STextBlock)
								  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 10))
								  .ColorAndOpacity(FLinearColor::White)
								  .Text_Lambda([this]() { return GetNetworkStatusPillText(); })
						  ]
				  ]
			+ SOverlay::Slot()
				  .HAlign(HAlign_Right)
				  .VAlign(VAlign_Top)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetActionQueueChromePadding))
				  [
					  SNew(SBorder)
						  .BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
						  .BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor())
						  .Padding(FMargin(8.f, 6.f))
						  [
					  SNew(SBox).WidthOverride(kActionQueuePanelWidth)
						  [
							  SNew(SVerticalBox)
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
									[SNew(STextBlock)
										 .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 11))
										 .ColorAndOpacity_Lambda([this]() { return GetChromeTextColor(); })
										 .Text_Lambda([this]() {
											 if (Subsystem.IsValid() && Subsystem->IsAnyAttackDeclarationPhase()) {
												 return FText::FromString(TEXT("Attack queue"));
											 }
											 return FText::FromString(TEXT("Batch queue"));
										 })]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
									[SNew(STextBlock)
										 .WrapTextAt(kActionQueuePanelWidth - 12.f)
										 .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 8))
										 .ColorAndOpacity_Lambda([this]() { return GetChromeTextColor(); })
										 .Text(FText::FromString(
											 TEXT("Top = resolves next. Newest batch on top, oldest batch at bottom; first queued in a batch above later ones.")))]
							  + SVerticalBox::Slot()
									.FillHeight(1.f)
									.Padding(0.f, 0.f, 0.f, 10.f)
									[
										SNew(SBorder)
											.BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
											.BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor())
											.Padding(FMargin(6.f))
											[
												SNew(SBox).MaxDesiredHeight(420.f)
													[SAssignNew(ActionQueueScroll, SScrollBox).Orientation(Orient_Vertical)]
											]
									]
						  ]
				  ]
				  ]  // SBorder (action queue)
			+ SOverlay::Slot()
				  .HAlign(HAlign_Right)
				  .VAlign(VAlign_Top)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetTopRightChromePadding))
				  [
					  SNew(SBorder)
						  .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 1.f))
						  .Padding(FMargin(8.f, 6.f))
						  [
					  SNew(SBox).WidthOverride(kRightSidePanelWidth)
						  [
							  SNew(SVerticalBox)
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
									[SNew(STextBlock)
										 .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 11))
										 .ColorAndOpacity_Lambda([this]() { return GetChromeTextColor(); })
										 .Text(FText::FromString(TEXT("Selected stats")))]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 0.f)
									[
										SNew(SBorder)
											.BorderBackgroundColor_Lambda([this]() { return GetSidePanelBorderColor(); })
											.Padding(FMargin(8.f))
											[
												SNew(SBox).MaxDesiredHeight(260.f)
													[
														SNew(SScrollBox)
														+ SScrollBox::Slot()
															  [
																  SAssignNew(SelectedStatsText, STextBlock)
																	  .WrapTextAt(236.f)
																	  .Font(TacticsCardText::DebugFont(9))
																	  .ColorAndOpacity_Lambda([this]() { return GetChromeTextColor(); })
																	  .Text(FText::FromString(
																		  TEXT("Click a unit or building to inspect stats and attributes.")))
															  ]
													]
											]
									]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 6.f)
									[
										SNew(SButton)
											.ForegroundColor_Lambda([this]() { return GetChromeTextColor(); })
											.OnClicked_Lambda([this]() {
												bShowDiscardPilePanel = !bShowDiscardPilePanel;
												RebuildDiscardPilePanel();
												return FReply::Handled();
											})
											[
												SNew(STextBlock)
													.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 11))
													.ColorAndOpacity_Lambda([this]() { return GetChromeTextColor(); })
													.Text_Lambda([this]() {
														int32 Total = 0;
														if (Subsystem.IsValid()) {
															for (int32 Seat = 1; Seat <= Subsystem->GetMatchPlayerCount(); ++Seat) {
																Total += Subsystem->GetPlayerDiscardCount(Seat);
															}
														}
														return FText::FromString(FString::Printf(
															TEXT("Discard piles (%d)%s"),
															Total,
															bShowDiscardPilePanel ? TEXT(" ▼") : TEXT(" ▶")));
													})
											]
									]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f)
									[
										SAssignNew(DiscardPilePanelChrome, SBox)
											.Visibility_Lambda([this]() {
												return bShowDiscardPilePanel ? EVisibility::Visible : EVisibility::Collapsed;
											})
											[
												SNew(SBorder)
													.BorderBackgroundColor_Lambda([this]() { return GetSidePanelBorderColor(); })
													.Padding(FMargin(6.f))
													[
														SNew(SBox).MaxDesiredHeight(200.f)
															[
																SAssignNew(DiscardPileScroll, SScrollBox).Orientation(Orient_Vertical)
															]
													]
											]
									]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 10.f, 0.f, 6.f)
									[
										SNew(SButton)
											.ForegroundColor_Lambda([this]() { return GetChromeTextColor(); })
											.OnClicked_Lambda([this]() {
												bShowPurgatoryPanel = !bShowPurgatoryPanel;
												RebuildPurgatoryPanel();
												return FReply::Handled();
											})
											[
												SNew(STextBlock)
													.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 11))
													.ColorAndOpacity_Lambda([this]() { return GetChromeTextColor(); })
													.Text_Lambda([this]() {
														int32 Total = 0;
														if (Subsystem.IsValid()) {
															for (int32 Seat = 1; Seat <= Subsystem->GetMatchPlayerCount(); ++Seat) {
																Total += Subsystem->GetPlayerPurgatoryCount(Seat);
															}
														}
														return FText::FromString(FString::Printf(
															TEXT("Purgatory (%d)%s"),
															Total,
															bShowPurgatoryPanel ? TEXT(" ▼") : TEXT(" ▶")));
													})
											]
									]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f)
									[
										SAssignNew(PurgatoryPanelChrome, SBox)
											.Visibility_Lambda([this]() {
												return bShowPurgatoryPanel ? EVisibility::Visible : EVisibility::Collapsed;
											})
											[
												SNew(SBorder)
													.BorderBackgroundColor_Lambda([this]() { return GetSidePanelBorderColor(); })
													.Padding(FMargin(6.f))
													[
														SNew(SBox).MaxDesiredHeight(200.f)
															[
																SAssignNew(PurgatoryScroll, SScrollBox).Orientation(Orient_Vertical)
															]
													]
											]
									]
						  ]
				  ]
				  ]  // SBorder (right stats column)
			+ SOverlay::Slot()
				  .HAlign(HAlign_Right)
				  .VAlign(VAlign_Center)
				  .Padding(FMargin(0.f, 0.f, kRightSidePanelWidth + 18.f, 0.f))
				  [
					  SAssignNew(AbilityDescriptionChrome, SBox)
						  .WidthOverride(kAbilityDescriptionPanelWidth)
						  .Visibility(EVisibility::Collapsed)
						  [
							  SNew(SBorder)
								  .BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
								  .BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor())
								  .Padding(FMargin(12.f, 11.f))
								  [
									  SAssignNew(AbilityDescriptionText, SRichTextBlock)
										  .WrapTextAt(kAbilityDescriptionPanelWidth - 24.f)
										  .LineHeightPercentage(1.18f)
										  .TextStyle(&TacticsCardText::EnergyTextStyle(10, kCardTextWhite))
										  .DecoratorStyleSet(&FCoreStyle::Get())
										  .Decorators(TacticsCardText::MakeEnergyDecorators(10,
											  TacticsCardText::ESpeedTooltipSubject::Ability))
										  .Text(FText::GetEmpty())
								  ]
						  ]
				  ]
			+ SOverlay::Slot()
				  .ZOrder(18)
				  .HAlign(HAlign_Right)
				  .VAlign(VAlign_Top)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetActionQueueHoverOverlayPadding))
				  [
					  SAssignNew(ActionQueueHoverOverlay, SBox)
						  .MaxDesiredWidth(kActionQueueHoverScrollMaxWidth)
						  .Visibility(EVisibility::Collapsed)
				  ]
			+ SOverlay::Slot()
				  .HAlign(HAlign_Center)
				  .VAlign(VAlign_Center)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetCenterGridOverlayPadding))
				  [
					  SAssignNew(CenterGridChrome, SBox)
						  .Visibility_Lambda([this]() {
							  return Uses3DBoardChrome() ? EVisibility::SelfHitTestInvisible : EVisibility::Visible;
						  })
						  .HAlign(HAlign_Center)
						  .VAlign(VAlign_Center)
						  [
							  SAssignNew(BoardGridSizer, SBox)
								  .HAlign(HAlign_Center)
								  .VAlign(VAlign_Center)
								  [
									  SNew(SBorder)
										  .Visibility_Lambda([this]() {
											  // 3D match uses world tiles + unit actors. A Visible grout
											  // SBorder is sized to the 2D grid and paints over those sprites.
											  if (Subsystem.IsValid() && Subsystem->Uses3DBoardTiles()) {
												  return EVisibility::Collapsed;
											  }
											  return EVisibility::Visible;
										  })
										  .BorderBackgroundColor(kBoardGridGroutBrown)
										  .Padding(0.f)
										  [
											  SNew(SOverlay)
												  + SOverlay::Slot()
														[
															SAssignNew(BoardCellsOverlay, SOverlay)
														.Visibility_Lambda([this]() {
															if (Subsystem.IsValid() && Subsystem->Uses3DBoardTiles()) {
																return EVisibility::Collapsed;
															}
															return EVisibility::Visible;
														})
												]
										  + SOverlay::Slot()
												.ZOrder(4)
												[
													SAssignNew(BoardUnitHoverOverlay, SOverlay)
														.Visibility_Lambda([this]() {
															if (Subsystem.IsValid() && Subsystem->Uses3DBoardTiles()) {
																return EVisibility::Collapsed;
															}
															return EVisibility::HitTestInvisible;
														})
												]
										  + SOverlay::Slot()
												.ZOrder(8)
												[
													SAssignNew(BoardUnitArtOverlay, SOverlay)
														.Visibility_Lambda([this]() {
															if (Subsystem.IsValid() && Subsystem->Uses3DBoardTiles()) {
																return EVisibility::Collapsed;
															}
															return EVisibility::HitTestInvisible;
														})
												]
										  + SOverlay::Slot()
												.ZOrder(50)
												[
													SAssignNew(BoardDamagePopupOverlay, SOverlay)
														.Visibility_Lambda([this]() {
															if (Subsystem.IsValid() && Subsystem->Uses3DBoardTiles()) {
																return EVisibility::Collapsed;
															}
															return EVisibility::HitTestInvisible;
														})
												]
										  ]
								  ]
						  ]
				  ]
			+ SOverlay::Slot()
				  .ZOrder(11)
				  .HAlign(HAlign_Left)
				  .VAlign(VAlign_Bottom)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetHandDetailOverlayPadding))
				  [
					  SAssignNew(SelectedHandCardPanel, SBorder)
						  .Visibility(EVisibility::Collapsed)
						  .BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
						  .BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor())
						  .Padding(FMargin(10.f, 8.f))
						  [
							  SNew(SBox)
								  .WidthOverride(kHandCardPanelW)
								  .HeightOverride(kHandCardDetailPanelH)
								  [
									  SNew(SVerticalBox)
										  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
												[
													SNew(SBox)
														.HeightOverride(kHandCardDetailTitleH)
														.Clipping(EWidgetClipping::ClipToBounds)
														.VAlign(VAlign_Center)
														[
															SAssignNew(SelectedHandCardTitleText, STextBlock)
																.Font(TacticsCardText::DesignFont(TEXT("Bold"), 16))
																.ColorAndOpacity(FLinearColor(0.95f, 0.86f, 0.55f, 1.f))
																.Text(FText::FromString(TEXT("Selected")))
														]
												]
										  // Art + cost + stat block (action icons overlay spans full height; floor = stat-row bottom)
										  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 7.f)
												[
													SNew(SOverlay)
													+ SOverlay::Slot()
														[
															SNew(SVerticalBox)
															+ SVerticalBox::Slot().AutoHeight()
																[
																	SNew(SHorizontalBox)
																	+ SHorizontalBox::Slot().AutoWidth()
																		[
																			SNew(SBox).WidthOverride(kHandCardArtW).HeightOverride(kHandCardArtH)
																				[
																					SAssignNew(SelectedHandCardArtImage, SImage)
																				]
																		]
																	+ SHorizontalBox::Slot().FillWidth(1.f).Padding(10.f, 0.f, 0.f, 0.f)
																		[
																			SNew(SBox)
																				.WidthOverride(kHandCardGlossaryW)
																				.HeightOverride(kHandCardArtH)
																				[
																					SNew(SScrollBox)
																					+ SScrollBox::Slot()
																						[
																							SNew(SVerticalBox)
																							+ SVerticalBox::Slot()
																								.AutoHeight()
																								[
																									SAssignNew(SelectedHandCardActiveEffectsVBox, SVerticalBox)
																								]
																							+ SVerticalBox::Slot()
																								.AutoHeight()
																								.Padding(0.f, 6.f, 0.f, 0.f)
																								[
																									SAssignNew(SelectedHandCardGlossarySection, SBox)
																										.Visibility(EVisibility::Collapsed)
																										[
																											SAssignNew(SelectedHandCardGlossaryVBox, SVerticalBox)
																										]
																								]
																						]
																				]
																		]
																]
															+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)
																[
																	SAssignNew(SelectedHandCardCostRowBox, SBox)
																		.HeightOverride(kHandCardDetailCostRowH)
																		.VAlign(VAlign_Center)
																		[
																			SAssignNew(SelectedHandCardCostText, SRichTextBlock)
																				.WrapTextAt(kHandCardDescWrapWidth)
																				.TextStyle(&TacticsCardText::EnergyTextStyle(14, FLinearColor(0.78f, 0.83f, 0.90f, 1.f)))
																				.DecoratorStyleSet(&FCoreStyle::Get())
																				.Decorators(TacticsCardText::MakeEnergyDecorators(14))
																		]
																]
															+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 7.f, 0.f, 0.f)
																[
																	SAssignNew(SelectedHandCardStatsRowBox, SBox)
																		.HeightOverride(kHandCardDetailStatsRowH)
																		.VAlign(VAlign_Center)
																		[
																			SAssignNew(SelectedHandCardStatsColumnBox, SBox)
																				[
																					SAssignNew(SelectedHandCardStatsText, SRichTextBlock)
																						.WrapTextAt(kHandCardDescWrapWidth)
																						.TextStyle(&TacticsCardText::EnergyTextStyle(14, kCardTextWhite))
																						.DecoratorStyleSet(&FCoreStyle::Get())
																						.Decorators(TacticsCardText::MakeEnergyDecorators(14))
																				]
																		]
																]
														]
													+ SOverlay::Slot()
														.HAlign(HAlign_Right)
														.VAlign(VAlign_Bottom)
														[
															SAssignNew(SelectedHandCardActionIconsBox, SBox)
																.Visibility(EVisibility::Collapsed)
																.HAlign(HAlign_Right)
																[
																	SNew(SHorizontalBox)
																	+ SHorizontalBox::Slot()
																		.AutoWidth()
																		.VAlign(VAlign_Bottom)
																		[
																			SNew(SBox)
																				.WidthOverride(kHandCardActionIconStackSize)
																				.HeightOverride(kHandCardActionIconAreaH)
																				.Clipping(EWidgetClipping::ClipToBounds)
																				[
																					SAssignNew(SelectedHandCardMoveActionIconsVBox, SVerticalBox)
																				]
																		]
																	+ SHorizontalBox::Slot()
																		.AutoWidth()
																		.Padding(6.f, 0.f, 0.f, 0.f)
																		.VAlign(VAlign_Bottom)
																		[
																			SNew(SBox)
																				.WidthOverride(kHandCardActionIconStackSize)
																				.HeightOverride(kHandCardActionIconAreaH)
																				.Clipping(EWidgetClipping::ClipToBounds)
																				[
																					SAssignNew(SelectedHandCardAttackActionIconsVBox, SVerticalBox)
																				]
																		]
																	+ SHorizontalBox::Slot()
																		.AutoWidth()
																		.Padding(6.f, 0.f, 0.f, 0.f)
																		.VAlign(VAlign_Bottom)
																		[
																			SNew(SBox)
																				.WidthOverride(kHandCardActionIconStackSize)
																				.HeightOverride(kHandCardActionIconAreaH)
																				.Clipping(EWidgetClipping::ClipToBounds)
																				[
																					SAssignNew(SelectedHandCardReactionActionIconsVBox, SVerticalBox)
																				]
																		]
																]
														]
												]
										  // Hairline divider between cost/stats and rules
										  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
										  	[
										  		SNew(SBox)
										  			.HeightOverride(kHandCardDetailDividerBlockH)
										  			.VAlign(VAlign_Center)
										  			[
										  				SAssignNew(SelectedHandCardDivider, SBox).HeightOverride(1.f)
										  					[
										  						SNew(SImage)
										  							.Image(CardChromeFillBrush())
										  							.ColorAndOpacity(kCardRulesDividerColor)
										  					]
										  			]
										  	]
										  // Rules text
										  + SVerticalBox::Slot().FillHeight(1.f)
										  	[
										  		SNew(SBox)
										  			.HeightOverride(kHandCardDetailRulesH)
										  			[
										  				SNew(SScrollBox)
										  				+ SScrollBox::Slot()
										  					[
										  						SAssignNew(SelectedHandCardDescVBox, SVerticalBox)
										  					]
										  			]
										  	]
										  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
										  	[
										  		SAssignNew(SelectedUnitAbilityHotbarBox, SBox)
										  			.Visibility(EVisibility::Collapsed)
										  			.WidthOverride(kHandCardPanelW)
										  			.MaxDesiredHeight(kHandCardDetailAbilityHotbarH)
										  			[
										  				SAssignNew(SelectedUnitAbilityHotbar, SWrapBox)
										  					.UseAllottedSize(true)
										  			]
										  	]
								  ]
						  ]
				  ]
			// Right rail: Available Energy (always) above Territories chip / roster.
			+ SOverlay::Slot()
				  .ZOrder(10)
				  .HAlign(HAlign_Right)
				  .VAlign(VAlign_Top)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetReservesStripOverlayPadding))
				  [
					  SNew(SBox)
						  .WidthOverride(kEnergyReservesPanelWidth)
						  .MaxDesiredHeight(720.f)
						  [
							  SNew(SVerticalBox)
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
									[
										SAssignNew(EnergyHudChrome, SBox)
											.Visibility(TAttribute<EVisibility>::CreateSP(
												this, &STacticsBoardPanel::GetEnergyHudVisibility))
											[
												SNew(SBorder)
													.BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
													.BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor())
													.Padding(FMargin(8.f, 6.f))
													[
														SNew(SVerticalBox)
														+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
															[
																SNew(STextBlock)
																	.Text(FText::FromString(TEXT("Available Energy")))
																	.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
																	.ColorAndOpacity(kCardTextWhite)
															]
														+ SVerticalBox::Slot().AutoHeight()
															[
																SAssignNew(EnergyCountersBox, SVerticalBox)
															]
													]
											]
									]
							  + SVerticalBox::Slot().AutoHeight()
									[
										SNew(SOverlay)
										+ SOverlay::Slot()
											  .HAlign(HAlign_Right)
											  .VAlign(VAlign_Top)
											  [
												  SAssignNew(TerritoriesChipChrome, SBox)
													  .WidthOverride(kRailChipW)
													  .Visibility(TAttribute<EVisibility>::CreateSP(
														  this, &STacticsBoardPanel::GetTerritoriesChipVisibility))
													  [
														  SNew(SButton)
															  .ButtonColorAndOpacity(FLinearColor(0.10f, 0.14f, 0.12f, 0.95f))
															  .ForegroundColor(kCardTextWhite)
															  .ContentPadding(FMargin(4.f, 8.f))
															  .ToolTipText(FText::FromString(
																  TEXT("Open Territories - your lands (1 use each per turn).")))
															  .OnClicked_Lambda([this]() {
																  SetTerritoriesRailExpanded(true, true);
																  Refresh();
																  return FReply::Handled();
															  })
															  [
																  SNew(STextBlock)
																	  .Justification(ETextJustify::Center)
																	  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 8))
																	  .ColorAndOpacity(kCardTextWhite)
																	  .Text(TAttribute<FText>::CreateSP(
																		  this, &STacticsBoardPanel::GetTerritoriesChipLabel))
															  ]
													  ]
											  ]
										+ SOverlay::Slot()
											  .HAlign(HAlign_Fill)
											  .VAlign(VAlign_Top)
											  [
												  SAssignNew(TerritoriesExpandedChrome, SBox)
													  .MaxDesiredHeight(520.f)
													  .Visibility(TAttribute<EVisibility>::CreateSP(
														  this, &STacticsBoardPanel::GetTerritoriesExpandedVisibility))
													  [
														  SNew(SBorder)
															  .BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
															  .BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor())
															  .Padding(FMargin(8.f, 6.f))
															  [
																  SNew(SVerticalBox)
																  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
																		[
																			SNew(SHorizontalBox)
																			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
																				[
																					SNew(STextBlock)
																						.Text(FText::FromString(TEXT("Territories")))
																						.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
																						.ColorAndOpacity(kCardTextWhite)
																				]
																			+ SHorizontalBox::Slot().AutoWidth()
																				[
																					SNew(SButton)
																						.ButtonColorAndOpacity(FLinearColor(0.16f, 0.16f, 0.18f, 1.f))
																						.ForegroundColor(kCardTextWhite)
																						.ContentPadding(FMargin(6.f, 2.f))
																						.ToolTipText(FText::FromString(TEXT("Collapse Territories")))
																						.OnClicked_Lambda([this]() {
																							SetTerritoriesRailExpanded(false, true);
																							Refresh();
																							return FReply::Handled();
																						})
																						[
																							SNew(STextBlock)
																								.Text(FText::FromString(TEXT("▼")))
																								.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
																								.ColorAndOpacity(kCardTextWhite)
																						]
																				]
																		]
																  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
																		[
																			SNew(STextBlock)
																				.WrapTextAt(kEnergyReservesPanelWidth - 12.f)
																				.Text(FText::FromString(
																					TEXT("Tap a land to show its abilities.")))
																				.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 8))
																				.ColorAndOpacity(kCardTextWhite)
																		]
																  + SVerticalBox::Slot().AutoHeight()
																		[
																			SNew(SBox)
																				.MaxDesiredHeight(460.f)
																				[
																					SNew(SScrollBox).Orientation(Orient_Vertical)
																					+ SScrollBox::Slot()
																						  [SAssignNew(TerritoryBar, SVerticalBox)]
																				]
																		]
															  ]
													  ]
											  ]
									]
						  ]
				  ]
			// Reserves column (left) - collapsed chip or expanded list. Energy lives in the top HUD.
			+ SOverlay::Slot()
				  .ZOrder(12)
				  .HAlign(HAlign_Left)
				  .VAlign(VAlign_Fill)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetReservesColumnPadding))
				  [
					  SNew(SOverlay)
					  + SOverlay::Slot()
							.HAlign(HAlign_Left)
							.VAlign(VAlign_Top)
							[
								SAssignNew(ReservesChipChrome, SBox)
									.WidthOverride(kRailChipW)
									.Visibility(TAttribute<EVisibility>::CreateSP(
										this, &STacticsBoardPanel::GetReservesChipVisibility))
									[
										SNew(SButton)
											.ButtonColorAndOpacity(FLinearColor(0.14f, 0.12f, 0.08f, 0.95f))
											.ForegroundColor(kCardTextWhite)
											.ContentPadding(FMargin(4.f, 8.f))
											.ToolTipText(FText::FromString(
												TEXT("Open Reserves - owner-turn sideboard cards.")))
											.OnClicked_Lambda([this]() {
												SetReservesRailExpanded(true, true);
												Refresh();
												return FReply::Handled();
											})
											[
												SNew(STextBlock)
													.Justification(ETextJustify::Center)
													.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 8))
													.ColorAndOpacity(kCardTextWhite)
													.Text(TAttribute<FText>::CreateSP(
														this, &STacticsBoardPanel::GetReservesChipLabel))
											]
									]
							]
					  + SOverlay::Slot()
							.HAlign(HAlign_Left)
							.VAlign(VAlign_Fill)
							[
								SAssignNew(ReservesExpandedChrome, SBox)
									.WidthOverride(kReservesColumnW)
									.Visibility(TAttribute<EVisibility>::CreateSP(
										this, &STacticsBoardPanel::GetReservesExpandedVisibility))
									[
										SNew(SBorder)
											.BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
											.BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor())
											.Padding(FMargin(8.f, 6.f))
											[
												SNew(SVerticalBox)
												+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
													[
														SNew(SHorizontalBox)
														+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
															[
																SNew(STextBlock)
																	.Text(FText::FromString(TEXT("Reserves")))
																	.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
																	.ColorAndOpacity(kCardTextWhite)
															]
														+ SHorizontalBox::Slot().AutoWidth()
															[
																SNew(SButton)
																	.ButtonColorAndOpacity(FLinearColor(0.16f, 0.16f, 0.18f, 1.f))
																	.ForegroundColor(kCardTextWhite)
																	.ContentPadding(FMargin(6.f, 2.f))
																	.ToolTipText(FText::FromString(TEXT("Collapse Reserves")))
																	.OnClicked_Lambda([this]() {
																		SetReservesRailExpanded(false, true);
																		Refresh();
																		return FReply::Handled();
																	})
																	[
																		SNew(STextBlock)
																			.Text(FText::FromString(TEXT("▼")))
																			.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
																			.ColorAndOpacity(kCardTextWhite)
																	]
															]
													]
												+ SVerticalBox::Slot().FillHeight(1.f)
													[
														SAssignNew(ReservesScroll, SScrollBox).Orientation(Orient_Vertical)
													]
											]
									]
							]
				  ]
			+ SOverlay::Slot()
				  .ZOrder(10)
				  .HAlign(HAlign_Fill)
				  .VAlign(VAlign_Bottom)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetHandStripOverlayPadding))
				  [
					  SAssignNew(HandScroll, SScrollBox).Orientation(Orient_Horizontal)
				  ]
			// Game over screen (victory / defeat / draw). Top-most; shown only once the match is decided
			// - team-aware, so in 2v2 it stays hidden while your team still has a living base.
			+ SOverlay::Slot()
				  .ZOrder(200)
				  .HAlign(HAlign_Fill)
				  .VAlign(VAlign_Fill)
				  [
					  SNew(SBorder)
						  .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.85f))
						  .HAlign(HAlign_Center)
						  .VAlign(VAlign_Center)
						  .Visibility_Lambda([this]() {
							  return (Subsystem.IsValid()
										  && Subsystem->GetControlledMatchResult() != ETacticsMatchResult::InProgress)
										  ? EVisibility::Visible
										  : EVisibility::Collapsed;
						  })
						  [
							  SNew(SVerticalBox)
							  + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 10.f)
									[SNew(STextBlock)
											.Font(TacticsCardText::DesignFont(TEXT("Bold"), 52))
											.Justification(ETextJustify::Center)
											.ColorAndOpacity_Lambda([this]() {
												switch (Subsystem.IsValid() ? Subsystem->GetControlledMatchResult()
																			 : ETacticsMatchResult::InProgress) {
												case ETacticsMatchResult::Win:
													return FLinearColor(0.55f, 0.95f, 0.62f, 1.f);
												case ETacticsMatchResult::Loss:
													return FLinearColor(0.96f, 0.46f, 0.42f, 1.f);
												default:
													return FLinearColor(0.86f, 0.86f, 0.88f, 1.f);
												}
											})
											.Text_Lambda([this]() {
												switch (Subsystem.IsValid() ? Subsystem->GetControlledMatchResult()
																			 : ETacticsMatchResult::InProgress) {
												case ETacticsMatchResult::Win: return FText::FromString(TEXT("VICTORY"));
												case ETacticsMatchResult::Loss: return FText::FromString(TEXT("DEFEAT"));
												case ETacticsMatchResult::Draw: return FText::FromString(TEXT("DRAW"));
												default: return FText::GetEmpty();
												}
											})]
							  + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0.f, 0.f, 0.f, 24.f)
									[SNew(STextBlock)
											.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 13))
											.Justification(ETextJustify::Center)
											.ColorAndOpacity(FLinearColor(0.82f, 0.86f, 0.92f, 1.f))
											.Text_Lambda([this]() {
												switch (Subsystem.IsValid() ? Subsystem->GetControlledMatchResult()
																			 : ETacticsMatchResult::InProgress) {
												case ETacticsMatchResult::Win:
													return FText::FromString(TEXT("All enemy bases destroyed."));
												case ETacticsMatchResult::Loss:
													return FText::FromString(TEXT("Your team's bases were destroyed."));
												default:
													return FText::FromString(TEXT("The match ended in a draw."));
												}
											})]
							  + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
									[SNew(SBox).WidthOverride(240.f)
											[SNew(SButton)
													.HAlign(HAlign_Center)
													.ContentPadding(FMargin(14.f, 9.f))
													.OnClicked_Lambda([this]() {
														if (Subsystem.IsValid()) {
															if (UTacticsGameInstance* GI = Cast<UTacticsGameInstance>(
																	Subsystem->GetGameInstance())) {
																GI->ShowMainMenu();
															}
														}
														return FReply::Handled();
													})
													[SNew(STextBlock)
															.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 14))
															.Justification(ETextJustify::Center)
															.Text(FText::FromString(TEXT("Back to menu")))]]]
						  ]
				  ]
			+ SOverlay::Slot()
				  .ZOrder(20)
				  .HAlign(HAlign_Right)
				  .VAlign(VAlign_Bottom)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetBottomRightOverlayPadding))
				  [
					  SNew(SBorder)
						  .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 1.f))
						  .Padding(FMargin(10.f, 8.f))
						  [
							  SAssignNew(BottomRightBar, SVerticalBox)
						  ]
				  ]
			// Top-center phase banner: current phase + acting player, updates dynamically for all seats.
			+ SOverlay::Slot()
				  .ZOrder(30)
				  .HAlign(HAlign_Center)
				  .VAlign(VAlign_Top)
				  .Padding(FMargin(0.f, 12.f, 0.f, 0.f))
				  [
					  SNew(SBorder)
						  .BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
						  .BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor())
						  .Padding(FMargin(24.f, 9.f))
						  .VAlign(VAlign_Center)
						  [
							  SNew(STextBlock)
								  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 15))
								  .ColorAndOpacity_Lambda([this]() {
									  return IsWaitingForControlledSeatTurn()
										  ? FLinearColor(0.72f, 0.72f, 0.72f, 1.f)
										  : FLinearColor(0.95f, 0.86f, 0.55f, 1.f);
								  })
								  .Justification(ETextJustify::Center)
								  .Text_Lambda([this]() {
									  return Subsystem.IsValid()
										  ? FText::FromString(Subsystem->GetPhaseBannerText())
										  : FText::GetEmpty();
								  })
						  ]
				  ]
			// Armed ability targeting banner (group-specific prompt).
			+ SOverlay::Slot()
				  .ZOrder(31)
				  .HAlign(HAlign_Center)
				  .VAlign(VAlign_Top)
				  .Padding(FMargin(0.f, 52.f, 0.f, 0.f))
				  [
					  SNew(SBorder)
						  .Visibility_Lambda([this]() {
							  return bAbilityArmedForTile && Subsystem.IsValid() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
						  })
						  .BorderBackgroundColor_Lambda([this]() {
							  if (!Subsystem.IsValid() || ArmedAbilityKey.IsEmpty()) {
								  return FSlateColor(FLinearColor(0.32f, 0.18f, 0.48f, 1.f));
							  }
							  const ETacticsAbilityVisualGroup Group = Subsystem->ResolveAbilityVisualGroup(ArmedAbilityKey);
							  return FSlateColor(TacticsAbilityVisual::FrameColor(Group, true));
						  })
						  .Padding(FMargin(2.f))
						  [
							  SNew(SBorder)
								  .BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
								  .BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor())
								  .Padding(FMargin(18.f, 6.f))
								  [
									  SNew(STextBlock)
										  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 12))
										  .ColorAndOpacity(FLinearColor(0.92f, 0.78f, 0.98f, 1.f))
										  .Justification(ETextJustify::Center)
										  .Text_Lambda([this]() {
											  if (!Subsystem.IsValid() || ArmedAbilityKey.IsEmpty()) {
												  return FText::GetEmpty();
											  }
											  const ETacticsAbilityVisualGroup Group = Subsystem->ResolveAbilityVisualGroup(ArmedAbilityKey);
											  return FText::FromString(FString::Printf(
												  TEXT("%s - %s"),
												  *TacticsAbilityVisual::GroupShortLabel(Group),
												  *TacticsAbilityVisual::GroupPrompt(Group)));
										  })
								  ]
						  ]
				  ]
			// Unit hover (3D: screen-anchored under unit). Cell-only hints stay bottom-center.
			+ SOverlay::Slot()
				  .ZOrder(28)
				  .HAlign(HAlign_Left)
				  .VAlign(VAlign_Top)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetUnitHoverScreenOverlayPadding))
				  [
					  SAssignNew(UnitHoverScreenOverlay, SBox)
						  .Visibility(EVisibility::Collapsed)
				  ]
			+ SOverlay::Slot()
				  .ZOrder(27)
				  .HAlign(HAlign_Center)
				  .VAlign(VAlign_Bottom)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetUnitHoverCellHintPadding))
				  [
					  SNew(SBorder)
						  .Visibility_Lambda([this]() {
							  return HoverCellHintLine.IsEmpty() ? EVisibility::Collapsed : EVisibility::HitTestInvisible;
						  })
						  .BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
						  .BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor())
						  .Padding(FMargin(16.f, 8.f))
						  [
							  SAssignNew(UnitHoverCellHintLabel, STextBlock)
								  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 12))
								  .ColorAndOpacity(kCardTextWhite)
								  .Justification(ETextJustify::Center)
						  ]
				  ]
			// Waiting-for-turn click catcher. The default SBorder brush is an opaque grey box, so
			// we use WhiteBrush + 0 alpha in 3D (watch the AI on the board, still block clicks).
			+ SOverlay::Slot()
				  .ZOrder(12)
				  .HAlign(HAlign_Fill)
				  .VAlign(VAlign_Fill)
				  .Padding(TAttribute<FMargin>::CreateSP(this, &STacticsBoardPanel::GetCenterGridOverlayPadding))
				  [
					  SAssignNew(WaitingTurnOverlay, SBox)
						  .Visibility_Lambda([this]() {
							  return IsWaitingForControlledSeatTurn() ? EVisibility::Visible : EVisibility::Collapsed;
						  })
						  .HAlign(HAlign_Fill)
						  .VAlign(VAlign_Fill)
						  [
							  SNew(SBorder)
								  .BorderImage(FCoreStyle::Get().GetBrush("WhiteBrush"))
								  .BorderBackgroundColor_Lambda([this]() {
									  return Uses3DBoardChrome()
										  ? FLinearColor(0.f, 0.f, 0.f, 0.f)
										  : FLinearColor(0.f, 0.f, 0.f, 0.28f);
								  })
								  .Padding(0.f)
						  ]
				  ]
			// Upper-center failure alert toast: shows why an action/cast was rejected, then fades out.
			+ SOverlay::Slot()
				  .ZOrder(40)
				  .HAlign(HAlign_Center)
				  .VAlign(VAlign_Top)
				  .Padding(FMargin(0.f, 96.f, 0.f, 0.f))
				  [
					  SNew(SBorder)
						  .Visibility_Lambda([this]() {
							  return FailureAlertCurve.IsPlaying() ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
						  })
						  .BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
						  .BorderBackgroundColor_Lambda([this]() {
							  const float L = FailureAlertCurve.GetLerp();
							  const float A = (L < 0.70f) ? 1.f : FMath::Clamp(1.f - (L - 0.70f) / 0.30f, 0.f, 1.f);
							  return FSlateColor(TacticsCardText::SolidPopupPanelFillColor(A));
						  })
						  .Padding(FMargin(26.f, 14.f))
						  [
							  SNew(STextBlock)
								  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 15))
								  .Justification(ETextJustify::Center)
								  .WrapTextAt(460.f)
								  .ColorAndOpacity_Lambda([this]() {
									  const float L = FailureAlertCurve.GetLerp();
									  const float A = (L < 0.70f) ? 1.f : FMath::Clamp(1.f - (L - 0.70f) / 0.30f, 0.f, 1.f);
									  return FSlateColor(FLinearColor(1.f, 0.13f, 0.10f, A));
								  })
								  .Text_Lambda([this]() { return FText::FromString(FailureAlertText); })
						  ]
				  ]
			// End-turn discard modal.
			+ SOverlay::Slot()
				  .ZOrder(45)
				  .HAlign(HAlign_Center)
				  .VAlign(VAlign_Center)
				  [
					  SAssignNew(DiscardModalChrome, SBox)
						  .Visibility_Lambda([this]() {
							  return (Subsystem.IsValid() && Subsystem->IsAwaitingHandDiscard())
								  ? EVisibility::Visible : EVisibility::Collapsed;
						  })
						  .WidthOverride(420.f)
						  [
							  SNew(SBorder)
								  .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 1.f))
								  .Padding(FMargin(16.f, 14.f))
								  [
									  SNew(SVerticalBox)
									  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)
										  [
											  SNew(STextBlock)
												  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 13))
												  .ColorAndOpacity(kCardTextWhite)
												  .Text_Lambda([this]() {
													  if (!Subsystem.IsValid()) {
														  return FText::GetEmpty();
													  }
													  return FText::FromString(FString::Printf(
														  TEXT("Discard to 8 cards (P%d) - click a card, then Discard"),
														  Subsystem->GetHandViewPlayerId()));
												  })
										  ]
									  + SVerticalBox::Slot().AutoHeight()
										  [
											  SNew(SBox).MaxDesiredHeight(280.f)
												  [
													  SAssignNew(DiscardModalList, SVerticalBox)
												  ]
										  ]
								  ]
						  ]
				  ]
			// Modal spell mode picker (MTG Arena-style duplicate cards).
			+ SOverlay::Slot()
				  .ZOrder(46)
				  .HAlign(HAlign_Center)
				  .VAlign(VAlign_Center)
				  [
					  SAssignNew(ModalSpellPickerChrome, SBox)
						  .Visibility_Lambda([this]() {
							  return bModalSpellPickerActive ? EVisibility::Visible : EVisibility::Collapsed;
						  })
						  [
							  SNew(SBorder)
								  .BorderImage(TacticsCardText::SolidPopupPanelBorderImage())
								  .BorderBackgroundColor(TacticsCardText::SolidPopupPanelFillColor(0.96f))
								  .Padding(FMargin(20.f, 16.f))
								  [
									  SNew(SVerticalBox)
									  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
										  [
											  SNew(STextBlock)
												  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 14))
												  .ColorAndOpacity(kCardTextWhite)
												  .Justification(ETextJustify::Center)
												  .Text(FText::FromString(TEXT("Choose one")))
										  ]
									  + SVerticalBox::Slot().AutoHeight()
										  [
											  SAssignNew(ModalSpellPickerRow, SHorizontalBox)
										  ]
								  ]
						  ]
				  ]
			// Bottom-left container: display options (Advanced descriptions toggle).
			+ SOverlay::Slot()
				  .ZOrder(19)
				  .HAlign(HAlign_Left)
				  .VAlign(VAlign_Bottom)
				  .Padding(FMargin(12.f, 0.f, 0.f, 168.f))
				  [
					  SAssignNew(CombatLogChrome, SBox)
						  .Visibility_Lambda([this]() {
							  return bShowCombatLog ? EVisibility::Visible : EVisibility::Collapsed;
						  })
						  .MaxDesiredWidth(300.f)
						  .MaxDesiredHeight(160.f)
						  [
							  SNew(SBorder)
								  .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 1.f))
								  .Padding(FMargin(8.f, 6.f))
								  [
									  SNew(SVerticalBox)
									  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
										  [
											  SNew(STextBlock)
												  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
												  .ColorAndOpacity(kCardTextWhite)
												  .Text(FText::FromString(TEXT("Combat log")))
										  ]
									  + SVerticalBox::Slot().FillHeight(1.f)
										  [
											  SAssignNew(CombatLogScroll, SScrollBox).Orientation(Orient_Vertical)
										  ]
								  ]
						  ]
				  ]
			+ SOverlay::Slot()
				  .ZOrder(20)
				  .HAlign(HAlign_Left)
				  .VAlign(VAlign_Bottom)
				  .Padding(FMargin(12.f, 0.f, 0.f, 12.f))
				  [
					  SNew(SBorder)
						  .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 1.f))
						  .Padding(FMargin(10.f, 8.f))
						  [
							  SNew(SVerticalBox)
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)
								  [
									  SNew(SVerticalBox)
									  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
										  [
											  SNew(STextBlock)
												  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
												  .ColorAndOpacity(kCardTextWhite)
												  .Text(FText::FromString(TEXT("Control seat")))
										  ]
									  + SVerticalBox::Slot().AutoHeight()
										  [
											  SAssignNew(SeatSwitcherRow, SHorizontalBox)
										  ]
								  ]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
								  [
									  SNew(SCheckBox)
										  .IsChecked_Lambda([this]() {
											  return bShowCombatLog ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
										  })
										  .OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState) {
											  bShowCombatLog = (NewState == ECheckBoxState::Checked);
											  SaveBoardUiPreferences();
											  if (CombatLogChrome.IsValid()) {
												  CombatLogChrome->Invalidate(EInvalidateWidget::Visibility);
											  }
										  })
										  .ToolTipText(FText::FromString(TEXT("Show filtered combat events (damage, casts, defeats).")))
										  [
											  SNew(STextBlock)
												  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
												  .ColorAndOpacity(kCardTextWhite)
												  .Text(FText::FromString(TEXT("Combat log")))
										  ]
								  ]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
								  [
									  SNew(SCheckBox)
										  .IsChecked_Lambda([this]() {
											  return (Subsystem.IsValid() && Subsystem->IsShowingAdvancedCardText())
												  ? ECheckBoxState::Checked
												  : ECheckBoxState::Unchecked;
										  })
										  .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) {
											  if (Subsystem.IsValid()) {
												  Subsystem->SetShowAdvancedCardText(NewState == ECheckBoxState::Checked);
											  }
											  SaveBoardUiPreferences();
										  })
										  .ToolTipText(FText::FromString(TEXT("Append edge-case clarifications below the normal description. Off = normal text only.")))
										  [
											  SNew(STextBlock)
												  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
												  .ColorAndOpacity(kCardTextWhite)
												  .Text(FText::FromString(TEXT("Advanced descriptions")))
										  ]
								  ]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
								  [
									  SNew(SBox)
										  .Visibility_Lambda([this]() {
											  return bShowDevTools ? EVisibility::Visible : EVisibility::Collapsed;
										  })
										  [
											  SNew(SCheckBox)
												  .IsChecked_Lambda([this]() {
													  return bShowConsoleLog ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
												  })
												  .OnCheckStateChanged_Lambda([this](ECheckBoxState NewState) {
													  bShowConsoleLog = (NewState == ECheckBoxState::Checked);
													  SaveBoardUiPreferences();
													  Invalidate(EInvalidateWidget::Visibility);
												  })
												  .ToolTipText(FText::FromString(TEXT(
													  "Show the top-left CLI / phase log (target cell, armed state, last command output).")))
												  [
													  SNew(STextBlock)
														  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
														  .ColorAndOpacity(kCardTextWhite)
														  .Text(FText::FromString(TEXT("Show console log")))
												  ]
										  ]
								  ]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
								  [
									  SNew(SCheckBox)
										  .IsChecked_Lambda([this]() {
											  return bP2BotEnabled ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
										  })
										  .IsEnabled_Lambda([this]() {
											  if (IsWebSocketClientP2()) {
												  return false;
											  }
											  if (Subsystem.IsValid()) {
												  if (const UGameInstance* GI = Subsystem->GetGameInstance()) {
													  if (const UTacticsWebSocketSubsystem* Net = GI->GetSubsystem<UTacticsWebSocketSubsystem>()) {
														  if (Net->IsHosting() && Net->GetRemoteWebSocketReadyPeerCount() > 0) {
															  return false;
														  }
													  }
												  }
											  }
											  return true;
										  })
										  .OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState) {
											  bP2BotEnabled = (NewState == ECheckBoxState::Checked);
											  SaveBoardUiPreferences();
											  ApplyBotSettingsFromPreferences();
										  })
										  .ToolTipText(FText::FromString(TEXT(
											  "Player 2 is driven by the headless MCTS bot (local solo only). You stay on P1; the bot acts on its own ticker.")))
										  [
											  SNew(STextBlock)
												  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
												  .ColorAndOpacity(kCardTextWhite)
												  .Text(FText::FromString(TEXT("P2 bot (MCTS)")))
										  ]
								  ]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
								  [
									  SNew(SBox)
										  .Visibility_Lambda([this]() {
											  return bShowDevTools ? EVisibility::Visible : EVisibility::Collapsed;
										  })
										  [
											  SNew(STextBlock)
												  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
												  .ColorAndOpacity(kCardTextWhite)
												  .Text(FText::FromString(TEXT("Match settings")))
										  ]
								  ]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
							  	  [
							  		  SNew(SBox)
										  .Visibility_Lambda([this]() {
											  return bShowDevTools ? EVisibility::Visible : EVisibility::Collapsed;
										  })
										  [
							  		  SNew(SCheckBox)
							  			  .IsChecked_Lambda([this]() {
							  				  return (Subsystem.IsValid() && Subsystem->GetAllowDeploymentUndo())
							  					  ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
							  			  })
							  			  .OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState) {
							  				  if (Subsystem.IsValid()) {
							  					  Subsystem->SetAllowDeploymentUndo(NewState == ECheckBoxState::Checked);
							  				  }
							  			  })
							  			  .ToolTipText(FText::FromString(TEXT(
							  				  "Allow Undo to reverse a deployment (synced match setting). Off = deploying seals your prior actions. Burst stays non-undoable.")))
							  			  [
							  				  SNew(STextBlock)
							  					  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
							  					  .ColorAndOpacity(kCardTextWhite)
							  				  .Text(FText::FromString(TEXT("Allow deploy undo")))
							  			  ]
										  ]
							  	  ]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 2.f)
								  [
									  SNew(SBox)
										  .Visibility_Lambda([this]() {
											  return bShowDevTools ? EVisibility::Visible : EVisibility::Collapsed;
										  })
										  [
											  SNew(SVerticalBox)
											  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
												  [
													  SNew(STextBlock)
														  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
														  .ColorAndOpacity(kCardTextWhite)
														  .Text(FText::FromString(TEXT("Team assignment (CLI: team / teams)")))
												  ]
											  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
												  [
													  SNew(SHorizontalBox)
													  + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
														  [
															  SNew(STextBlock)
																  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
																  .ColorAndOpacity(kCardTextWhite)
																  .Text(FText::FromString(TEXT("P1")))
														  ]
													  + SHorizontalBox::Slot().FillWidth(1.f)
														  [
															  SNew(SEditableTextBox)
																  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
																  .Visibility_Lambda([this]() {
																	  return (Subsystem.IsValid() && Subsystem->GetMatchPlayerSeats().Contains(1))
																		  ? EVisibility::Visible : EVisibility::Collapsed;
																  })
																  .Text_Lambda([this]() {
																	  return Subsystem.IsValid()
																		  ? FText::AsNumber(Subsystem->GetTeamForSeat(1)) : FText::GetEmpty();
																  })
																  .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) {
																	  if (Subsystem.IsValid()) {
																		  Subsystem->SetTeamForSeat(1, FCString::Atoi(*Text.ToString()));
																		  RefreshStatusText();
																	  }
																  })
														  ]
												  ]
											  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
												  [
													  SNew(SHorizontalBox)
													  + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
														  [
															  SNew(STextBlock)
																  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
																  .ColorAndOpacity(kCardTextWhite)
																  .Text(FText::FromString(TEXT("P2")))
														  ]
													  + SHorizontalBox::Slot().FillWidth(1.f)
														  [
															  SNew(SEditableTextBox)
																  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
																  .Visibility_Lambda([this]() {
																	  return (Subsystem.IsValid() && Subsystem->GetMatchPlayerSeats().Contains(2))
																		  ? EVisibility::Visible : EVisibility::Collapsed;
																  })
																  .Text_Lambda([this]() {
																	  return Subsystem.IsValid()
																		  ? FText::AsNumber(Subsystem->GetTeamForSeat(2)) : FText::GetEmpty();
																  })
																  .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) {
																	  if (Subsystem.IsValid()) {
																		  Subsystem->SetTeamForSeat(2, FCString::Atoi(*Text.ToString()));
																		  RefreshStatusText();
																	  }
																  })
														  ]
												  ]
											  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 2.f)
												  [
													  SNew(SHorizontalBox)
													  + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
														  [
															  SNew(STextBlock)
																  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
																  .ColorAndOpacity(kCardTextWhite)
																  .Text(FText::FromString(TEXT("P3")))
														  ]
													  + SHorizontalBox::Slot().FillWidth(1.f)
														  [
															  SNew(SEditableTextBox)
																  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
																  .Visibility_Lambda([this]() {
																	  return (Subsystem.IsValid() && Subsystem->GetMatchPlayerSeats().Contains(3))
																		  ? EVisibility::Visible : EVisibility::Collapsed;
																  })
																  .Text_Lambda([this]() {
																	  return Subsystem.IsValid()
																		  ? FText::AsNumber(Subsystem->GetTeamForSeat(3)) : FText::GetEmpty();
																  })
																  .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) {
																	  if (Subsystem.IsValid()) {
																		  Subsystem->SetTeamForSeat(3, FCString::Atoi(*Text.ToString()));
																		  RefreshStatusText();
																	  }
																  })
														  ]
												  ]
											  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
												  [
													  SNew(SHorizontalBox)
													  + SHorizontalBox::Slot().AutoWidth().Padding(0.f, 0.f, 6.f, 0.f)
														  [
															  SNew(STextBlock)
																  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
																  .ColorAndOpacity(kCardTextWhite)
																  .Text(FText::FromString(TEXT("P4")))
														  ]
													  + SHorizontalBox::Slot().FillWidth(1.f)
														  [
															  SNew(SEditableTextBox)
																  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
																  .Visibility_Lambda([this]() {
																	  return (Subsystem.IsValid() && Subsystem->GetMatchPlayerSeats().Contains(4))
																		  ? EVisibility::Visible : EVisibility::Collapsed;
																  })
																  .Text_Lambda([this]() {
																	  return Subsystem.IsValid()
																		  ? FText::AsNumber(Subsystem->GetTeamForSeat(4)) : FText::GetEmpty();
																  })
																  .OnTextCommitted_Lambda([this](const FText& Text, ETextCommit::Type) {
																	  if (Subsystem.IsValid()) {
																		  Subsystem->SetTeamForSeat(4, FCString::Atoi(*Text.ToString()));
																		  RefreshStatusText();
																	  }
																  })
														  ]
												  ]
										  ]
								  ]
							  + SVerticalBox::Slot().AutoHeight()
								  [
									  SAssignNew(SelectedHandCardGlossaryCheckBox, SCheckBox)
										  .IsChecked_Lambda([this]() {
											  return bShowCardGlossaryDefinitions
												  ? ECheckBoxState::Checked
												  : ECheckBoxState::Unchecked;
										  })
										  .OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState) {
											  bShowCardGlossaryDefinitions = (NewState == ECheckBoxState::Checked);
											  SaveBoardUiPreferences();
											  RefreshSelectedHandCardPanel();
										  })
										  .ToolTipText(FText::FromString(TEXT(
											  "Show glossary definition blocks beside the art. Inline hovers still work when off.")))
										  [
											  SNew(STextBlock)
												  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
												  .ColorAndOpacity(kCardTextWhite)
												  .Text(FText::FromString(TEXT("Glossary definitions")))
										  ]
								  ]
							  + SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
								  [
									  SNew(SCheckBox)
										  .IsChecked_Lambda([this]() {
											  return bShowDevTools ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
										  })
										  .OnCheckStateChanged_Lambda([this](const ECheckBoxState NewState) {
											  bShowDevTools = (NewState == ECheckBoxState::Checked);
											  if (!bShowDevTools) {
												  bShowConsoleLog = false;
												  bShowDiscardPilePanel = false;
												  bShowPurgatoryPanel = false;
												  RebuildDiscardPilePanel();
												  RebuildPurgatoryPanel();
											  }
											  SaveBoardUiPreferences();
											  RebuildActionBar();
											  RebuildBottomRightBar();
											  Invalidate(EInvalidateWidget::Visibility);
										  })
										  .ToolTipText(FText::FromString(TEXT(
											  "Show CLI shortcuts (Control Pn, Hand, Zones), WebSocket host/client, reset buttons, match/team editors, and zone pile panels.")))
										  [
											  SNew(STextBlock)
												  .Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
												  .ColorAndOpacity(kCardTextWhite)
												  .Text(FText::FromString(TEXT("Dev tools")))
										  ]
								  ]
						  ]
				  ]
		];

	Refresh();
}

