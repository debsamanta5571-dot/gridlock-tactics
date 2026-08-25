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


STacticsBoardPanel* STacticsBoardPanel::sActiveInstance = nullptr;


bool STacticsBoardPanel::IsHandScrollHovered()
{
	if (!sActiveInstance || !sActiveInstance->HandScroll.IsValid()) {
		return false;
	}
	return IsCursorInsideWidget(sActiveInstance->HandScroll);
}

bool STacticsBoardPanel::ShouldSuppressBoardHoverUnderCursor()
{
	if (!sActiveInstance || !FSlateApplication::IsInitialized()) {
		return true;
	}
	if (IsHandScrollHovered()) {
		return true;
	}
	if (sActiveInstance->ActionQueueHoverOverlay.IsValid()
		&& sActiveInstance->ActionQueueHoverOverlay->GetVisibility() != EVisibility::Collapsed
		&& IsCursorInsideWidget(sActiveInstance->ActionQueueHoverOverlay)) {
		return true;
	}
	if (sActiveInstance->UnitHoverScreenOverlay.IsValid()
		&& sActiveInstance->UnitHoverScreenOverlay->GetVisibility() != EVisibility::Collapsed
		&& IsCursorInsideWidget(sActiveInstance->UnitHoverScreenOverlay)) {
		return true;
	}
	if (sActiveInstance->Subsystem.IsValid() && sActiveInstance->Subsystem->Uses3DBoardTiles()
		&& sActiveInstance->CenterGridChrome.IsValid()
		&& !IsCursorInsideWidget(sActiveInstance->CenterGridChrome)) {
		return true;
	}
	return false;
}

STacticsBoardPanel::~STacticsBoardPanel()
{
	if (sActiveInstance == this) {
		sActiveInstance = nullptr;
	}
	SaveBoardUiPreferences();
	if (Subsystem.IsValid()) {
		Subsystem->OnBoardChanged.Remove(BoardChangedHandle);
		Subsystem->OnAbilityDamagePopupsChanged.Remove(AbilityDamagePopupsChangedHandle);
		Subsystem->OnTargetPreviewChanged.Remove(TargetPreviewChangedHandle);
		UTacticsWebSocketSubsystem* Net = nullptr;
		if (ResolveWebSocketNet(Net) && CliAckHandle.IsValid()) {
			Net->OnCliAckFromHost.Remove(CliAckHandle);
		}
	}
}

/** Clears armed hand/ability/land targeting so the next click is not a leftover cast. */
void STacticsBoardPanel::ClearPlayArmingState()
{
	bHandArmedForTile = false;
	bReservesArmedForTile = false;
	bFocusSpellAwaitingCaster = false;
	bPushSpellAwaitingUnit = false;
	PushSpellTargetWorldX = -1;
	PushSpellTargetWorldY = -1;
	bAbilityArmedForTile = false;
	ArmedAbilityKey.Empty();
	bLandAbilityArmedForTile = false;
	bLandAbilityArmedNoTarget = false;
	ArmedLandTerritoryIndex = -1;
	ArmedLandAbilityIndex = -1;
	ArmedLandDescription.Empty();
	ArmedMulticastTargetTotal = 0;
	ArmedMulticastTargetsPicked = 0;
	ArmedMulticastCliTokens.Empty();
	ArmedXCostAmount = 0;
	ArmedXCostMin = 0;
	ArmedXCostMax = 0;
	ArmedXCostEnergyType.Empty();
	bArmedSpellHasXCost = false;
	bArmedAbilityRequiresXCost = false;
	bModalSpellPickerActive = false;
	bModalSpellFromReserves = false;
	ArmedSpellModeIndex = -1;
	HideAbilityDescription();
	if (Subsystem.IsValid()) {
		Subsystem->ClearBoardTargetPreview();
		Subsystem->ClearDeployPreview();
	}
}

/** True if this CLI cell token pair is already in the multicast target list. */
bool STacticsBoardPanel::MulticastCellAlreadyPicked(const FString& C, const FString& R) const
{
	for (int32 i = 0; i + 1 < ArmedMulticastCliTokens.Num(); i += 2) {
		if (ArmedMulticastCliTokens[i] == C && ArmedMulticastCliTokens[i + 1] == R) {
			return true;
		}
	}
	return false;
}

/** Sends the armed multicast spell with every cell picked so far. */
void STacticsBoardPanel::RunMulticastCastCli(bool bReserves)
{
	const int32 Idx = bReserves ? DeployReservesIndex1Based : DeployHandIndex1Based;
	FString Line = bReserves
		? FString::Printf(TEXT("cast_reserve %d"), Idx)
		: FString::Printf(TEXT("cast %d"), Idx);
	Line = InsertArmedSpellModeIntoCastLine(Line);
	for (const FString& Tok : ArmedMulticastCliTokens) {
		Line += FString::Printf(TEXT(" %s"), *Tok);
	}
	ClearPlayArmingState();
	RunCli(Line);
}

/** Records one multicast target; casts when the required count is reached. */
bool STacticsBoardPanel::TryHandleMulticastCellPick(const FString& C, const FString& R, bool bReserves)
{
	if (ArmedMulticastTargetTotal <= 1) {
		return false;
	}
	if (MulticastCellAlreadyPicked(C, R)) {
		LastCliOutput = TEXT("Multicast: choose a different target for each copy.");
		RefreshStatusText();
		Refresh();
		return true;
	}
	ArmedMulticastCliTokens.Add(C);
	ArmedMulticastCliTokens.Add(R);
	++ArmedMulticastTargetsPicked;
	if (ArmedMulticastTargetsPicked >= ArmedMulticastTargetTotal) {
		RunMulticastCastCli(bReserves);
		return true;
	}
	LastCliOutput = FString::Printf(
		TEXT("Multicast: pick target %d of %d (or Finish multicast to skip remaining copies)."),
		ArmedMulticastTargetsPicked + 1,
		ArmedMulticastTargetTotal);
	RefreshStatusText();
	Refresh();
	return true;
}

/** Fills OutNet with the LAN subsystem when this client is connected to a host. */
bool STacticsBoardPanel::ResolveWebSocketNet(UTacticsWebSocketSubsystem*& OutNet) const
{
	OutNet = nullptr;
	if (!Subsystem.IsValid()) {
		return false;
	}
	UGameInstance* GI = Subsystem->GetGameInstance();
	if (!GI) {
		return false;
	}
	OutNet = GI->GetSubsystem<UTacticsWebSocketSubsystem>();
	return OutNet != nullptr;
}

