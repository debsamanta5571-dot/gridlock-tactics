#include "STacticsBoardPanel.h"

#include "Framework/Application/SlateApplication.h"
#include "TacticsCardArtUi.h"
#include "TacticsCardText.h"
#include "TacticsMatchSubsystem.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/SRichTextBlock.h"
#include "Widgets/Text/STextBlock.h"

#include "STacticsBoardPanel_Internal.h"

FString STacticsBoardPanel::InsertArmedSpellModeIntoCastLine(const FString& BaseCastLine) const
{
	if (ArmedSpellModeIndex < 0) {
		return BaseCastLine;
	}
	TArray<FString> Parts;
	BaseCastLine.ParseIntoArrayWS(Parts);
	if (Parts.Num() < 2) {
		return BaseCastLine;
	}
	FString Out = FString::Printf(TEXT("%s %s mode %d"), *Parts[0], *Parts[1], ArmedSpellModeIndex);
	for (int32 i = 2; i < Parts.Num(); ++i) {
		Out += TEXT(" ") + Parts[i];
	}
	return Out;
}

bool STacticsBoardPanel::GetArmedSpellRequiresBoardCell(bool& bOutRequiresCell) const
{
	bOutRequiresCell = true;
	if (!Subsystem.IsValid()) {
		return false;
	}
	if (bReservesArmedForTile) {
		const int32 N = Subsystem->GetControlledReservesCount();
		if (N < 1) {
			return false;
		}
		const int32 Idx = FMath::Clamp(DeployReservesIndex1Based, 1, N);
		if (ArmedSpellModeIndex >= 0) {
			return Subsystem->TryGetReservesSpellModeRequiresBoardCell(Idx, ArmedSpellModeIndex, bOutRequiresCell);
		}
		return Subsystem->TryGetReservesSpellRequiresBoardCell(Idx, bOutRequiresCell);
	}
	if (!bHandArmedForTile) {
		return false;
	}
	const int32 N = Subsystem->GetControlledHandCount();
	if (N < 1) {
		return false;
	}
	const int32 Idx = FMath::Clamp(DeployHandIndex1Based, 1, N);
	if (ArmedSpellModeIndex >= 0) {
		return Subsystem->TryGetHandSpellModeRequiresBoardCell(Idx, ArmedSpellModeIndex, bOutRequiresCell);
	}
	return Subsystem->TryGetHandSpellRequiresBoardCell(Idx, bOutRequiresCell);
}

void STacticsBoardPanel::BeginArmedSpellTargetingAfterModePick()
{
	if (!Subsystem.IsValid()) {
		return;
	}
	const bool bReserves = bReservesArmedForTile;
	const int32 Idx = bReserves ? DeployReservesIndex1Based : DeployHandIndex1Based;

	bool bRequiresCell = true;
	if (!GetArmedSpellRequiresBoardCell(bRequiresCell) || !bRequiresCell) {
		const FString Base = bReserves
			? FString::Printf(TEXT("cast_reserve %d"), Idx)
			: FString::Printf(TEXT("cast %d"), Idx);
		RunArmedCastCli(Base);
		return;
	}

	ArmedMulticastTargetTotal = 0;
	ArmedMulticastTargetsPicked = 0;
	ArmedMulticastCliTokens.Empty();
	bFocusSpellAwaitingCaster = false;
	bPushSpellAwaitingUnit = false;
	PushSpellTargetWorldX = -1;
	PushSpellTargetWorldY = -1;

	if (bReserves) {
		int32 Mc = 1;
		bool bPerCopy = false;
		if (Subsystem->TryGetReservesSpellMulticastInfo(Idx, Mc, bPerCopy) && bPerCopy && Mc > 1) {
			ArmedMulticastTargetTotal = Mc;
		}
		InitArmedXCostForReservesSpell(Idx);
		if (IsArmedFocusSpell()) {
			bFocusSpellAwaitingCaster = true;
			Subsystem->ClearSelectedUnitOnly();
			Subsystem->SetBoardFocusCasterSelectionPreviewForReservesCard(Idx);
			LastCliOutput = IsArmedXCostSpell()
				? FString::Printf(
					TEXT("Reserves: spell armed - set X (bottom right, currently %d), click a highlighted unit to cast from, then click a target."),
					ArmedXCostAmount)
				: TEXT("Reserves: spell armed - click a highlighted unit to cast from, then click a target.");
		} else {
			bool bPushDirection = false;
			bPushSpellAwaitingUnit =
				Subsystem->TryGetReservesSpellUsesPushDirectionAim(Idx, bPushDirection) && bPushDirection;
			Subsystem->SetBoardTargetPreviewForReservesCardMode(Idx, ArmedSpellModeIndex);
			LastCliOutput = bPushSpellAwaitingUnit
				? TEXT("Reserves: Push spell armed - click a unit to push, then choose a cardinal direction.")
				: TEXT("Reserves armed - click a highlighted tile to cast (your turn only).");
		}
	} else {
		int32 Mc = 1;
		bool bPerCopy = false;
		if (Subsystem->TryGetHandSpellMulticastInfo(Idx, Mc, bPerCopy) && bPerCopy && Mc > 1) {
			ArmedMulticastTargetTotal = Mc;
		}
		InitArmedXCostForHandSpell(Idx);
		if (IsArmedFocusSpell()) {
			bFocusSpellAwaitingCaster = true;
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
			bPushSpellAwaitingUnit = Subsystem->TryGetHandSpellUsesPushDirectionAim(Idx, bPushDirection) && bPushDirection;
			Subsystem->SetBoardTargetPreviewForHandCardMode(Idx, ArmedSpellModeIndex);
			if (bPushSpellAwaitingUnit) {
				LastCliOutput = TEXT("Push spell armed - click a unit to push, then choose a cardinal direction. Cancel play to abort.");
			} else if (IsArmedXCostSpell()) {
				LastCliOutput = FString::Printf(
					TEXT("X-cost spell armed - set X (bottom right, currently %d), then click a highlighted target. Cancel play to abort."),
					ArmedXCostAmount);
			} else {
				LastCliOutput = TEXT("Spell armed - click a highlighted target. Cancel play to abort.");
			}
		}
	}
	RefreshStatusText();
	Refresh();
}

