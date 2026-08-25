#pragma once

#include "CoreMinimal.h"
#include "InputCoreTypes.h"
#include "Animation/CurveSequence.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCompoundWidget.h"
#include "TacticsGlossaryMarkup.h"
#include "TacticsMatchSubsystem.h"
#include "TacticsCardText.h"

class UTacticsMatchSubsystem;
class UTacticsWebSocketSubsystem;
struct FTacticsStackItemUi;
class SOverlay;
class SBox;
class SHorizontalBox;
class SScrollBox;
class SImage;
class STextBlock;
class SVerticalBox;

/** Viewport overlay: contextual actions + merged board grid. */
class STacticsBoardPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(STacticsBoardPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<UTacticsMatchSubsystem>, Subsystem)
	SLATE_END_ARGS()

	/** Builds the match HUD overlay. */
	void Construct(const FArguments& InArgs);
	virtual ~STacticsBoardPanel();

	/** True when the cursor is over the hand scroll strip. Polled by the player controller to suppress map zoom. */
	static bool IsHandScrollHovered();
	/** True when board unit/cell hover should be suppressed (chrome, overlays, off-grid cursor). */
	static bool ShouldSuppressBoardHoverUnderCursor();

	/** Same cell targeting as the Slate grid (3D traces call this). */
	void HandleWorldGridCellClicked(int WorldX, int WorldY, bool bShiftHeld = false);
	/** Select/inspect a board unit and fill the left detail panel (allowed off-turn). */
	void InspectBoardUnitAtWorld(int WorldX, int WorldY);
	/** Right-click on a traced cell: attack if red, otherwise cancel move/arming/deselect. */
	void HandleWorldGridCellSecondaryClicked(int WorldX, int WorldY);
	/** Updates directional blast preview while hovering (Energy Wave, etc.). */
	void HandleWorldGridCellHovered(int WorldX, int WorldY);
	/** Updates hover chrome for the cell under the cursor. */
	void HandleBoardCellHover(int WorldX, int WorldY);
	void ClearBoardCellHover();
	/** Forwards keyboard shortcuts to the match HUD (pass, undo, cancel). */
	void HandleShortcutKey(const FKey& Key);
	/** 1-9 selects the Nth hand card in the sorted strip (cheapest first). */
	void HandleHandHotkeySlot(int32 Slot1Based);
	bool WantsDirectionalAreaHoverPreview() const;
	bool WantsBoardHoverTrace() const { return true; }
	/** Rebuilds hand, board chrome, and status from the match snapshot. */
	void RefreshBoardUi();
	/** Hides match HUD while combat visualization is on screen. */
	void SetMatchUiHidden(bool bHidden);
	bool IsMatchUiHidden() const { return bMatchUiHidden; }
	/** Hide battle viz curtain and restore the live 3D board (called from game instance on viz exit). */
	void SetCombatScreenActive(bool bActive);