bool STacticsBoardPanel::TryBuildCliFromPendingCell(const FString& Verb, FString& OutLine, FString& OutErr) const
{
	if (!EnsureCliCell(OutErr)) {
		return false;
	}
	int Px = 0, Py = 0;
	if (!Subsystem.IsValid() || !Subsystem->TryGetPendingCliWorldCell(Px, Py)) {
		OutErr = TEXT("No target cell.");
		return false;
	}
	FString C, R;
	if (!Subsystem->GetCliCellTokens(Px, Py, C, R)) {
		OutErr = TEXT("Invalid cell for CLI.");
		return false;
	}
	OutLine = FString::Printf(TEXT("%s %s %s"), *Verb, *C, *R);
	return true;
}

FReply STacticsBoardPanel::DispatchCliFromPendingCellVerb(const FString& Verb)
{
	FString Line, Err;
	if (!TryBuildCliFromPendingCell(Verb, Line, Err)) {
		LastCliOutput = Err;
		RefreshStatusText();
		return FReply::Handled();
	}
	RunCli(Line);
	return FReply::Handled();
}

/** Starts a move preview if the selected unit can reach this cell. */
bool STacticsBoardPanel::DispatchMovePreviewForWorldCell(int WorldX, int WorldY)
{
	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady() || Subsystem->IsAwaitingHandDiscard() || Subsystem->IsAwaitingScan() || Subsystem->IsAwaitingTerritoryLoot()) {
		return false;
	}
	if (!Subsystem->HasUnitSelected() || !Subsystem->IsSelectedUnitControlled() || !Subsystem->CanControlledPlayerActInMainPhase()) {
		return false;
	}
	if (!Subsystem->IsReachableMoveCellAtWorld(WorldX, WorldY)) {
		LastCliOutput = TEXT("That cell is outside movement range - green tiles are valid.");
		RefreshStatusText();
		Refresh();
		return true;
	}
	FString C, R;
	if (!Subsystem->GetCliCellTokens(WorldX, WorldY, C, R)) {
		LastCliOutput = TEXT("That cell is outside the CLI grid for movement.");
		RefreshStatusText();
		Refresh();
		return true;
	}
	bDetailPanelShowsHandCard = false;
	RunCli(FString::Printf(TEXT("move %s %s"), *C, *R));
	return true;
}

/** Issues an attack CLI if this cell is a legal attack target. */
bool STacticsBoardPanel::TryDispatchAttackAtWorldCell(int WorldX, int WorldY)
{
	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady() || Subsystem->IsAwaitingHandDiscard() || Subsystem->IsAwaitingScan() || Subsystem->IsAwaitingTerritoryLoot()) {
		return false;
	}
	if (!Subsystem->IsAttackTargetCellAtWorld(WorldX, WorldY)) {
		return false;
	}
	if (Subsystem->HasPendingMoveForControlledPlayer()) {
		ShowFailureAlert(TEXT("Confirm or cancel pending move first"));
		LastCliOutput = TEXT("Confirm or cancel pending move before attacking.");
		RefreshStatusText();
		Refresh();
		return true;
	}
	FString C, R;
	if (Subsystem->GetCliCellTokens(WorldX, WorldY, C, R)) {
		bDetailPanelShowsHandCard = false;
		RunCli(FString::Printf(TEXT("attack %s %s"), *C, *R));
		return true;
	}
	LastCliOutput = TEXT("That cell is outside the CLI grid for attack.");
	RefreshStatusText();
	Refresh();
	return true;
}

bool STacticsBoardPanel::Uses3DBoardChrome() const
{
	return Subsystem.IsValid() && Subsystem->Uses3DBoardTiles();
}

FLinearColor STacticsBoardPanel::GetChromeTextColor() const
{
	const FLinearColor Base = Uses3DBoardChrome() ? kCardTextWhite : kUiBlack;
	if (IsWaitingForControlledSeatTurn()) {
		return Base * 0.55f;
	}
	return Base;
}

FLinearColor STacticsBoardPanel::GetSidePanelBorderColor() const
{
	return FLinearColor(0.f, 0.f, 0.f, 1.f);
}

FMargin STacticsBoardPanel::GetTopLeftChromePadding() const
{
	return FMargin(10.f, 6.f, 0.f, 0.f);
}

FMargin STacticsBoardPanel::GetTopRightChromePadding() const
{
	return FMargin(0.f, 6.f, 10.f, 0.f);
}

FMargin STacticsBoardPanel::GetActionQueueChromePadding() const
{
	return FMargin(0.f, 6.f, kRightSidePanelWidth + 10.f, 0.f);
}

FMargin STacticsBoardPanel::GetActionQueueHoverOverlayPadding() const
{
	float QueueBodyH = 28.f;
	if (Subsystem.IsValid()) {
		const int32 QueueCount = Subsystem->GetPhaseActionQueueCount();
		QueueBodyH = QueueCount > 0
			? FMath::Min(static_cast<float>(QueueCount) * kActionQueueHoverQueueRowH, kActionQueueHoverQueueScrollMaxH)
			: 28.f;
	}
	const float Top = kActionQueueHoverQueueChromeTop + kActionQueueHoverQueuePanelHeaderH + QueueBodyH
		+ kActionQueueHoverBelowQueueGap;
	return FMargin(0.f, Top, kRightSidePanelWidth + 10.f, 0.f);
}

FMargin STacticsBoardPanel::GetCenterGridOverlayPadding() const
{
	const bool b3d = Uses3DBoardChrome();
	const float Top = b3d ? k3DBoardSafeTop : 64.f;
	const float Bottom = b3d ? k3DBoardSafeBottom : 214.f;
	const float Left = b3d ? GetDynamicBoardSafeLeft() : 12.f;
	const float Right = b3d ? GetDynamicBoardSafeRight() : 12.f;
	return FMargin(Left, Top, Right, Bottom);
}

FMargin STacticsBoardPanel::GetHandDetailOverlayPadding() const
{
	const float Bottom = Uses3DBoardChrome() ? k3DBoardSafeBottom + 4.f : 214.f;
	return FMargin(16.f, 0.f, 0.f, Bottom);
}