void STacticsBoardPanel::OnModalSpellModePicked(const int32 ModeIndex0)
{
	ArmedSpellModeIndex = ModeIndex0;
	bModalSpellPickerActive = false;
	BeginArmedSpellTargetingAfterModePick();
}

void STacticsBoardPanel::RebuildModalSpellPicker()
{
	if (!ModalSpellPickerRow.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	ModalSpellPickerRow->ClearChildren();
	if (!bModalSpellPickerActive) {
		return;
	}

	const bool bReserves = bModalSpellFromReserves;
	const int32 Idx = bReserves ? DeployReservesIndex1Based : DeployHandIndex1Based;
	const int32 ModeCount = bReserves
		? Subsystem->GetReservesSpellModalModeCount(Idx)
		: Subsystem->GetHandSpellModalModeCount(Idx);
	if (ModeCount < 1) {
		return;
	}

	FString CardName, CardKind, CostUnused, RulesUnused, ArtId;
	if (bReserves) {
		if (!Subsystem->TryGetReservesCardUi(Idx, CardName, CardKind, CostUnused, RulesUnused)) {
			return;
		}
		Subsystem->TryGetReservesCardArtId(Idx, ArtId);
	} else {
		if (!Subsystem->TryGetHandCardUi(Idx, CardName, CardKind, CostUnused, RulesUnused)) {
			return;
		}
		Subsystem->TryGetHandCardArtId(Idx, ArtId);
	}

	const FVector2D CardSize(132.f, 184.f);
	for (int32 ModeIdx = 0; ModeIdx < ModeCount; ++ModeIdx) {
		FString Label, Rules;
		const bool bGotMode = bReserves
			? Subsystem->TryGetReservesSpellModalModeUi(Idx, ModeIdx, Label, Rules)
			: Subsystem->TryGetHandSpellModalModeUi(Idx, ModeIdx, Label, Rules);
		if (!bGotMode) {
			Label = FString::Printf(TEXT("Mode %d"), ModeIdx + 1);
		}
		FString RulesSnippet = Rules;
		if (RulesSnippet.Len() > 72) {
			RulesSnippet = RulesSnippet.Left(71) + TEXT("…");
		}

		ModalSpellPickerRow->AddSlot()
			.AutoWidth()
			.Padding(0.f, 0.f, 14.f, 0.f)
			[
				SNew(SButton)
					.ButtonColorAndOpacity(FLinearColor(0.06f, 0.06f, 0.08f, 1.f))
					.ContentPadding(FMargin(4.f))
					.OnClicked_Lambda([this, ModeIdx]() {
						OnModalSpellModePicked(ModeIdx);
						return FReply::Handled();
					})
					[
						SNew(SBorder)
							.BorderBackgroundColor(FLinearColor(0.22f, 0.18f, 0.10f, 1.f))
							.Padding(FMargin(2.f))
							[
								SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight()
									[
										SNew(SBox).WidthOverride(CardSize.X).HeightOverride(CardSize.Y)
											[
												SNew(SOverlay)
												+ SOverlay::Slot()
													[
														SNew(SImage)
															.Image_Lambda([ArtId, CardSize]() -> const FSlateBrush* {
																return ArtId.IsEmpty()
																	? nullptr
																	: TacticsCardArtUi::GetCardArtBrush(ArtId, CardSize);
															})
													]
												+ SOverlay::Slot()
													  .VAlign(VAlign_Bottom)
													  .Padding(FMargin(4.f, 0.f, 4.f, 4.f))
													  [
														  SNew(SBorder)
															  .BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.82f))
															  .Padding(FMargin(6.f, 4.f))
															  [
																  SNew(STextBlock)
																	  .Font(TacticsCardText::DefaultFont(TEXT("Bold"), 10))
																	  .ColorAndOpacity(kCardTextWhite)
																	  .Justification(ETextJustify::Center)
																	  .WrapTextAt(CardSize.X - 12.f)
																	  .Text(FText::FromString(Label))
															  ]
													  ]
											]
									]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
									[
										SNew(STextBlock)
											.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
											.ColorAndOpacity(FLinearColor(0.85f, 0.85f, 0.88f, 1.f))
											.Justification(ETextJustify::Center)
											.WrapTextAt(CardSize.X + 8.f)
											.Text(FText::FromString(RulesSnippet))
									]
							]
					]
			];
	}
}