private:
	static STacticsBoardPanel* sActiveInstance;

	struct FBoardCellWidgetCache
	{
		TSharedPtr<SWidget> Widget;
		uint32 PresentationHash = 0;
	};

	FReply OnCellClicked(int WorldX, int WorldY);
	void Refresh();
	void RefreshBoardTargetHighlights();
	void RefreshStatusText();
	void RunCli(const FString& Line);
	void RunAbilityCli(const FString& Line, const FString& AbilityKey, const TArray<FIntPoint>& TargetCells);
	/** Exec CLI on the local/host authority match and show combat flash when attacks resolve. */
	void ExecuteLocalCliLine(const FString& Line);
	/** Pass all seats in the Defense window, then show the battle visualization. */
	void ResolvePendingAttacksWithVisualization();
	bool EnsureCliCell(FString& OutError) const;
	/** Clears hand/ability arming (not pending move - server owns that). */
	void ClearPlayArmingState();
	bool MulticastCellAlreadyPicked(const FString& C, const FString& R) const;
	void RunMulticastCastCli(bool bReserves);
	bool TryHandleMulticastCellPick(const FString& C, const FString& R, bool bReserves);
	bool ResolveWebSocketNet(UTacticsWebSocketSubsystem*& OutNet) const;
	bool TryBuildCliFromPendingCell(const FString& Verb, FString& OutLine, FString& OutErr) const;
	FReply DispatchCliFromPendingCellVerb(const FString& Verb);
	bool DispatchMovePreviewForWorldCell(int WorldX, int WorldY);
	bool TryDispatchAttackAtWorldCell(int WorldX, int WorldY);
	void ArmSelectedAbilityForTargeting(const FString& AbilityKey, bool bNeedsBoardTarget);
	void RebuildUnitAbilityHotbar(bool bShowForSelectedUnit);

	TSharedRef<SWidget> BuildCellButton(int WorldX, int WorldY);
	void EnsureBoardGridBuilt();
	void RefreshBoardGrid2DPresentations();
	void RebuildBoardGrid2D();
	void RebuildBoardUnitArtOverlay();
	void RebuildBoardDamagePopupOverlay();
	void RefreshAbilityDamagePopups();
	TSharedRef<SWidget> BuildStackCard(const FTacticsStackItemUi& Item);
	void RebuildHandBar();
	void RebuildReservesBar();
	/** Repopulate the available-energy counters above Territories (floating + untapped territories). */
	void RebuildEnergyCountersBar();
	void RefreshSelectedHandCardPanel();
	void RebuildZoneChoiceBar();
	void RebuildScanChoiceBar();
	/** Conquering Territories: compact placed-territory roster; abilities show for the selected land. */
	void RebuildTerritoryBar();
	/** Auto-expand / collapse Territories + Reserves rails based on phase and arming. */
	void SyncCollapsibleRails();
	void SetTerritoriesRailExpanded(bool bExpanded, bool bFromUser);
	void SetReservesRailExpanded(bool bExpanded, bool bFromUser);
	bool IsHandDetailPanelVisible() const;
	float GetDynamicBoardSafeLeft() const;
	float GetDynamicBoardSafeRight() const;
	float GetTerritoriesRailOccupiedWidth() const;
	float GetReservesRailOccupiedWidth() const;
	FText GetTerritoriesChipLabel() const;
	FText GetReservesChipLabel() const;
	EVisibility GetTerritoriesExpandedVisibility() const;
	EVisibility GetTerritoriesChipVisibility() const;
	EVisibility GetReservesExpandedVisibility() const;
	EVisibility GetReservesChipVisibility() const;
	EVisibility GetEnergyHudVisibility() const;
	void RebuildSpellStack();
	void RebuildActionQueue();
	void RefreshActionQueueHoverOverlay();
	TSharedRef<SWidget> BuildActionQueueHoverPreviewCard(const FTacticsActionQueueHoverCardUi& Card) const;
	void ShowAbilityDescription(const FString& AbilityKey);
	void HideAbilityDescription();
	void RebuildActionBar();
	/** Hand off resolved encounters to the 3D battle visualization stage. */
	void ShowBattleVisualization(const TArray<FCombatEncounter>& Encounters);
	void TryPresentCombatVisualizationPause();
	void PresentPausedCombatOrAbilityVisualization();
	/** Show the next queued passive attack viz event (mortar shot etc.) if any remain. */
	void TryPresentNextPassiveAttackViz();
	void HandleBattleVisualizationDismissed();
	void RebuildBottomRightBar();
	void RebuildDiscardPilePanel();
	void RebuildPurgatoryPanel();
	void RunResetDemo();
	void RunResetSandbox();
	void RebuildSeatSwitcher();
	void RebuildDiscardModal();
	void RebuildModalSpellPicker();
	void OnModalSpellModePicked(int32 ModeIndex0);
	void BeginArmedSpellTargetingAfterModePick();
	bool GetArmedSpellRequiresBoardCell(bool& bOutRequiresCell) const;
	FString InsertArmedSpellModeIntoCastLine(const FString& BaseCastLine) const;
	void TryPrimaryConfirmAction();
	void LoadBoardUiPreferences();
	void SaveBoardUiPreferences();
	void ApplyBotSettingsFromPreferences();
	bool CanControlledSeatTakeActions() const;
	bool IsWaitingForControlledSeatTurn() const;
	void SwitchControlSeat(int32 Seat);
	void ActivateHandCardAtIndex(int32 Index1Based);
	void AppendCombatLogLine(const FString& Line);
	void RebuildCombatLogPanel();
	bool ShouldAppendToCombatLog(const FString& Line) const;
	FText GetNetworkStatusPillText() const;
	FLinearColor GetNetworkStatusPillColor() const;

	bool IsArmedNoTargetSpell() const;
	bool IsArmedFocusSpell() const;
	bool IsArmedDirectionalFocusSpell() const;
	bool IsArmedPushDirectionSpell() const;
	bool IsArmedStackTargetSpell() const;
	bool IsArmedPlayerTargetSpell() const;
	bool IsArmedXCostSpell() const;
	bool IsArmedXCostAbility() const;
	bool IsArmedStackTargetEffect() const;
	bool CanArmedStackTargetSourceType(const FString& SourceType) const;
	void InitArmedXCostForHandSpell(int32 Index1Based);
	void InitArmedXCostForReservesSpell(int32 Index1Based);
	void InitArmedXCostForAbility(const FString& AbilityKey);
	FString AppendArmedXCostCliSuffix(const FString& BaseLine) const;
	/** Build cast line (with X if armed), clear arming state, then dispatch CLI. */
	void RunArmedCastCli(const FString& BaseCastLine);
	FString BuildArmedAbilityStackCliLine(const FString& StackId) const;

	bool IsWebSocketClientP2() const;
	void OnHostCliAckFromNet(const FString& Message);

	bool Uses3DBoardChrome() const;
	FLinearColor GetChromeTextColor() const;
	FLinearColor GetSidePanelBorderColor() const;
	FMargin GetTopLeftChromePadding() const;
	FMargin GetTopRightChromePadding() const;
	FMargin GetActionQueueChromePadding() const;
	FMargin GetActionQueueHoverOverlayPadding() const;
	FMargin GetCenterGridOverlayPadding() const;
	FMargin GetHandDetailOverlayPadding() const;
	FMargin GetHandStripOverlayPadding() const;
	FMargin GetReservesStripOverlayPadding() const;
	FMargin GetReservesColumnPadding() const;
	FMargin GetEnergyHudOverlayPadding() const;
	FMargin GetBottomRightOverlayPadding() const;
	FMargin GetUnitHoverScreenOverlayPadding() const;
	FMargin GetUnitHoverCellHintPadding() const;
	void RebuildUnitHoverOverlays();
	bool TryComputeUnitHoverGridAnchor(float& OutAnchorX, float& OutAnchorY) const;
	bool TryComputeUnitHoverScreenAnchor(FVector2D& OutPanelLocal) const;

	bool CanActivateAbilityFromDetail(const FString& SpeedTag, bool bUsed) const;
	bool TryResolveAbilityForDescBlock(const FString& BlockName, int32 ActivatedAbilityIndex, FString& OutKey, bool& bNeedCell,
		bool& bUsed, bool& bEnabled) const;
	void RebuildCardDetailDescVBox(const TSharedPtr<SVerticalBox>& Box, const FString& RulesConverted, bool bUnitBoardDetail,
		bool bIsSpellCard, int32 DetailCardIndex1Based, bool bDetailFromReserves,
		const TArray<FTacticsGlossaryNameBody>& GlossaryMarkupEntries, bool bAdvancedGlossary,
		const TacticsCardText::FCardRulesLayout* PrebuiltLayout = nullptr);
	bool TryComposeBoardUnitRulesLayout(TacticsCardText::FCardRulesLayout& OutLayout, FString& OutRulesConverted) const;

	TWeakObjectPtr<UTacticsMatchSubsystem> Subsystem;
	TSharedPtr<SBox> BoardGridSizer;
	TSharedPtr<SOverlay> BoardCellsOverlay;
	TSharedPtr<SOverlay> BoardUnitArtOverlay;
	TSharedPtr<SOverlay> BoardDamagePopupOverlay;
	TSharedPtr<SOverlay> BoardUnitHoverOverlay;
	TMap<FIntPoint, FBoardCellWidgetCache> BoardCellWidgets;
	int32 CachedGridMinX = 0;
	int32 CachedGridMaxY = 0;
	int32 CachedGridW = 0;
	int32 CachedGridH = 0;
	TSharedPtr<SHorizontalBox> ActionBar;
	TSharedPtr<SScrollBox> HandScroll;
	/** Conquering Territories: compact roster of placed lands (right rail when expanded). */
	TSharedPtr<class SVerticalBox> TerritoryBar;
	TSharedPtr<SScrollBox> ReservesScroll;
	/** Right-rail energy totals above Territories (floating + untapped territories). */
	TSharedPtr<SVerticalBox> EnergyCountersBox;
	TSharedPtr<SBox> TerritoriesExpandedChrome;
	TSharedPtr<SBox> TerritoriesChipChrome;
	TSharedPtr<SBox> ReservesExpandedChrome;
	TSharedPtr<SBox> ReservesChipChrome;
	TSharedPtr<SBox> EnergyHudChrome;
	TSharedPtr<SVerticalBox> BottomRightBar;
	TSharedPtr<SBox> DiscardPilePanelChrome;
	TSharedPtr<SScrollBox> DiscardPileScroll;
	TSharedPtr<SBox> PurgatoryPanelChrome;
	TSharedPtr<SScrollBox> PurgatoryScroll;
	TSharedPtr<SScrollBox> SpellStackScroll;
	TSharedPtr<SScrollBox> ActionQueueScroll;
	TSharedPtr<SBox> ActionQueueHoverOverlay;
	TSharedPtr<SBox>          AbilityDescriptionChrome;
	TSharedPtr<class SRichTextBlock> AbilityDescriptionText;
	TSharedPtr<SOverlay>      CombatScreenHost;
	TSharedPtr<SOverlay>      RootOverlay;
	bool                      bCombatScreenActive{false};
	bool                      bMatchUiHidden{false};
	FCombatEncounter          ActiveCombatEncounter;
	bool                      bBattleVizAwaitingResume{false};
	bool                      bBattleVizShowingResult{false};
	/** True while draining the passive attack viz event queue (mortar shots etc.). */
	bool                      bPassiveVizDraining{false};
	/** Prevents Refresh ↔ PresentPausedCombatOrAbilityVisualization infinite recursion. */
	bool                      bPresentingPausedCombatViz{false};
	TSharedPtr<STextBlock> StackLegendText;
	TSharedPtr<STextBlock> SelectedStatsText;
	TSharedPtr<SBorder> SelectedHandCardPanel;
	TSharedPtr<SImage> SelectedHandCardArtImage;
	TSharedPtr<STextBlock> SelectedHandCardTitleText;
	TSharedPtr<class SRichTextBlock> SelectedHandCardCostText;
	TSharedPtr<class SRichTextBlock> SelectedHandCardStatsText;
	TSharedPtr<SBox> SelectedHandCardCostRowBox;
	TSharedPtr<SBox> SelectedHandCardStatsRowBox;
	TSharedPtr<SBox> SelectedHandCardStatsColumnBox;
	TSharedPtr<SBox> SelectedHandCardActionIconsBox;
	TSharedPtr<class SVerticalBox> SelectedHandCardMoveActionIconsVBox;
	TSharedPtr<class SVerticalBox> SelectedHandCardAttackActionIconsVBox;
	TSharedPtr<class SVerticalBox> SelectedHandCardReactionActionIconsVBox;
	TSharedPtr<class SVerticalBox> SelectedHandCardActiveEffectsVBox;
	TSharedPtr<SWidget> SelectedHandCardDivider;
	TSharedPtr<class SVerticalBox> SelectedHandCardDescVBox;
	TSharedPtr<SBox> SelectedUnitAbilityHotbarBox;
	TSharedPtr<class SWrapBox> SelectedUnitAbilityHotbar;
	TSharedPtr<class SCheckBox> SelectedHandCardGlossaryCheckBox;
	TSharedPtr<SBox> SelectedHandCardGlossarySection;
	TSharedPtr<class SVerticalBox> SelectedHandCardGlossaryVBox;
	TSharedPtr<STextBlock> TipsText;
	TSharedPtr<STextBlock> StatusText;
	TSharedPtr<SBox> UnitHoverScreenOverlay;
	TSharedPtr<STextBlock> UnitHoverCellHintLabel;
	TSharedPtr<SBox> CombatLogChrome;
	TSharedPtr<SScrollBox> CombatLogScroll;
	TSharedPtr<STextBlock> NetworkStatusPillText;
	TSharedPtr<SBox> DiscardModalChrome;
	TSharedPtr<SVerticalBox> DiscardModalList;
	TSharedPtr<SHorizontalBox> SeatSwitcherRow;
	TSharedPtr<SBox> WaitingTurnOverlay;
	FTacticsUnitHoverPresentation HoverUnit;
	FString HoverCellHintLine;
	TSharedPtr<SBox> CenterGridChrome;
	FDelegateHandle BoardChangedHandle;
	FDelegateHandle AbilityDamagePopupsChangedHandle;
	FDelegateHandle TargetPreviewChangedHandle;
	FDelegateHandle CliAckHandle;

	int32 DeployHandIndex1Based{1};
	int32 DeployReservesIndex1Based{1};
	/** True after clicking a hand card; next grid click plays that card (unit deploy / spell cast). */
	bool bHandArmedForTile{false};
	/** True after clicking a reserves card (owner turn only). */
	bool bReservesArmedForTile{false};
	/** Focus spell: waiting for a friendly unit click to choose the caster before target highlights. */
	bool bFocusSpellAwaitingCaster{false};
	/** Push-direction spell phase 1: waiting for unit target before direction picker. */
	bool bPushSpellAwaitingUnit{false};
	int32 PushSpellTargetWorldX{-1};
	int32 PushSpellTargetWorldY{-1};
	/** True after clicking an ability that needs a board cell; next grid click runs `ability <key> <col> <row>`. */
	bool bAbilityArmedForTile{false};
	/** Conquering Territories: after clicking a targeted "use land" ability, the next grid click runs
	 *  `use_land <territory#> <ability#> <col> <row>`. Indices are 0-based here, 1-based in the CLI. */
	bool bLandAbilityArmedForTile{false};
	/** A no-target "use land" ability that is armed and awaiting a Confirm click (so the player can
	 *  read the territory first). Confirm runs `use_land <t> <a>`. */
	bool bLandAbilityArmedNoTarget{false};
	int32 ArmedLandTerritoryIndex{-1};
	int32 ArmedLandAbilityIndex{-1};
	/** Human-readable description of the currently-armed land ability, shown in the prompt. */
	FString ArmedLandDescription;
	/** Selected territory row in the right rail (0-based representative index); -1 = roster only. */
	int32 SelectedTerritoryRepIndex{-1};
	/** Collapsible chrome: Territories / Reserves default closed; auto-open when relevant. */
	bool bTerritoriesRailExpanded{false};
	bool bTerritoriesUserCollapsed{false};
	bool bTerritoriesUserPinnedOpen{false};
	bool bReservesRailExpanded{true};
	bool bReservesUserPinnedOpen{true};
	/** Conquering Territories placement: which Energy-phase territory choice (1-based) is selected and
	 *  awaiting a Confirm click, so the player can read the territory before it is placed. -1 = none.
	 *  Deliberately NOT reset by ClearPlayArmingState (that runs on every zone-bar rebuild); it is
	 *  cleared explicitly on confirm/skip and when no zone choice is pending. */
	int32 ArmedZoneChoiceIndex{-1};
	/** Scan: which peeked card (1-based) is selected and awaiting a Discard confirm, so the player can
	 *  read it before deciding. -1 = none. Same lifecycle notes as ArmedZoneChoiceIndex (not reset by
	 *  ClearPlayArmingState; cleared explicitly on discard/finish/cancel and when no scan is pending). */
	int32 ArmedScanCardIndex{-1};
	/** Multicast spell: total targets to pick and CLI col/row tokens collected so far. */
	int32 ArmedMulticastTargetTotal{0};
	int32 ArmedMulticastTargetsPicked{0};
	TArray<FString> ArmedMulticastCliTokens;
	/** Chosen X for variable-cost spells (0 = not chosen yet). */
	int32 ArmedXCostAmount{0};
	int32 ArmedXCostMin{0};
	int32 ArmedXCostMax{0};
	FString ArmedXCostEnergyType;
	bool bArmedSpellHasXCost{false};
	bool bArmedAbilityRequiresXCost{false};
	/** Modal spell: MTG Arena-style mode picker before targeting / no-target cast. */
	bool bModalSpellPickerActive{false};
	bool bModalSpellFromReserves{false};
	int32 ArmedSpellModeIndex{-1};
	TSharedPtr<SBox> ModalSpellPickerChrome;
	TSharedPtr<SHorizontalBox> ModalSpellPickerRow;
	FString ArmedAbilityKey;
	FString LastCliOutput{
		TEXT("Arm a card to deploy/cast on a tile. Context actions appear in the bar above.")};

	/** Transient "why it failed" toast shown when an action/cast is rejected. Fades out on its own. */
	void ShowFailureAlert(const FString& Reason);
	FString FailureAlertText;
	FCurveSequence FailureAlertCurve;
	/** Offline / non-remote: seat count used for the next `Reset demo` or explicit new local game (not used while acting as WS client). */
	int32 LocalDemoPlayerCount{2};
	bool bShowDiscardPilePanel{false};
	bool bShowPurgatoryPanel{false};
	/** End-of-turn discard: true after the player clicks a hand card to discard (shows bottom-right Discard). */
	bool bDiscardHandCardSelected{false};
	/** When false, the detail panel shows the selected board unit's card; hand click sets true. */
	bool bDetailPanelShowsHandCard{false};
	/** Top-left CLI / phase log (Tips + Status). Off by default so it does not show through the card panel. */
	bool bShowConsoleLog{false};
	/** When off, hides CLI shortcuts, networking, reset, match/team editors, and zone/discard dev panels. */
	bool bShowDevTools{false};
	/** Card detail sidebar: show keyword/status definition blocks (right of art). */
	bool bShowCardGlossaryDefinitions{true};
	/** Player-facing filtered event log (damage, casts, defeats). Hidden by default. */
	bool bShowCombatLog{false};
	/** Local solo: P2 runs headless MCTS via UTacticsMatchSubsystem bot ticker. */
	bool bP2BotEnabled{true};
	TArray<FString> CombatLogLines;
	static constexpr int32 MaxCombatLogLines = 48;
};