FMargin STacticsBoardPanel::GetHandStripOverlayPadding() const
{
	const bool b3d = Uses3DBoardChrome();
	const float Bottom = b3d ? 6.f : 12.f;
	const float Left = b3d ? GetDynamicBoardSafeLeft() + 24.f : 124.f;
	const float Right = b3d ? GetDynamicBoardSafeRight() : 16.f;
	return FMargin(Left, 0.f, Right, Bottom);
}

FMargin STacticsBoardPanel::GetReservesStripOverlayPadding() const
{
	const float Right = kActionQueuePanelWidth + kRightSidePanelWidth + 10.f + kSidePanelGap;
	return FMargin(0.f, 6.f, Right, 0.f);
}

FMargin STacticsBoardPanel::GetReservesColumnPadding() const
{
	const float Bottom = Uses3DBoardChrome() ? k3DBoardSafeBottom + 4.f : 214.f;
	// Stable left edge: always to the right of the left chrome AND the (toggleable) card-detail panel.
	// A visibility-dependent fallback used to drop this to x=8 while no card was selected - during the
	// land phase (nothing selected) that put the reserves column on top of the left chrome, which read
	// as a glitch until you selected a land and the detail panel reappeared.
	return FMargin(kReservesColumnLeftBesideDetail, k3DBoardSafeTop, 0.f, Bottom);
}

FMargin STacticsBoardPanel::GetEnergyHudOverlayPadding() const
{
	// Energy sits in the right Territories rail; this helper is unused by layout now.
	return FMargin(0.f);
}

FMargin STacticsBoardPanel::GetBottomRightOverlayPadding() const
{
	const float Bottom = Uses3DBoardChrome() ? k3DBoardSafeBottom + 12.f : 100.f;
	return FMargin(12.f, 0.f, 20.f, Bottom);
}

bool STacticsBoardPanel::IsHandDetailPanelVisible() const
{
	return SelectedHandCardPanel.IsValid()
		&& SelectedHandCardPanel->GetVisibility() != EVisibility::Collapsed;
}

float STacticsBoardPanel::GetTerritoriesRailOccupiedWidth() const
{
	// Available Energy is always shown above Territories at full rail width.
	return kEnergyReservesPanelWidth;
}

float STacticsBoardPanel::GetReservesRailOccupiedWidth() const
{
	return bReservesRailExpanded ? kReservesColumnW : kRailChipW;
}

float STacticsBoardPanel::GetDynamicBoardSafeLeft() const
{
	// Board starts to the right of the reserves column, whose left edge is fixed beside the left chrome
	// / detail panel (see GetReservesColumnPadding). Stable regardless of detail-panel visibility so the
	// board doesn't jump when a card is selected/deselected.
	return kReservesColumnLeftBesideDetail + GetReservesRailOccupiedWidth() + k3DBoardGapFromChrome;
}

float STacticsBoardPanel::GetDynamicBoardSafeRight() const
{
	return GetTerritoriesRailOccupiedWidth() + kSidePanelGap + kActionQueuePanelWidth
		+ kSidePanelGap + kRightSidePanelWidth + 10.f;
}

void STacticsBoardPanel::SetTerritoriesRailExpanded(const bool bExpanded, const bool bFromUser)
{
	bTerritoriesRailExpanded = bExpanded;
	if (bFromUser) {
		bTerritoriesUserPinnedOpen = bExpanded;
		bTerritoriesUserCollapsed = !bExpanded;
	}
}

void STacticsBoardPanel::SetReservesRailExpanded(const bool bExpanded, const bool bFromUser)
{
	bReservesRailExpanded = bExpanded;
	if (bFromUser) {
		bReservesUserPinnedOpen = bExpanded;
		if (!bExpanded) {
			bReservesUserPinnedOpen = false;
		}
	}
}

void STacticsBoardPanel::SyncCollapsibleRails()
{
	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady()) {
		return;
	}

	const bool bCanUseLands = Subsystem->CanControlledPlayerActInMainPhase()
		|| (Subsystem->IsAnyReactionWindowPhase() && Subsystem->CanControlledPlayerPassPriority());
	const bool bLandArmed = bLandAbilityArmedForTile || bLandAbilityArmedNoTarget;
	const bool bWantTerritoriesOpen =
		bLandArmed || (bCanUseLands && Subsystem->GetControlledTerritoryCount() > 0);

	if (bWantTerritoriesOpen) {
		if (!bTerritoriesUserCollapsed) {
			bTerritoriesRailExpanded = true;
		}
	} else if (!bTerritoriesUserPinnedOpen) {
		bTerritoriesRailExpanded = false;
		bTerritoriesUserCollapsed = false;
	}

	const int32 ReservesN = Subsystem->GetControlledReservesCount();
	if (bReservesArmedForTile && ReservesN > 0) {
		bReservesRailExpanded = true;
	}
	// Reserves stay open by default; only the player collapse button closes them.
}

FText STacticsBoardPanel::GetTerritoriesChipLabel() const
{
	const int32 N = (Subsystem.IsValid() && Subsystem->IsMatchReady())
		? Subsystem->GetControlledTerritoryCount()
		: 0;
	return FText::FromString(FString::Printf(TEXT("Lands\n%d"), N));
}

FText STacticsBoardPanel::GetReservesChipLabel() const
{
	const int32 N = (Subsystem.IsValid() && Subsystem->IsMatchReady())
		? Subsystem->GetControlledReservesCount()
		: 0;
	return FText::FromString(FString::Printf(TEXT("Res\n%d"), N));
}

EVisibility STacticsBoardPanel::GetTerritoriesExpandedVisibility() const
{
	return bTerritoriesRailExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility STacticsBoardPanel::GetTerritoriesChipVisibility() const
{
	return bTerritoriesRailExpanded ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility STacticsBoardPanel::GetReservesExpandedVisibility() const
{
	return bReservesRailExpanded ? EVisibility::Visible : EVisibility::Collapsed;
}

EVisibility STacticsBoardPanel::GetReservesChipVisibility() const
{
	return bReservesRailExpanded ? EVisibility::Collapsed : EVisibility::Visible;
}

EVisibility STacticsBoardPanel::GetEnergyHudVisibility() const
{
	return (Subsystem.IsValid() && Subsystem->IsMatchReady()) ? EVisibility::Visible
															 : EVisibility::Collapsed;
}

FMargin STacticsBoardPanel::GetUnitHoverScreenOverlayPadding() const
{
	FVector2D PanelLocal(0.f, 0.f);
	if (!HoverUnit.bHasUnit || !TryComputeUnitHoverScreenAnchor(PanelLocal)) {
		return FMargin(0.f);
	}
	const float Left = FMath::Max(0.f, PanelLocal.X - kUnitHoverPanelWidth * 0.5f);
	const float Top = FMath::Max(0.f, PanelLocal.Y);
	return FMargin(Left, Top, 0.f, 0.f);
}

FMargin STacticsBoardPanel::GetUnitHoverCellHintPadding() const
{
	const float Bottom = Uses3DBoardChrome() ? k3DBoardSafeBottom + 72.f : 232.f;
	return FMargin(0.f, 0.f, 0.f, Bottom);
}

bool STacticsBoardPanel::TryComputeUnitHoverGridAnchor(float& OutAnchorX, float& OutAnchorY) const
{
	if (!HoverUnit.bHasUnit || !Subsystem.IsValid()) {
		return false;
	}
	int32 MinX = 0;
	int32 MinY = 0;
	int32 W = 0;
	int32 H = 0;
	if (!Subsystem->GetMergedBounds(MinX, MinY, W, H) || W < 1 || H < 1) {
		return false;
	}
	const int32 MaxY = MinY + H - 1;
	const float StepX = kCellW + kCellGap;
	const float StepY = kCellH + kCellGap;

	float GridCenterX = static_cast<float>(HoverUnit.WorldX);
	float GridCenterY = static_cast<float>(HoverUnit.WorldY);
	int32 SpanY = 1;
	TArray<FTacticsBoardUnitPose> Poses;
	Subsystem->GatherBoardUnitPoses(Poses);
	for (const FTacticsBoardUnitPose& Pose : Poses) {
		if (Pose.bIsBase) {
			continue;
		}
		const float HalfSpanX = static_cast<float>(FMath::Max(1, Pose.FootprintSpanX)) * 0.5f;
		const float HalfSpanY = static_cast<float>(FMath::Max(1, Pose.FootprintSpanY)) * 0.5f;
		if (FMath::Abs(Pose.GridCenterX - GridCenterX) <= HalfSpanX + 0.01f
			&& FMath::Abs(Pose.GridCenterY - GridCenterY) <= HalfSpanY + 0.01f) {
			GridCenterX = Pose.GridCenterX;
			GridCenterY = Pose.GridCenterY;
			SpanY = FMath::Max(1, Pose.FootprintSpanY);
			break;
		}
	}

	const float ArtH = static_cast<float>(SpanY) * StepY - kCellGap;
	const float CenterCx = GridCenterX - static_cast<float>(MinX);
	const float CenterCy = static_cast<float>(MaxY) - GridCenterY;
	OutAnchorX = CenterCx * StepX + kCellW * 0.5f;
	OutAnchorY = CenterCy * StepY + kCellH * 0.5f + ArtH * 0.5f + kUnitHoverBelowArtGap2D + ArtH * kUnitHoverBelowArt2DScale;
	return true;
}

bool STacticsBoardPanel::TryComputeUnitHoverScreenAnchor(FVector2D& OutPanelLocal) const
{
	if (!HoverUnit.bHasUnit || !Subsystem.IsValid() || !Uses3DBoardChrome()) {
		return false;
	}
	UTacticsGameInstance* GI = Cast<UTacticsGameInstance>(Subsystem->GetGameInstance());
	UWorld* World = GI ? GI->GetWorld() : nullptr;
	if (!World) {
		return false;
	}
	ATactics3DBoardGameMode* GM = Cast<ATactics3DBoardGameMode>(World->GetAuthGameMode());
	ATactics3DWorldBoardActor* Board = GM ? GM->WorldBoardActor.Get() : nullptr;
	if (!Board) {
		return false;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC) {
		return false;
	}

	float GridCenterX = static_cast<float>(HoverUnit.WorldX);
	float GridCenterY = static_cast<float>(HoverUnit.WorldY);
	int32 FootprintSpan = 1;
	int32 FootprintSpanY = 1;
	TArray<FTacticsBoardUnitPose> Poses;
	Subsystem->GatherBoardUnitPoses(Poses);
	for (const FTacticsBoardUnitPose& Pose : Poses) {
		if (Pose.bIsBase) {
			continue;
		}
		const float HalfSpanX = static_cast<float>(FMath::Max(1, Pose.FootprintSpanX)) * 0.5f;
		const float HalfSpanY = static_cast<float>(FMath::Max(1, Pose.FootprintSpanY)) * 0.5f;
		if (FMath::Abs(Pose.GridCenterX - GridCenterX) <= HalfSpanX + 0.01f
			&& FMath::Abs(Pose.GridCenterY - GridCenterY) <= HalfSpanY + 0.01f) {
			GridCenterX = Pose.GridCenterX;
			GridCenterY = Pose.GridCenterY;
			FootprintSpan = FMath::Max(Pose.FootprintSpanX, Pose.FootprintSpanY);
			FootprintSpanY = FMath::Max(1, Pose.FootprintSpanY);
			break;
		}
	}

	// The unit actor sits at GetUnitArtFloorZ() with the art plane pivoted at its center, so the
	// sprite spans half above / half below that anchor. Project the sprite bottom edge (including
	// plane depth corners) so the hover panel lands well below the whole sprite, not over it.
	const FVector WorldCenter = Board->GetUnitHoverAnchorWorldPosition(GridCenterX, GridCenterY, FootprintSpan);
	const float ArtHeightWorld = Board->GetUnitArtExtentWorldZ(FootprintSpan);
	const float ArtHalfHeight = ArtHeightWorld * 0.5f;
	const float ArtHalfDepthWorld = 50.f * static_cast<float>(FootprintSpanY) * 0.98f;
	const FVector WorldTop = WorldCenter + FVector(0.f, 0.f, ArtHalfHeight);
	const FVector BottomCenter = WorldCenter - FVector(0.f, 0.f, ArtHalfHeight);
	const FVector BottomSamples[] = {
		BottomCenter,
		BottomCenter + FVector(0.f, ArtHalfDepthWorld, 0.f),
		BottomCenter - FVector(0.f, ArtHalfDepthWorld, 0.f),
	};
	FVector2D ScreenTop;
	FVector2D ScreenBottomCenter;
	if (!PC->ProjectWorldLocationToScreen(WorldTop, ScreenTop, true)
		|| !PC->ProjectWorldLocationToScreen(BottomCenter, ScreenBottomCenter, true)) {
		return false;
	}
	float LowestScreenY = ScreenBottomCenter.Y;
	for (const FVector& Sample : BottomSamples) {
		FVector2D ScreenSample;
		if (PC->ProjectWorldLocationToScreen(Sample, ScreenSample, true)) {
			LowestScreenY = FMath::Max(LowestScreenY, ScreenSample.Y);
		}
	}
	const float ScreenArtHeight = FMath::Abs(ScreenTop.Y - ScreenBottomCenter.Y);
	const float BelowGap = kUnitHoverBelowArtGap3DScreen + ScreenArtHeight * kUnitHoverBelowArtScreenHeightScale;
	FVector2D ScreenPos;
	ScreenPos.X = (ScreenBottomCenter.X + ScreenTop.X) * 0.5f;
	ScreenPos.Y = LowestScreenY + BelowGap;
	const FGeometry PanelGeo = GetCachedGeometry();
	OutPanelLocal = PanelGeo.AbsoluteToLocal(ScreenPos);
	return true;
}

void STacticsBoardPanel::RebuildUnitHoverOverlays()
{
	if (BoardUnitHoverOverlay.IsValid()) {
		BoardUnitHoverOverlay->ClearChildren();
	}
	if (UnitHoverScreenOverlay.IsValid()) {
		UnitHoverScreenOverlay->SetContent(SNullWidget::NullWidget);
		UnitHoverScreenOverlay->SetVisibility(EVisibility::Collapsed);
	}
	if (UnitHoverCellHintLabel.IsValid()) {
		if (HoverCellHintLine.IsEmpty()) {
			UnitHoverCellHintLabel->SetText(FText::GetEmpty());
		} else {
			UnitHoverCellHintLabel->SetText(FText::FromString(HoverCellHintLine));
		}
	}

	if (!HoverUnit.bHasUnit) {
		return;
	}

	const TSharedRef<SWidget> PanelContent = BuildUnitHoverPanelContent(HoverUnit);
	if (Uses3DBoardChrome()) {
		if (UnitHoverScreenOverlay.IsValid()) {
			UnitHoverScreenOverlay->SetContent(PanelContent);
			UnitHoverScreenOverlay->SetVisibility(EVisibility::HitTestInvisible);
			UnitHoverScreenOverlay->Invalidate(EInvalidateWidget::LayoutAndVolatility);
		}
		return;
	}

	if (!BoardUnitHoverOverlay.IsValid()) {
		return;
	}
	float AnchorX = 0.f;
	float AnchorY = 0.f;
	if (!TryComputeUnitHoverGridAnchor(AnchorX, AnchorY)) {
		return;
	}
	BoardUnitHoverOverlay->AddSlot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(AnchorX - kUnitHoverPanelWidth * 0.5f, AnchorY, 0.f, 0.f))
		[
			SNew(SBox)
				.WidthOverride(kUnitHoverPanelWidth)
				[
					PanelContent
				]
		];
}

void STacticsBoardPanel::LoadBoardUiPreferences()
{
	GConfig->GetBool(kBoardUiPrefsSection, TEXT("bShowDevTools"), bShowDevTools, GGameUserSettingsIni);
	GConfig->GetBool(kBoardUiPrefsSection, TEXT("bShowConsoleLog"), bShowConsoleLog, GGameUserSettingsIni);
	GConfig->GetBool(kBoardUiPrefsSection, TEXT("bShowCardGlossaryDefinitions"), bShowCardGlossaryDefinitions, GGameUserSettingsIni);
	GConfig->GetBool(kBoardUiPrefsSection, TEXT("bShowCombatLog"), bShowCombatLog, GGameUserSettingsIni);
	GConfig->GetBool(kBoardUiPrefsSection, TEXT("bP2BotEnabled"), bP2BotEnabled, GGameUserSettingsIni);
	bool bAdvanced = false;
	GConfig->GetBool(kBoardUiPrefsSection, TEXT("bShowAdvancedCardText"), bAdvanced, GGameUserSettingsIni);
	if (Subsystem.IsValid()) {
		Subsystem->SetShowAdvancedCardText(bAdvanced);
	}
	ApplyBotSettingsFromPreferences();
}

void STacticsBoardPanel::ApplyBotSettingsFromPreferences()
{
	if (!Subsystem.IsValid()) {
		return;
	}
	bool bAllow = bP2BotEnabled;
	if (UTacticsGameInstance* TGI = Cast<UTacticsGameInstance>(Subsystem->GetGameInstance())) {
		if (TGI->IsSoloVsAiMatch()) {
			bAllow = true;
			bP2BotEnabled = true;
		}
	}
	if (IsWebSocketClientP2()) {
		bAllow = false;
	} else if (const UGameInstance* GI = Subsystem->GetGameInstance()) {
		if (const UTacticsWebSocketSubsystem* Net = GI->GetSubsystem<UTacticsWebSocketSubsystem>()) {
			if (Net->IsHosting() && Net->GetRemoteWebSocketReadyPeerCount() > 0) {
				bAllow = false;
			}
		}
	}

	// SetControlledPlayer always BroadcastRefresh. Refresh used to call this function, which
	// re-entered forever (EXCEPTION_STACK_OVERFLOW) once Play vs AI forced the bot on.
	if (Subsystem->IsBotSeatEnabled(2) != bAllow) {
		Subsystem->SetBotSeatEnabled(2, bAllow);
	}
	if (Subsystem->IsBotAutoPlayEnabled() != bAllow) {
		Subsystem->SetBotAutoPlayEnabled(bAllow);
	}
	if (bAllow) {
		Subsystem->SetAutoFollowActiveSeat(true);
		if (Subsystem->GetControlledPlayer() != 1) {
			Subsystem->SetControlledPlayer(1);
		}
	}
}

void STacticsBoardPanel::SaveBoardUiPreferences()
{
	GConfig->SetBool(kBoardUiPrefsSection, TEXT("bShowDevTools"), bShowDevTools, GGameUserSettingsIni);
	GConfig->SetBool(kBoardUiPrefsSection, TEXT("bShowConsoleLog"), bShowConsoleLog, GGameUserSettingsIni);
	GConfig->SetBool(kBoardUiPrefsSection, TEXT("bShowCardGlossaryDefinitions"), bShowCardGlossaryDefinitions, GGameUserSettingsIni);
	GConfig->SetBool(kBoardUiPrefsSection, TEXT("bShowCombatLog"), bShowCombatLog, GGameUserSettingsIni);
	GConfig->SetBool(kBoardUiPrefsSection, TEXT("bP2BotEnabled"), bP2BotEnabled, GGameUserSettingsIni);
	const bool bAdvanced = Subsystem.IsValid() && Subsystem->IsShowingAdvancedCardText();
	GConfig->SetBool(kBoardUiPrefsSection, TEXT("bShowAdvancedCardText"), bAdvanced, GGameUserSettingsIni);
	GConfig->Flush(false, GGameUserSettingsIni);
}

/** True if the locally controlled seat is the one that may act right now. */
bool STacticsBoardPanel::CanControlledSeatTakeActions() const
{
	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady() || bCombatScreenActive) {
		return false;
	}
	if (Subsystem->IsAwaitingHandDiscard()) {
		return Subsystem->GetHandViewPlayerId() == Subsystem->GetControlledPlayer();
	}
	if (Subsystem->IsAwaitingScan()) {
		return Subsystem->GetPendingScanPlayerId() == Subsystem->GetControlledPlayer();
	}
	if (Subsystem->IsAwaitingTerritoryLoot()) {
		return true;
	}
	if (Subsystem->IsAnyReactionWindowPhase()) {
		return Subsystem->CanControlledPlayerPassPriority();
	}
	if (Subsystem->IsAnyMainPhase() || Subsystem->IsAnyAttackDeclarationPhase()) {
		return Subsystem->CanControlledPlayerActInMainPhase();
	}
	if (Subsystem->GetPendingEnergyZoneChoiceCount() > 0) {
		return Subsystem->GetActivePlayerId() == Subsystem->GetControlledPlayer();
	}
	return true;
}

bool STacticsBoardPanel::IsWaitingForControlledSeatTurn() const
{
	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady() || bCombatScreenActive) {
		return false;
	}
	return !CanControlledSeatTakeActions();
}

void STacticsBoardPanel::SwitchControlSeat(const int32 Seat)
{
	if (!Subsystem.IsValid() || IsWebSocketClientP2()) {
		return;
	}
	const int32 N = Subsystem->GetMatchPlayerCount();
	if (Seat < 1 || Seat > N) {
		return;
	}
	RunCli(FString::Printf(TEXT("as %d"), Seat));
}

bool STacticsBoardPanel::ShouldAppendToCombatLog(const FString& Line) const
{
	if (Line.IsEmpty()) {
		return false;
	}
	if (Line.Contains(TEXT("Failed:"), ESearchCase::IgnoreCase)) {
		return false;
	}
	static const TCHAR* kKeywords[] = {
		TEXT("damage"),
		TEXT("attack"),
		TEXT("defeat"),
		TEXT("destroy"),
		TEXT("heal"),
		TEXT("cast"),
		TEXT("applied"),
		TEXT("killed"),
		TEXT("combat"),
		TEXT("block"),
		TEXT("shield"),
		TEXT("armor"),
		TEXT("crit"),
		TEXT("overload"),
		TEXT("deploy"),
	};
	for (const TCHAR* Keyword : kKeywords) {
		if (Line.Contains(Keyword, ESearchCase::IgnoreCase)) {
			return true;
		}
	}
	return false;
}

void STacticsBoardPanel::AppendCombatLogLine(const FString& Line)
{
	if (!ShouldAppendToCombatLog(Line)) {
		return;
	}
	FString Clean = Line;
	Clean.ReplaceInline(TEXT("\r"), TEXT(""));
	TArray<FString> Rows;
	Clean.ParseIntoArrayLines(Rows, false);
	for (FString Row : Rows) {
		Row.TrimStartAndEndInline();
		if (Row.IsEmpty() || !ShouldAppendToCombatLog(Row)) {
			continue;
		}
		CombatLogLines.Add(MoveTemp(Row));
	}
	while (CombatLogLines.Num() > MaxCombatLogLines) {
		CombatLogLines.RemoveAt(0);
	}
	RebuildCombatLogPanel();
}

void STacticsBoardPanel::RebuildCombatLogPanel()
{
	if (!CombatLogScroll.IsValid()) {
		return;
	}
	CombatLogScroll->ClearChildren();
	for (int32 i = CombatLogLines.Num() - 1; i >= 0; --i) {
		const FString& Row = CombatLogLines[i];
		CombatLogScroll->AddSlot().Padding(0.f, 0.f, 0.f, 2.f)
			[
				SNew(STextBlock)
					.WrapTextAt(280.f)
					.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
					.ColorAndOpacity(FLinearColor(0.88f, 0.92f, 0.96f, 1.f))
					.Text(FText::FromString(Row))
			];
	}
}

FText STacticsBoardPanel::GetNetworkStatusPillText() const
{
	UTacticsWebSocketSubsystem* Net = nullptr;
	if (!ResolveWebSocketNet(Net) || !Net) {
		return FText::FromString(TEXT("Offline"));
	}
	if (Net->IsHosting()) {
		const int32 Peers = Net->GetRemoteWebSocketReadyPeerCount();
		const TCHAR* Bind = Net->IsListeningPublic() ? TEXT("LAN") : TEXT("local");
		return FText::FromString(FString::Printf(TEXT("Host · %s · :%d · %d remote"), Bind, Net->GetListenPort(), Peers));
	}
	if (Net->IsClientConnecting()) {
		return FText::FromString(FString::Printf(TEXT("Connecting P%d…"), Net->GetClientRemoteSeatPlayerId()));
	}
	if (!Net->GetClientConnectionError().IsEmpty()) {
		return FText::FromString(Net->GetClientConnectionError());
	}
	if (Net->IsClientConnectedToHost()) {
		return FText::FromString(FString::Printf(TEXT("P%d · Connected"), Net->GetClientRemoteSeatPlayerId()));
	}
	return FText::FromString(TEXT("Offline"));
}

FLinearColor STacticsBoardPanel::GetNetworkStatusPillColor() const
{
	UTacticsWebSocketSubsystem* Net = nullptr;
	if (!ResolveWebSocketNet(Net) || !Net) {
		return FLinearColor(0.45f, 0.48f, 0.52f, 0.92f);
	}
	if (Net->IsHosting()) {
		return FLinearColor(0.12f, 0.48f, 0.28f, 0.94f);
	}
	if (Net->IsClientConnecting()) {
		return FLinearColor(0.55f, 0.42f, 0.08f, 0.94f);
	}
	if (!Net->GetClientConnectionError().IsEmpty()) {
		return FLinearColor(0.55f, 0.12f, 0.12f, 0.94f);
	}
	if (Net->IsClientConnectedToHost()) {
		return FLinearColor(0.08f, 0.38f, 0.58f, 0.94f);
	}
	return FLinearColor(0.45f, 0.48f, 0.52f, 0.92f);
}

void STacticsBoardPanel::ActivateHandCardAtIndex(const int32 Idx)
{
	if (!Subsystem.IsValid()) {
		return;
	}
	const bool bDiscard = Subsystem->IsAwaitingHandDiscard();
	const bool bTerritoryLoot = Subsystem->IsAwaitingTerritoryLoot();
	FString Name, CardKind, Cost, RulesUnused;
	if (!Subsystem->TryGetHandCardUi(Idx, Name, CardKind, Cost, RulesUnused)) {
		return;
	}
	if (bDiscard || bTerritoryLoot) {
		DeployHandIndex1Based = Idx;
		bDiscardHandCardSelected = true;
		LastCliOutput = bTerritoryLoot
			? TEXT("Card selected - press Discard to draw (bottom right) or Enter.")
			: TEXT("Card selected - press Discard (bottom right) or Enter.");
		RefreshStatusText();
		Refresh();
		return;
	}
	// Acting out of turn always pops the alert rather than silently doing nothing. Covers main /
	// attack-declaration phases, reaction windows where you hold no priority, and the Energy phase.
	if (IsWaitingForControlledSeatTurn()) {
		ShowFailureAlert(TEXT("Not your turn - wait for your turn or switch Control seat."));
		return;
	}
	if (Subsystem->IsAnyReactionWindowPhase()) {
		if (CardKind.Equals(TEXT("spell"), ESearchCase::IgnoreCase)) {
			FString SpeedTag;
			if (Subsystem->TryGetHandSpellSpeedTag(Idx, SpeedTag)
				&& !SpeedTag.Equals(TEXT("reflex"), ESearchCase::IgnoreCase)
				&& !SpeedTag.Equals(TEXT("blazing"), ESearchCase::IgnoreCase)) {
				ShowFailureAlert(TEXT("Only reflex and blazing spells can be played in reaction windows."));
				return;
			}
		} else {
			ShowFailureAlert(TEXT("Only reflex/blazing spells and abilities can be played in reaction windows."));
			return;
		}
	}
	DeployHandIndex1Based = Idx;
	bHandArmedForTile = true;
	bDetailPanelShowsHandCard = true;
	bReservesArmedForTile = false;
	bAbilityArmedForTile = false;
	ArmedAbilityKey.Empty();
	ArmedMulticastTargetTotal = 0;
	ArmedMulticastTargetsPicked = 0;
	ArmedMulticastCliTokens.Empty();
	HideAbilityDescription();
	if (CardKind.Equals(TEXT("spell"), ESearchCase::IgnoreCase)) {
		bool bModal = false;
		if (Subsystem->TryGetHandSpellIsModal(Idx, bModal) && bModal) {
			bModalSpellPickerActive = true;
			bModalSpellFromReserves = false;
			ArmedSpellModeIndex = -1;
			LastCliOutput = TEXT("Choose a mode - click one of the card copies below.");
			RefreshStatusText();
			Refresh();
			return;
		}
		int32 Mc = 1;
		bool bPerCopy = false;
		if (Subsystem->TryGetHandSpellMulticastInfo(Idx, Mc, bPerCopy) && bPerCopy && Mc > 1) {
			ArmedMulticastTargetTotal = Mc;
		}
		InitArmedXCostForHandSpell(Idx);
		if (IsArmedFocusSpell()) {
			bFocusSpellAwaitingCaster = true;
			bPushSpellAwaitingUnit = false;
			Subsystem->ClearSelectedUnitOnly();
			Subsystem->SetBoardFocusCasterSelectionPreviewForHandCard(Idx);
			const FString XHint = IsArmedXCostSpell()
				? FString::Printf(TEXT(" Set X with bottom-right buttons (currently %d)."), ArmedXCostAmount)
				: FString();
			LastCliOutput = IsArmedDirectionalFocusSpell()
				? FString::Printf(
					TEXT("Focus spell armed - click a highlighted unit to cast from, then pick a direction on the board.%s Cancel play to abort."),
					*XHint)
				: FString::Printf(
					TEXT("Focus spell armed - click a highlighted unit to cast from, then click a highlighted enemy.%s Cancel play to abort."),
					*XHint);
		} else {
			bool bPushDirection = false;
			bFocusSpellAwaitingCaster = false;
			bPushSpellAwaitingUnit = Subsystem->TryGetHandSpellUsesPushDirectionAim(Idx, bPushDirection) && bPushDirection;
			PushSpellTargetWorldX = -1;
			PushSpellTargetWorldY = -1;
			Subsystem->SetBoardTargetPreviewForHandCard(Idx);
			if (bPushSpellAwaitingUnit) {
				LastCliOutput = TEXT("Push spell armed - click a unit to push, then choose a cardinal direction. Cancel play to abort.");
			} else if (IsArmedXCostSpell()) {
				LastCliOutput = FString::Printf(
					TEXT("X-cost spell armed - set X (bottom right, currently %d), then click a highlighted target. Cancel play to abort."),
					ArmedXCostAmount);
			}
		}
	} else if (CardKind.Equals(TEXT("unit"), ESearchCase::IgnoreCase) || CardKind.Equals(TEXT("building"), ESearchCase::IgnoreCase)) {
		bFocusSpellAwaitingCaster = false;
		bPushSpellAwaitingUnit = false;
		Subsystem->SetDeployPreviewForHandCard(Idx);
	} else {
		bFocusSpellAwaitingCaster = false;
		bPushSpellAwaitingUnit = false;
		Subsystem->SetBoardTargetPreviewForHandCard(Idx);
	}
	RefreshStatusText();
	Refresh();
}

void STacticsBoardPanel::HandleHandHotkeySlot(const int32 Slot1Based)
{
	if (bMatchUiHidden || bCombatScreenActive || !Subsystem.IsValid() || !Subsystem->IsMatchReady()) {
		return;
	}
	if (Slot1Based < 1 || Slot1Based > 9) {
		return;
	}
	const int32 N = Subsystem->GetControlledHandCount();
	if (N < 1) {
		return;
	}
	if (Slot1Based > N) {
		return;
	}
	ActivateHandCardAtIndex(Slot1Based);
}

void STacticsBoardPanel::HandleBoardCellHover(const int WorldX, const int WorldY)
{
	if (ShouldSuppressBoardHoverUnderCursor()) {
		ClearBoardCellHover();
		return;
	}
	if (Subsystem.IsValid() && !Subsystem->IsActionQueuePreviewPinned() && Subsystem->IsActionQueueHoverActive()) {
		Subsystem->ClearActionQueueHoverPreview();
		RefreshActionQueueHoverOverlay();
		RefreshBoardTargetHighlights();
	}
	if (WantsDirectionalAreaHoverPreview() && Subsystem.IsValid()
		&& (Subsystem->IsBoardTargetOtherHighlightAtWorld(WorldX, WorldY)
			|| Subsystem->IsBoardTargetEnemyHighlightAtWorld(WorldX, WorldY)
			|| Subsystem->IsBoardTargetAoEHighlightAtWorld(WorldX, WorldY))) {
		Subsystem->SetPendingCliWorldCell(WorldX, WorldY);
	}
	FTacticsUnitHoverPresentation NextUnit;
	FString NextCellHint;
	if (Subsystem.IsValid() && Subsystem->TryBuildBoardHoverAtWorld(WorldX, WorldY, NextUnit, NextCellHint)) {
		HoverUnit = MoveTemp(NextUnit);
		// Action hints live in the unit hover panel; bottom chrome is cell-only.
		HoverCellHintLine = NextUnit.bHasUnit ? FString() : MoveTemp(NextCellHint);
		RebuildUnitHoverOverlays();
	} else {
		ClearBoardCellHover();
	}
}

void STacticsBoardPanel::ClearBoardCellHover()
{
	const bool bHadUnitHover = HoverUnit.bHasUnit;
	const bool bHadCellHint = !HoverCellHintLine.IsEmpty();
	if (!bHadUnitHover && !bHadCellHint) {
		return;
	}
	HoverUnit = {};
	HoverCellHintLine.Empty();
	RebuildUnitHoverOverlays();
	if (UnitHoverScreenOverlay.IsValid()) {
		UnitHoverScreenOverlay->Invalidate(EInvalidateWidget::LayoutAndVolatility);
	}
}

void STacticsBoardPanel::HandleShortcutKey(const FKey& Key)
{
	if (bMatchUiHidden || bCombatScreenActive || !Subsystem.IsValid() || !Subsystem->IsMatchReady()) {
		return;
	}
	if (Key == EKeys::Escape) {
		if (Subsystem->HasPendingMoveForControlledPlayer()) {
			RunCli(TEXT("move_cancel"));
			return;
		}
		if (bHandArmedForTile || bReservesArmedForTile || bAbilityArmedForTile) {
			ClearPlayArmingState();
			LastCliOutput = TEXT("Card / ability play cancelled.");
			RefreshStatusText();
			Refresh();
			return;
		}
		if (Subsystem->HasUnitSelected()) {
			RunCli(TEXT("deselect"));
		} else if (Subsystem.IsValid()) {
			Subsystem->ClearPendingCliWorldCell();
			ClearPlayArmingState();
		}
		return;
	}
	if (Key == EKeys::Z) {
		if (Subsystem->CanControlledPlayerUndo()) {
			RunCli(TEXT("undo"));
		}
		return;
	}
	if (Key == EKeys::P || Key == EKeys::SpaceBar) {
		if (Subsystem->CanControlledPlayerPassPriority()) {
			RunCli(TEXT("pass"));
		}
		return;
	}
	if (Key == EKeys::Enter) {
		TryPrimaryConfirmAction();
		return;
	}
	if (Key == EKeys::RightMouseButton) {
		if (Subsystem->HasPendingMoveForControlledPlayer()) {
			RunCli(TEXT("move_cancel"));
			return;
		}
		if (bHandArmedForTile || bReservesArmedForTile || bAbilityArmedForTile) {
			ClearPlayArmingState();
			LastCliOutput = TEXT("Card / ability play cancelled.");
			RefreshStatusText();
			Refresh();
			return;
		}
		if (Subsystem->HasUnitSelected()) {
			RunCli(TEXT("deselect"));
		}
	}
}

void STacticsBoardPanel::TryPrimaryConfirmAction()
{
	if (!Subsystem.IsValid()) {
		return;
	}
	if (Subsystem->IsAwaitingHandDiscard() && bDiscardHandCardSelected) {
		const int32 N = Subsystem->GetControlledHandCount();
		const int32 Idx = FMath::Clamp(DeployHandIndex1Based, 1, FMath::Max(1, N));
		bDiscardHandCardSelected = false;
		RunCli(FString::Printf(TEXT("discard %d"), Idx));
		return;
	}
	if (Subsystem->IsAwaitingTerritoryLoot() && bDiscardHandCardSelected) {
		const int32 N = Subsystem->GetControlledHandCount();
		const int32 Idx = FMath::Clamp(DeployHandIndex1Based, 1, FMath::Max(1, N));
		bDiscardHandCardSelected = false;
		RunCli(FString::Printf(TEXT("territory_loot_discard %d"), Idx));
		return;
	}
	if (Subsystem->HasPendingMoveForControlledPlayer()) {
		RunCli(TEXT("move_confirm"));
		return;
	}
	if (Subsystem->IsDefenseReactionPhase() && Subsystem->HasPendingAttacksInQueue()) {
		ResolvePendingAttacksWithVisualization();
		return;
	}
	if (Subsystem->IsAnyReactionWindowPhase() && Subsystem->CanControlledPlayerPassPriority()) {
		RunCli(TEXT("pass"));
		return;
	}
	if (Subsystem->IsAnyAttackDeclarationPhase() && Subsystem->CanControlledPlayerCommitAttacks()) {
		RunCli(TEXT("attack_commit"));
		return;
	}
	if (Subsystem->IsAnyMainPhase() && Subsystem->CanControlledPlayerEndTurn()) {
		RunCli(TEXT("end_main"));
		return;
	}
	if (Subsystem->GetPendingEnergyZoneChoiceCount() > 0) {
		RunCli(TEXT("zoneskip"));
	}
}

