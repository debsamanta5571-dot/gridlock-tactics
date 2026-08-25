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


void STacticsBoardPanel::ArmSelectedAbilityForTargeting(const FString& AbilityKey, bool bNeedsBoardTarget)
{
	bool bNeedStack = false;
	if (Subsystem.IsValid() && Subsystem->TryGetSelectedAbilityRequiresStackTarget(AbilityKey, bNeedStack) && bNeedStack) {
		bAbilityArmedForTile = true;
		ArmedAbilityKey = AbilityKey;
		bHandArmedForTile = false;
		InitArmedXCostForAbility(AbilityKey);
		Subsystem->ClearBoardTargetPreview();
		ShowAbilityDescription(AbilityKey);
		const ETacticsAbilityVisualGroup Group = Subsystem.IsValid()
			? Subsystem->ResolveAbilityVisualGroup(AbilityKey)
			: ETacticsAbilityVisualGroup::StackTarget;
		LastCliOutput = IsArmedXCostAbility()
			? FString::Printf(TEXT("Ability '%s' armed - %s (X matches batched spell cost)."), *AbilityKey,
				*TacticsAbilityVisual::GroupPrompt(Group))
			: FString::Printf(TEXT("Ability '%s' armed - %s"), *AbilityKey, *TacticsAbilityVisual::GroupPrompt(Group));
		RefreshStatusText();
		Refresh();
		return;
	}

	if (bNeedsBoardTarget) {
		bAbilityArmedForTile = true;
		ArmedAbilityKey = AbilityKey;
		bHandArmedForTile = false;
		InitArmedXCostForAbility(AbilityKey);
		if (Subsystem.IsValid()) {
			Subsystem->SetBoardTargetPreviewForSelectedAbility(AbilityKey);
		}
		ShowAbilityDescription(AbilityKey);
		const ETacticsAbilityVisualGroup Group = Subsystem.IsValid()
			? Subsystem->ResolveAbilityVisualGroup(AbilityKey)
			: ETacticsAbilityVisualGroup::BoardEntityTarget;
		LastCliOutput = FString::Printf(TEXT("Ability '%s' armed - %s"), *AbilityKey, *TacticsAbilityVisual::GroupPrompt(Group));
		RefreshStatusText();
		Refresh();
		return;
	}

	HideAbilityDescription();
	RunAbilityCli(FString::Printf(TEXT("ability %s"), *AbilityKey), AbilityKey, {});
	ClearPlayArmingState();
}

void STacticsBoardPanel::RunAbilityCli(const FString& Line, const FString& AbilityKey, const TArray<FIntPoint>& TargetCells)
{
	(void)AbilityKey;
	(void)TargetCells;
	RunCli(Line);
}

void STacticsBoardPanel::RunResetSandbox()
{
	if (!Subsystem.IsValid()) {
		return;
	}
	Subsystem->ResetSandboxMatch();
	LastCliOutput = FString::Printf(
		TEXT("Sandbox reset: 8x12 board with bases, full catalog hand, %d ACM territories per seat (3 of each unique, basics fill the rest)."),
		tactics::kSandboxTerritoriesPerPlayer);
	ClearPlayArmingState();
	Refresh();
}

void STacticsBoardPanel::RunResetDemo()
{
	if (!Subsystem.IsValid()) {
		return;
	}
	if (UGameInstance* GI = Subsystem->GetGameInstance()) {
		if (UTacticsWebSocketSubsystem* Net = GI->GetSubsystem<UTacticsWebSocketSubsystem>()) {
			if (Net->IsHosting()) {
				FTacticsMatchSetupProfile Profile;
				Profile.bAutoFollowActiveSeat = false;
				Profile.bSeedDemoState = true;
				int32 Seats = FMath::Max(2, 1 + Net->GetRemoteWebSocketReadyPeerCount());
				if (const UTacticsGameInstance* TGI = Cast<UTacticsGameInstance>(GI)) {
					const UTacticsGameInstance::FTacticsMatchSettings& S = TGI->GetPendingMatchSettings();
					Profile.bTeam2v2 = S.bTeam2v2;
					Profile.bObjScanner = S.bObjScanner;
					Profile.bObjOmni = S.bObjOmni;
					Profile.bObjAether = S.bObjAether;
					Profile.bGiveFieldRequisition = S.bGiveFieldRequisition;
					Seats = FMath::Max(S.bTeam2v2 ? 4 : 2, Seats);
				}
				Subsystem->ResetMatchWithProfile(Seats, Profile);
				Subsystem->SetControlledPlayer(1);
				Net->NotifyClientsDemoReset();
				LastCliOutput = FString::Printf(TEXT("Demo reset for %d seat(s) (host settings preserved)."), Seats);
			} else {
				Subsystem->ResetMatchToPlayerCount(LocalDemoPlayerCount);
				Net->NotifyClientsDemoReset();
				LastCliOutput = FString::Printf(TEXT("Demo reset (%d local seats)."), LocalDemoPlayerCount);
			}
		} else {
			Subsystem->ResetMatchToPlayerCount(LocalDemoPlayerCount);
			LastCliOutput = FString::Printf(TEXT("Demo reset (%d local seats)."), LocalDemoPlayerCount);
		}
	} else {
		Subsystem->ResetMatchToPlayerCount(LocalDemoPlayerCount);
		LastCliOutput = FString::Printf(TEXT("Demo reset (%d local seats)."), LocalDemoPlayerCount);
	}
	ApplyBotSettingsFromPreferences();
	ClearPlayArmingState();
	RefreshStatusText();
	Refresh();
}

void STacticsBoardPanel::RebuildSeatSwitcher()
{
	if (!SeatSwitcherRow.IsValid()) {
		return;
	}
	SeatSwitcherRow->ClearChildren();
	if (!Subsystem.IsValid() || IsWebSocketClientP2()) {
		return;
	}
	const int32 N = Subsystem->GetMatchPlayerCount();
	if (N <= 1) {
		return;
	}
	const int32 ActiveSeat = Subsystem->GetControlledPlayer();
	for (int32 Seat = 1; Seat <= N; ++Seat) {
		const bool bCurrent = (Seat == ActiveSeat);
		const FLinearColor Bg = bCurrent ? FLinearColor(0.22f, 0.42f, 0.28f, 1.f) : FLinearColor(0.14f, 0.16f, 0.20f, 1.f);
		SeatSwitcherRow->AddSlot().AutoWidth().Padding(0.f, 0.f, 4.f, 0.f)
			[
				SNew(SButton)
					.IsEnabled(!bCurrent)
					.ButtonColorAndOpacity(Bg)
					.ContentPadding(FMargin(14.f, 7.f))
					.OnClicked_Lambda([this, Seat]() {
						SwitchControlSeat(Seat);
						return FReply::Handled();
					})
					[
						SNew(STextBlock)
							.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 11))
							.ColorAndOpacity(kCardTextWhite)
							.Text(FText::FromString(FString::Printf(TEXT("P%d"), Seat)))
					]
			];
	}
}

void STacticsBoardPanel::RebuildDiscardModal()
{
	if (!DiscardModalList.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	DiscardModalList->ClearChildren();
	if (!Subsystem->IsAwaitingHandDiscard()) {
		return;
	}
	const int32 N = Subsystem->GetControlledHandCount();
	for (int32 Idx = 1; Idx <= N; ++Idx) {
		FString Name, CardKind, Cost, RulesUnused;
		if (!Subsystem->TryGetHandCardUi(Idx, Name, CardKind, Cost, RulesUnused)) {
			continue;
		}
		if (Name.Len() > 28) {
			Name = Name.Left(27) + TEXT("…");
		}
		const FString Line = FString::Printf(TEXT("%s  (%s)"), *Name, *Cost);
		DiscardModalList->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 4.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(STextBlock)
							.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 9))
							.ColorAndOpacity(kCardTextWhite)
							.Text(FText::FromString(Line))
					]
				+ SHorizontalBox::Slot().AutoWidth()
					[
						SNew(SButton)
							.ContentPadding(FMargin(14.f, 6.f))
							.OnClicked_Lambda([this, Idx]() {
								RunCli(FString::Printf(TEXT("discard %d"), Idx));
								bDiscardHandCardSelected = false;
								return FReply::Handled();
							})
							[
								SNew(STextBlock)
									.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 11))
									.ColorAndOpacity(kCardTextWhite)
									.Text(FText::FromString(TEXT("Discard")))
							]
					]
			];
	}
}

void STacticsBoardPanel::RebuildActionBar()
{
	if (!ActionBar.IsValid()) {
		return;
	}
	ActionBar->ClearChildren();

	auto AddBtn = [this](const TCHAR* Label, FOnClicked Click) {
		ActionBar->AddSlot().AutoWidth().Padding(0.f, 0.f, 8.f, 4.f)[MakeCmdButton(FText::FromString(Label), Click)];
	};

	auto AddCliShortcuts = [this, &AddBtn]() {
		if (!bShowDevTools) {
			return;
		}
		// Remote WebSocket clients have a fixed seat (`as` is rejected in RunCli); they still need
		// Hand / Zones / Float to inspect state and pay costs the same way as host/local play.
		if (!IsWebSocketClientP2()) {
			const int32 NP = Subsystem.IsValid() ? Subsystem->GetMatchPlayerCount() : 0;
			for (int32 Si = 1; Si <= NP; ++Si) {
				const int32 Seat = Si;
				const FString ControlLabel = FString::Printf(TEXT("Control P%d"), Seat);
				AddBtn(*ControlLabel,
					FOnClicked::CreateLambda([this, Seat]() {
						RunCli(FString::Printf(TEXT("as %d"), Seat));
						return FReply::Handled();
					}));
			}
		}
		AddBtn(TEXT("Hand"),
			FOnClicked::CreateLambda([this]() {
				RunCli(TEXT("hand"));
				return FReply::Handled();
			}));
		AddBtn(TEXT("Reserves"),
			FOnClicked::CreateLambda([this]() {
				RunCli(TEXT("reserves"));
				return FReply::Handled();
			}));
		AddBtn(TEXT("Territories"),
			FOnClicked::CreateLambda([this]() {
				RunCli(TEXT("zones"));
				return FReply::Handled();
			}));
		AddBtn(TEXT("Discard pile"),
			FOnClicked::CreateLambda([this]() {
				bShowDiscardPilePanel = !bShowDiscardPilePanel;
				RebuildDiscardPilePanel();
				Refresh();
				return FReply::Handled();
			}));
		AddBtn(TEXT("Purgatory"),
			FOnClicked::CreateLambda([this]() {
				bShowPurgatoryPanel = !bShowPurgatoryPanel;
				RebuildPurgatoryPanel();
				Refresh();
				return FReply::Handled();
			}));
		AddBtn(TEXT("Float"),
			FOnClicked::CreateLambda([this]() {
				RunCli(TEXT("float"));
				return FReply::Handled();
			}));
		AddBtn(Subsystem.IsValid() && Subsystem->IsTurnOrderViewEnabled() ? TEXT("Turn order ON") : TEXT("Turn order"),
			FOnClicked::CreateLambda([this]() {
				if (Subsystem.IsValid()) {
					Subsystem->SetTurnOrderViewEnabled(!Subsystem->IsTurnOrderViewEnabled());
					LastCliOutput = Subsystem->IsTurnOrderViewEnabled()
						? TEXT("Turn order view: showing passive-action order (1 = oldest).")
						: TEXT("Turn order view off.");
					RefreshStatusText();
					Refresh();
				}
				return FReply::Handled();
			}));
	};

	auto AddLocalSeatControls = [this, &AddBtn]() {
		if (!Subsystem.IsValid() || IsWebSocketClientP2()) {
			return;
		}
		AddBtn(TEXT("Local seats −"),
			FOnClicked::CreateLambda([this]() {
				LocalDemoPlayerCount = FMath::Clamp(LocalDemoPlayerCount - 1, 1, 12);
				LastCliOutput = FString::Printf(TEXT("Next offline reset / new local: %d seats."), LocalDemoPlayerCount);
				RefreshStatusText();
				Refresh();
				return FReply::Handled();
			}));
		AddBtn(TEXT("Local seats +"),
			FOnClicked::CreateLambda([this]() {
				LocalDemoPlayerCount = FMath::Clamp(LocalDemoPlayerCount + 1, 1, 12);
				LastCliOutput = FString::Printf(TEXT("Next offline reset / new local: %d seats."), LocalDemoPlayerCount);
				RefreshStatusText();
				Refresh();
				return FReply::Handled();
			}));
		AddBtn(*FString::Printf(TEXT("New local (%dP)"), LocalDemoPlayerCount),
			FOnClicked::CreateLambda([this]() {
				if (!Subsystem.IsValid()) {
					return FReply::Handled();
				}
				if (UGameInstance* GI = Subsystem->GetGameInstance()) {
					if (UTacticsWebSocketSubsystem* Net = GI->GetSubsystem<UTacticsWebSocketSubsystem>(); Net && Net->IsHosting()) {
						FTacticsMatchSetupProfile Profile;
						Profile.bAutoFollowActiveSeat = false;
						Profile.bSeedDemoState = false;
						int32 Seats = LocalDemoPlayerCount;
						if (const UTacticsGameInstance* TGI = Cast<UTacticsGameInstance>(GI)) {
							const UTacticsGameInstance::FTacticsMatchSettings& S = TGI->GetPendingMatchSettings();
							Profile.bTeam2v2 = S.bTeam2v2;
							Profile.bObjScanner = S.bObjScanner;
							Profile.bObjOmni = S.bObjOmni;
							Profile.bObjAether = S.bObjAether;
							Profile.bGiveFieldRequisition = S.bGiveFieldRequisition;
							Seats = S.bTeam2v2 ? 4 : 2;
						}
						Subsystem->ResetMatchWithProfile(Seats, Profile);
						Subsystem->SetControlledPlayer(1);
						LastCliOutput = FString::Printf(TEXT("New host match: %d seats (menu settings)."), Seats);
					} else {
						Subsystem->ResetMatchToPlayerCount(LocalDemoPlayerCount);
						LastCliOutput = FString::Printf(TEXT("New local game: %d seats."), LocalDemoPlayerCount);
					}
				} else {
					Subsystem->ResetMatchToPlayerCount(LocalDemoPlayerCount);
					LastCliOutput = FString::Printf(TEXT("New local game: %d seats."), LocalDemoPlayerCount);
				}
				if (UGameInstance* GI = Subsystem->GetGameInstance()) {
					if (UTacticsWebSocketSubsystem* Net = GI->GetSubsystem<UTacticsWebSocketSubsystem>()) {
						Net->NotifyClientsDemoReset();
					}
				}
				ClearPlayArmingState();
				RefreshStatusText();
				Refresh();
				return FReply::Handled();
			}));
	};

	if (bShowDevTools) {
		AddLocalSeatControls();
	}

	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady()) {
		return;
	}

	if (bShowDevTools) {
		if (UGameInstance* GI = Subsystem->GetGameInstance()) {
			if (UTacticsWebSocketSubsystem* Net = GI->GetSubsystem<UTacticsWebSocketSubsystem>()) {
				AddBtn(TEXT("WS Host (P1)"),
					FOnClicked::CreateLambda([this, Net]() {
						Net->StartHost(8788, /*bBindPublic=*/true, /*bResetMatch=*/true);
						LastCliOutput = TEXT(
							"WebSocket host on 0.0.0.0:8788 - authority P1. Seats capped at match size (2 or 4). Clients: ws://<lan-ip>:8788/");
						RefreshStatusText();
						Refresh();
						return FReply::Handled();
					}));
				AddBtn(TEXT("WS Client (remote)"),
					FOnClicked::CreateLambda([this, Net]() {
						Net->ConnectClient(TEXT("ws://127.0.0.1:8788/"));
						LastCliOutput = TEXT(
							"Connecting to ws://127.0.0.1:8788/ - Unreal host assigns P2+. Catalog mismatch or disconnect returns to menu.");
						RefreshStatusText();
						Refresh();
						return FReply::Handled();
					}));
				AddBtn(TEXT("WS Stop"),
					FOnClicked::CreateLambda([this, Net]() {
						Net->Disconnect();
						Net->StopHost();
						LastCliOutput = TEXT("WebSocket networking stopped.");
						RefreshStatusText();
						Refresh();
						return FReply::Handled();
					}));
			}
		}
	}

	/** End-of-turn discard: select a hand card, then use Discard in the bottom-right bar. */
	if (Subsystem->IsAwaitingHandDiscard()) {
		if (bDiscardHandCardSelected) {
			AddBtn(TEXT("Discard selected card"),
				FOnClicked::CreateLambda([this]() {
					if (Subsystem.IsValid()) {
						const int32 N = Subsystem->GetControlledHandCount();
						const int32 Idx = FMath::Clamp(DeployHandIndex1Based, 1, FMath::Max(1, N));
						bDiscardHandCardSelected = false;
						RunCli(FString::Printf(TEXT("discard %d"), Idx));
					}
					return FReply::Handled();
				}));
		}
		AddCliShortcuts();
		return;
	}

	// ---- Zone / Energy phase ------------------------------------------------
	if (Subsystem->IsEnergyPhase()) {
		AddCliShortcuts();
		AddBtn(TEXT("Skip Territory"),
			FOnClicked::CreateLambda([this]() {
				RunCli(TEXT("zoneskip"));
				return FReply::Handled();
			}));
		return;
	}

	// ---- Reaction windows (Spell Window / Defense) --------------------------
	// Pass is the key action; fast spells can still be cast from the hand strip.
	if (Subsystem->IsAnyReactionWindowPhase()) {
		if (Subsystem->IsDefenseReactionPhase() && Subsystem->HasPendingAttacksInQueue()) {
			AddBtn(TEXT("Resolve attacks"),
				FOnClicked::CreateLambda([this]() {
					ResolvePendingAttacksWithVisualization();
					return FReply::Handled();
				}));
		}
		if (Subsystem->CanControlledPlayerPassPriority()) {
			AddBtn(TEXT("Pass"),
				FOnClicked::CreateLambda([this]() {
					RunCli(TEXT("pass"));
					return FReply::Handled();
				}));
		}

		if (Subsystem->CanControlledPlayerUndo()) {
			AddBtn(TEXT("Undo"),
				FOnClicked::CreateLambda([this]() {
					RunCli(TEXT("undo"));
					return FReply::Handled();
				}));
		}
		// Allow ability targeting during reaction windows
		FString CellErrReact;
		const bool bHasCellReact = EnsureCliCell(CellErrReact);
		if (bHasCellReact || bAbilityArmedForTile) {
			AddBtn(TEXT("Clear grid target"),
				FOnClicked::CreateLambda([this]() {
					if (Subsystem.IsValid()) Subsystem->ClearPendingCliWorldCell();
					ClearPlayArmingState();
					LastCliOutput = TEXT("Grid target cleared.");
					RefreshStatusText();
					Refresh();
					return FReply::Handled();
				}));
		}
		if (Subsystem->HasUnitSelected()) {
			AddBtn(TEXT("Deselect"),
				FOnClicked::CreateLambda([this]() {
					RunCli(TEXT("deselect"));
					return FReply::Handled();
				}));
			if (Subsystem->IsSelectedUnitControlled() && !Subsystem->IsSelectedUnitJammed()) {
				const int32 NAbl = Subsystem->GetSelectedUnitActivatedAbilityCount();
				for (int32 Ai = 1; Ai <= NAbl; ++Ai) {
					FString AKey, ALab, ASpeed, ACost;
					bool bUsed = false, bNeedCell = false;
					FString ARangeToken;
					int32 AUsesRemaining = 0;
					int32 AUsesMax = 0;
					if (!Subsystem->TryGetSelectedUnitActivatedAbilityUi(Ai, AKey, ALab, ASpeed, ACost, bUsed, bNeedCell, ARangeToken,
							AUsesRemaining, AUsesMax)
						|| bUsed)
						continue;
					// Only show reflex/blazing abilities during reaction windows
					if (!ASpeed.Equals(TEXT("reflex"), ESearchCase::IgnoreCase) && !ASpeed.Equals(TEXT("blazing"), ESearchCase::IgnoreCase))
						continue;
					const FString BtnLabel = FString::Printf(TEXT("Ab:%s"), *ALab.Left(14));
					AddBtn(*BtnLabel, FOnClicked::CreateLambda([this, AKey, bNeedCell]() {
						ArmSelectedAbilityForTargeting(AKey, bNeedCell);
						return FReply::Handled();
					}));
				}
			}
		} else if (bHasCellReact) {
			AddBtn(TEXT("Select unit"),
				FOnClicked::CreateLambda([this]() { return DispatchCliFromPendingCellVerb(TEXT("select")); }));
		}
		AddCliShortcuts();
		return;
	}

	// ---- Attack Declaration phases ------------------------------------------
	if (Subsystem->IsAnyAttackDeclarationPhase()) {
		{
			TArray<FString> UndeclareIds;
			TArray<FString> UndeclareLabels;
			Subsystem->GetControlledOpenAttackUndeclareOptions(UndeclareIds, UndeclareLabels);
			for (int32 Ui = 0; Ui < UndeclareIds.Num(); ++Ui) {
				const FString EntityId = UndeclareIds[Ui];
				const FString BtnLabel = UndeclareLabels.IsValidIndex(Ui) ? UndeclareLabels[Ui] : EntityId;
				AddBtn(*BtnLabel.Left(28), FOnClicked::CreateLambda([this, EntityId]() {
					RunCli(FString::Printf(TEXT("attack_undeclare %s"), *EntityId));
					return FReply::Handled();
				}));
			}
		}
		if (Subsystem->CanControlledPlayerCommitAttacks()) {
			AddBtn(TEXT("Commit attacks"),
				FOnClicked::CreateLambda([this]() {
					RunCli(TEXT("attack_commit"));
					return FReply::Handled();
				}));
		}
		FString CellErrAtk;
		const bool bHasCellAtk = EnsureCliCell(CellErrAtk);
		if (bHasCellAtk || bAbilityArmedForTile) {
			AddBtn(TEXT("Clear grid target"),
				FOnClicked::CreateLambda([this]() {
					if (Subsystem.IsValid()) Subsystem->ClearPendingCliWorldCell();
					ClearPlayArmingState();
					LastCliOutput = TEXT("Grid target cleared.");
					RefreshStatusText();
					Refresh();
					return FReply::Handled();
				}));
		}
		if (Subsystem->HasUnitSelected()) {
			AddBtn(TEXT("Deselect"),
				FOnClicked::CreateLambda([this]() {
					RunCli(TEXT("deselect"));
					return FReply::Handled();
				}));
			const bool bSelectedControlled = Subsystem->IsSelectedUnitControlled();
		
	if (Subsystem->HasPendingMoveForControlledPlayer()) {
				AddBtn(TEXT("Rotate CW"),
					FOnClicked::CreateLambda([this]() {
						RunCli(TEXT("move_rotate cw"));
						return FReply::Handled();
					}));
				AddBtn(TEXT("Rotate CCW"),
					FOnClicked::CreateLambda([this]() {
						RunCli(TEXT("move_rotate ccw"));
						return FReply::Handled();
					}));
			}
			if (bHasCellAtk && bSelectedControlled) {
				AddBtn(TEXT("Attack"),
					FOnClicked::CreateLambda([this]() { return DispatchCliFromPendingCellVerb(TEXT("attack")); }));
			}
		} else if (bHasCellAtk) {
			AddBtn(TEXT("Select unit"),
				FOnClicked::CreateLambda([this]() { return DispatchCliFromPendingCellVerb(TEXT("select")); }));
		}
		if (Subsystem->HasUnitSelected() && Subsystem->IsSelectedUnitControlled() && !Subsystem->IsSelectedUnitJammed()) {
			const int32 NAbl = Subsystem->GetSelectedUnitActivatedAbilityCount();
			for (int32 Ai = 1; Ai <= NAbl; ++Ai) {
				FString AKey, ALab, ASpeed, ACost;
				bool bUsed = false;
				bool bNeedCell = false;
				FString ARangeToken;
				int32 AUsesRemaining = 0;
				int32 AUsesMax = 0;
				if (!Subsystem->TryGetSelectedUnitActivatedAbilityUi(Ai, AKey, ALab, ASpeed, ACost, bUsed, bNeedCell, ARangeToken,
						AUsesRemaining, AUsesMax)
					|| bUsed
					|| (!ASpeed.Equals(TEXT("reflex"), ESearchCase::IgnoreCase)
						&& !ASpeed.Equals(TEXT("blazing"), ESearchCase::IgnoreCase))) {
					continue;
				}
				const FString BtnLabel = FString::Printf(TEXT("Ab:%s"), *ALab.Left(14));
				AddBtn(*BtnLabel, FOnClicked::CreateLambda([this, AKey, bNeedCell]() {
					ArmSelectedAbilityForTargeting(AKey, bNeedCell);
					return FReply::Handled();
				}));
			}
		}
		AddCliShortcuts();
		return;
	}

	// ---- Main / Second Main phase -------------------------------------------
	if (!Subsystem->IsAnyMainPhase()) {
		AddCliShortcuts();
		return;
	}
	if (Subsystem->CanControlledPlayerEndTurn()) {
		AddBtn(TEXT("End main phase"),
			FOnClicked::CreateLambda([this]() {
				RunCli(TEXT("end_main"));
				return FReply::Handled();
			}));
	}
	FString CellErr;
	const bool bHasCell = EnsureCliCell(CellErr);

	if (bHasCell || bHandArmedForTile || bReservesArmedForTile || bAbilityArmedForTile) {
		AddBtn(TEXT("Clear grid target"),
			FOnClicked::CreateLambda([this]() {
				if (Subsystem.IsValid()) {
					Subsystem->ClearPendingCliWorldCell();
				}
				ClearPlayArmingState();
				LastCliOutput = TEXT("Grid target cleared.");
				RefreshStatusText();
				Refresh();
				return FReply::Handled();
			}));
	}

	if (Subsystem->HasUnitSelected()) {
		AddBtn(TEXT("Deselect"),
			FOnClicked::CreateLambda([this]() {
				RunCli(TEXT("deselect"));
				return FReply::Handled();
			}));
		const bool bSelectedControlled = Subsystem->IsSelectedUnitControlled();
		if (Subsystem->HasPendingMoveForControlledPlayer()) {
			AddBtn(TEXT("Rotate CW (pending move)"),
				FOnClicked::CreateLambda([this]() {
					RunCli(TEXT("move_rotate cw"));
					if (Subsystem.IsValid()) {
						LastCliOutput = FString::Printf(TEXT("Pending footprint rotation (quarters CW): %+d"),
							Subsystem->GetPendingMoveQuarterTurnsCw());
						RefreshStatusText();
					}
					return FReply::Handled();
				}));
			AddBtn(TEXT("Rotate CCW (pending move)"),
				FOnClicked::CreateLambda([this]() {
					RunCli(TEXT("move_rotate ccw"));
					if (Subsystem.IsValid()) {
						LastCliOutput = FString::Printf(TEXT("Pending footprint rotation (quarters CW): %+d"),
							Subsystem->GetPendingMoveQuarterTurnsCw());
						RefreshStatusText();
					}
					return FReply::Handled();
				}));
		}
		if (bSelectedControlled && !Subsystem->IsSelectedUnitJammed()) {
			const int32 NAbl = Subsystem->GetSelectedUnitActivatedAbilityCount();
			for (int32 Ai = 1; Ai <= NAbl; ++Ai) {
				FString AKey, ALab, ASpeed, ACost;
				bool bUsed = false;
				bool bNeedCell = false;
				FString ARangeToken;
				int32 AUsesRemaining = 0;
				int32 AUsesMax = 0;
				if (!Subsystem->TryGetSelectedUnitActivatedAbilityUi(Ai, AKey, ALab, ASpeed, ACost, bUsed, bNeedCell, ARangeToken,
						AUsesRemaining, AUsesMax)) {
					continue;
				}
				if (bUsed) {
					continue;
				}
				const FString BtnLabel = FString::Printf(TEXT("Ab:%s"), *ALab.Left(14));
				AddBtn(*BtnLabel, FOnClicked::CreateLambda([this, AKey, bNeedCell]() {
					ArmSelectedAbilityForTargeting(AKey, bNeedCell);
					return FReply::Handled();
				}));
			}
		}
	} else if (bHasCell) {
		AddBtn(TEXT("Select unit"),
			FOnClicked::CreateLambda([this]() { return DispatchCliFromPendingCellVerb(TEXT("select")); }));
	}

	AddCliShortcuts();
}

bool STacticsBoardPanel::IsArmedFocusSpell() const
{
	if (!Subsystem.IsValid()) {
		return false;
	}
	bool bRequiresFocus = false;
	bool bRequiresForced = false;
	if (bReservesArmedForTile) {
		const bool bFocus = Subsystem->TryGetReservesSpellRequiresFocusCaster(DeployReservesIndex1Based, bRequiresFocus)
			&& bRequiresFocus;
		const bool bForced = Subsystem->TryGetReservesSpellRequiresForcedFocusCaster(DeployReservesIndex1Based, bRequiresForced)
			&& bRequiresForced;
		return bFocus || bForced;
	}
	if (bHandArmedForTile) {
		const bool bFocus = Subsystem->TryGetHandSpellRequiresFocusCaster(DeployHandIndex1Based, bRequiresFocus) && bRequiresFocus;
		const bool bForced = Subsystem->TryGetHandSpellRequiresForcedFocusCaster(DeployHandIndex1Based, bRequiresForced)
			&& bRequiresForced;
		return bFocus || bForced;
	}
	return false;
}

bool STacticsBoardPanel::IsArmedPushDirectionSpell() const
{
	if (!Subsystem.IsValid()) {
		return false;
	}
	bool bUsesPushDirection = false;
	if (bReservesArmedForTile) {
		return Subsystem->TryGetReservesSpellUsesPushDirectionAim(DeployReservesIndex1Based, bUsesPushDirection) && bUsesPushDirection;
	}
	if (bHandArmedForTile) {
		return Subsystem->TryGetHandSpellUsesPushDirectionAim(DeployHandIndex1Based, bUsesPushDirection) && bUsesPushDirection;
	}
	return false;
}

bool STacticsBoardPanel::IsArmedDirectionalFocusSpell() const
{
	if (!IsArmedFocusSpell() || !Subsystem.IsValid()) {
		return false;
	}
	bool bUsesDirectionalAim = false;
	if (bReservesArmedForTile) {
		const int32 N = Subsystem->GetControlledReservesCount();
		if (N < 1) {
			return false;
		}
		const int32 Idx = FMath::Clamp(DeployReservesIndex1Based, 1, N);
		return Subsystem->TryGetReservesSpellUsesDirectionalAim(Idx, bUsesDirectionalAim) && bUsesDirectionalAim;
	}
	if (bHandArmedForTile) {
		return Subsystem->TryGetHandSpellUsesDirectionalAim(DeployHandIndex1Based, bUsesDirectionalAim) && bUsesDirectionalAim;
	}
	return false;
}

bool STacticsBoardPanel::IsArmedNoTargetSpell() const
{
	if (bModalSpellPickerActive) {
		return false;
	}
	if (!Subsystem.IsValid()) {
		return false;
	}
	if (bReservesArmedForTile) {
		const int32 N = Subsystem->GetControlledReservesCount();
		if (N < 1) {
			return false;
		}
		const int32 Idx = FMath::Clamp(DeployReservesIndex1Based, 1, N);
		FString Name, Kind, Cost, Rules;
		if (!Subsystem->TryGetReservesCardUi(Idx, Name, Kind, Cost, Rules)) {
			return false;
		}
		if (!Kind.Equals(TEXT("spell"), ESearchCase::IgnoreCase)) {
			return false;
		}
		bool bRequiresCell = true;
		if (ArmedSpellModeIndex >= 0) {
			if (!Subsystem->TryGetReservesSpellModeRequiresBoardCell(Idx, ArmedSpellModeIndex, bRequiresCell)) {
				return false;
			}
		} else if (!Subsystem->TryGetReservesSpellRequiresBoardCell(Idx, bRequiresCell)) {
			return false;
		}
		if (bRequiresCell) {
			return false;
		}
		bool bRequiresStack = false;
		if (Subsystem->TryGetReservesSpellRequiresStackTarget(Idx, bRequiresStack) && bRequiresStack) {
			return false;
		}
		bool bRequiresPlayer = false;
		if (Subsystem->TryGetReservesSpellRequiresPlayerSeatTarget(Idx, bRequiresPlayer) && bRequiresPlayer) {
			return false;
		}
		return true;
	}
	if (!bHandArmedForTile) {
		return false;
	}
	const int32 N = Subsystem->GetControlledHandCount();
	if (N < 1) {
		return false;
	}
	const int32 Idx = FMath::Clamp(DeployHandIndex1Based, 1, N);
	FString Name, Kind, Cost, Rules;
	if (!Subsystem->TryGetHandCardUi(Idx, Name, Kind, Cost, Rules)) {
		return false;
	}
	if (!Kind.Equals(TEXT("spell"), ESearchCase::IgnoreCase)) {
		return false;
	}
	bool bRequiresCell = true;
	if (ArmedSpellModeIndex >= 0) {
		if (!Subsystem->TryGetHandSpellModeRequiresBoardCell(Idx, ArmedSpellModeIndex, bRequiresCell)) {
			return false;
		}
	} else if (!Subsystem->TryGetHandSpellRequiresBoardCell(Idx, bRequiresCell)) {
		return false;
	}
	if (bRequiresCell) {
		return false;
	}
	bool bRequiresStack = false;
	if (Subsystem->TryGetHandSpellRequiresStackTarget(Idx, bRequiresStack) && bRequiresStack) {
		return false;
	}
	bool bRequiresPlayer = false;
	if (Subsystem->TryGetHandSpellRequiresPlayerSeatTarget(Idx, bRequiresPlayer) && bRequiresPlayer) {
		return false;
	}
	return true;
}

bool STacticsBoardPanel::IsArmedPlayerTargetSpell() const
{
	if (!Subsystem.IsValid()) {
		return false;
	}
	if (bReservesArmedForTile) {
		const int32 N = Subsystem->GetControlledReservesCount();
		if (N < 1) {
			return false;
		}
		const int32 Idx = FMath::Clamp(DeployReservesIndex1Based, 1, N);
		bool bRequiresPlayer = false;
		return Subsystem->TryGetReservesSpellRequiresPlayerSeatTarget(Idx, bRequiresPlayer) && bRequiresPlayer;
	}
	if (!bHandArmedForTile) {
		return false;
	}
	const int32 N = Subsystem->GetControlledHandCount();
	if (N < 1) {
		return false;
	}
	const int32 Idx = FMath::Clamp(DeployHandIndex1Based, 1, N);
	bool bRequiresPlayer = false;
	return Subsystem->TryGetHandSpellRequiresPlayerSeatTarget(Idx, bRequiresPlayer) && bRequiresPlayer;
}

bool STacticsBoardPanel::IsArmedXCostSpell() const
{
	return (bHandArmedForTile || bReservesArmedForTile) && bArmedSpellHasXCost;
}

bool STacticsBoardPanel::IsArmedXCostAbility() const
{
	return bAbilityArmedForTile && bArmedAbilityRequiresXCost;
}

void STacticsBoardPanel::InitArmedXCostForHandSpell(int32 Index1Based)
{
	ArmedXCostAmount = 0;
	ArmedXCostMin = 0;
	ArmedXCostMax = 0;
	ArmedXCostEnergyType.Empty();
	bArmedSpellHasXCost = false;
	if (!Subsystem.IsValid()) {
		return;
	}
	bool bHasXCost = false;
	FString EnergyType;
	int32 MinX = 0;
	int32 MaxX = 0;
	if (!Subsystem->TryGetHandSpellXCostInfo(Index1Based, bHasXCost, EnergyType, MinX, MaxX) || !bHasXCost) {
		return;
	}
	bArmedSpellHasXCost = true;
	ArmedXCostEnergyType = EnergyType;
	ArmedXCostMin = MinX;
	ArmedXCostMax = FMath::Max(MinX, MaxX);
	ArmedXCostAmount = ArmedXCostMax >= MinX ? MinX : 0;
}

void STacticsBoardPanel::InitArmedXCostForReservesSpell(int32 Index1Based)
{
	ArmedXCostAmount = 0;
	ArmedXCostMin = 0;
	ArmedXCostMax = 0;
	ArmedXCostEnergyType.Empty();
	bArmedSpellHasXCost = false;
	if (!Subsystem.IsValid()) {
		return;
	}
	bool bHasXCost = false;
	FString EnergyType;
	int32 MinX = 0;
	int32 MaxX = 0;
	if (!Subsystem->TryGetReservesSpellXCostInfo(Index1Based, bHasXCost, EnergyType, MinX, MaxX) || !bHasXCost) {
		return;
	}
	bArmedSpellHasXCost = true;
	ArmedXCostEnergyType = EnergyType;
	ArmedXCostMin = MinX;
	ArmedXCostMax = FMath::Max(MinX, MaxX);
	ArmedXCostAmount = ArmedXCostMax >= MinX ? MinX : 0;
}

void STacticsBoardPanel::InitArmedXCostForAbility(const FString& AbilityKey)
{
	ArmedXCostAmount = 0;
	ArmedXCostMin = 0;
	ArmedXCostMax = 0;
	ArmedXCostEnergyType.Empty();
	bArmedAbilityRequiresXCost = false;
	if (!Subsystem.IsValid()) {
		return;
	}
	bool bHasXCost = false;
	FString EnergyType;
	int32 MinX = 0;
	int32 MaxX = 0;
	if (!Subsystem->TryGetSelectedAbilityXCostInfo(AbilityKey, bHasXCost, EnergyType, MinX, MaxX) || !bHasXCost) {
		return;
	}
	ArmedXCostEnergyType = EnergyType;
	ArmedXCostMin = MinX;
	ArmedXCostMax = FMath::Max(MinX, MaxX);
	ArmedXCostAmount = ArmedXCostMax >= MinX ? MinX : 0;
	bArmedAbilityRequiresXCost = true;
}

FString STacticsBoardPanel::AppendArmedXCostCliSuffix(const FString& BaseLine) const
{
	if (ArmedXCostAmount <= 0) {
		return BaseLine;
	}
	return BaseLine + FString::Printf(TEXT(" %d"), ArmedXCostAmount);
}

void STacticsBoardPanel::RunArmedCastCli(const FString& BaseCastLine)
{
	FString Line = InsertArmedSpellModeIntoCastLine(BaseCastLine);
	Line = AppendArmedXCostCliSuffix(Line);
	ClearPlayArmingState();
	RunCli(Line);
}

FString STacticsBoardPanel::BuildArmedAbilityStackCliLine(const FString& StackId) const
{
	FString Line = FString::Printf(TEXT("ability %s stack %s"), *ArmedAbilityKey, *StackId);
	if (IsArmedXCostAbility() && Subsystem.IsValid()) {
		int32 StackCost = 0;
		if (Subsystem->TryGetStackItemBatchedSpellTotalCost(StackId, StackCost) && StackCost > 0) {
			Line += FString::Printf(TEXT(" %d"), StackCost);
		}
	}
	return Line;
}

bool STacticsBoardPanel::IsArmedStackTargetSpell() const
{
	if (!Subsystem.IsValid()) {
		return false;
	}
	if (bReservesArmedForTile) {
		const int32 N = Subsystem->GetControlledReservesCount();
		if (N < 1) {
			return false;
		}
		const int32 Idx = FMath::Clamp(DeployReservesIndex1Based, 1, N);
		bool bRequiresStack = false;
		return Subsystem->TryGetReservesSpellRequiresStackTarget(Idx, bRequiresStack) && bRequiresStack;
	}
	if (!bHandArmedForTile) {
		return false;
	}
	const int32 N = Subsystem->GetControlledHandCount();
	if (N < 1) {
		return false;
	}
	const int32 Idx = FMath::Clamp(DeployHandIndex1Based, 1, N);
	bool bRequiresStack = false;
	return Subsystem->TryGetHandSpellRequiresStackTarget(Idx, bRequiresStack) && bRequiresStack;
}

bool STacticsBoardPanel::IsArmedStackTargetEffect() const
{
	if (IsArmedStackTargetSpell()) {
		return true;
	}
	if (!Subsystem.IsValid() || !bAbilityArmedForTile || ArmedAbilityKey.IsEmpty()) {
		return false;
	}
	bool bRequiresStack = false;
	return Subsystem->TryGetSelectedAbilityRequiresStackTarget(ArmedAbilityKey, bRequiresStack) && bRequiresStack;
}

bool STacticsBoardPanel::CanArmedStackTargetSourceType(const FString& SourceType) const
{
	if (!Subsystem.IsValid()) {
		return false;
	}
	if (IsArmedStackTargetSpell()) {
		if (bReservesArmedForTile) {
			const int32 N = Subsystem->GetControlledReservesCount();
			const int32 Idx = FMath::Clamp(DeployReservesIndex1Based, 1, FMath::Max(1, N));
			return Subsystem->CanReservesSpellTargetStackSourceType(Idx, SourceType);
		}
		const int32 N = Subsystem->GetControlledHandCount();
		const int32 Idx = FMath::Clamp(DeployHandIndex1Based, 1, FMath::Max(1, N));
		return Subsystem->CanHandSpellTargetStackSourceType(Idx, SourceType);
	}
	if (bAbilityArmedForTile && !ArmedAbilityKey.IsEmpty()) {
		return Subsystem->CanSelectedAbilityTargetStackSourceType(ArmedAbilityKey, SourceType);
	}
	return false;
}

void STacticsBoardPanel::RebuildBottomRightBar()
{
	if (!BottomRightBar.IsValid()) {
		return;
	}
	BottomRightBar->ClearChildren();

	auto AddBottomBtn = [this](const TCHAR* Label, FOnClicked OnClick) {
		BottomRightBar->AddSlot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[SNew(SHorizontalBox) + SHorizontalBox::Slot().FillWidth(1.f) +
																									 SHorizontalBox::Slot().AutoWidth()
																										 [MakeCmdButton(FText::FromString(Label), OnClick)]];
	};

	if (bShowDevTools) {
		AddBottomBtn(TEXT("Reset demo"),
			FOnClicked::CreateLambda([this]() {
				RunResetDemo();
				return FReply::Handled();
			}));
		AddBottomBtn(TEXT("Reset sandbox"),
			FOnClicked::CreateLambda([this]() {
				RunResetSandbox();
				return FReply::Handled();
			}));
	}

	if (!Subsystem.IsValid() || !Subsystem->IsMatchReady()) {
		return;
	}

	if (!Subsystem->IsAwaitingHandDiscard() && !Subsystem->IsAwaitingScan() && !Subsystem->IsAwaitingTerritoryLoot()
		&& Subsystem->GetPendingEnergyZoneChoiceCount() < 1) {
		if (Subsystem->IsDefenseReactionPhase() && Subsystem->HasPendingAttacksInQueue()) {
			AddBottomBtn(TEXT("Resolve attacks"),
				FOnClicked::CreateLambda([this]() {
					ResolvePendingAttacksWithVisualization();
					return FReply::Handled();
				}));
		}
		if (Subsystem->IsAnyReactionWindowPhase() && Subsystem->CanControlledPlayerPassPriority()) {
			AddBottomBtn(TEXT("Pass"),
				FOnClicked::CreateLambda([this]() {
					RunCli(TEXT("pass"));
					return FReply::Handled();
				}));
		}
		if (Subsystem->IsAnyMainPhase() && Subsystem->CanControlledPlayerEndTurn()) {
			AddBottomBtn(TEXT("End main phase"),
				FOnClicked::CreateLambda([this]() {
					RunCli(TEXT("end_main"));
					return FReply::Handled();
				}));
		}
		if (Subsystem->IsAnyAttackDeclarationPhase() && Subsystem->CanControlledPlayerCommitAttacks()) {
			AddBottomBtn(TEXT("Commit attacks"),
				FOnClicked::CreateLambda([this]() {
					RunCli(TEXT("attack_commit"));
					return FReply::Handled();
				}));
		}
	}
	if (Subsystem->CanControlledPlayerUndo()) {
		AddBottomBtn(TEXT("Undo"),
			FOnClicked::CreateLambda([this]() {
				RunCli(TEXT("undo"));
				return FReply::Handled();
			}));
	}

	// Conquering Territories: a placed territory's targeted enter/groundwork effect can be skipped.
	if (Subsystem->IsControlledPlayerAwaitingTerritoryTarget()) {
		AddBottomBtn(TEXT("Skip (territory effect)"),
			FOnClicked::CreateLambda([this]() {
				RunCli(TEXT("land_target_skip"));
				return FReply::Handled();
			}));
		return;
	}

	// Conquering Territories: a no-target "use land" ability is armed - confirm or cancel it, so the
	// player has a moment to read the territory before it fires.
	if (bLandAbilityArmedNoTarget) {
		const int32 T = ArmedLandTerritoryIndex;
		const int32 A = ArmedLandAbilityIndex;
		AddBottomBtn(TEXT("Confirm use land"),
			FOnClicked::CreateLambda([this, T, A]() {
				ClearPlayArmingState();
				RunCli(FString::Printf(TEXT("use_land %d %d"), T + 1, A + 1));
				return FReply::Handled();
			}));
		AddBottomBtn(TEXT("Cancel"),
			FOnClicked::CreateLambda([this]() {
				ClearPlayArmingState();
				RefreshStatusText();
				Refresh();
				return FReply::Handled();
			}));
		return;
	}

	if (Subsystem->IsAwaitingHandDiscard()) {
		if (bDiscardHandCardSelected) {
			AddBottomBtn(TEXT("Discard"),
				FOnClicked::CreateLambda([this]() {
					if (Subsystem.IsValid()) {
						const int32 N = Subsystem->GetControlledHandCount();
						const int32 Idx = FMath::Clamp(DeployHandIndex1Based, 1, FMath::Max(1, N));
						bDiscardHandCardSelected = false;
						RunCli(FString::Printf(TEXT("discard %d"), Idx));
					}
					return FReply::Handled();
				}));
		}
		return;
	}
	if (Subsystem->IsAwaitingTerritoryLoot()) {
		if (bDiscardHandCardSelected) {
			AddBottomBtn(TEXT("Discard to draw"),
				FOnClicked::CreateLambda([this]() {
					if (Subsystem.IsValid()) {
						const int32 N = Subsystem->GetControlledHandCount();
						const int32 Idx = FMath::Clamp(DeployHandIndex1Based, 1, FMath::Max(1, N));
						bDiscardHandCardSelected = false;
						RunCli(FString::Printf(TEXT("territory_loot_discard %d"), Idx));
					}
					return FReply::Handled();
				}));
		}
		AddBottomBtn(TEXT("Skip (keep hand)"),
			FOnClicked::CreateLambda([this]() {
				RunCli(TEXT("territory_loot_skip"));
				return FReply::Handled();
			}));
		return;
	}
	if (Subsystem->IsAwaitingScan()) {
		// A scanned card must be selected (armed) first so its description shows on the left; Discard
		// then removes it. Keep-all finishes the scan without discarding anything.
		if (ArmedScanCardIndex >= 1) {
			const int32 Pick = ArmedScanCardIndex;
			AddBottomBtn(TEXT("Discard selected"),
				FOnClicked::CreateLambda([this, Pick]() {
					ArmedScanCardIndex = -1;
					RunCli(FString::Printf(TEXT("scan_discard %d"), Pick));
					return FReply::Handled();
				}));
			AddBottomBtn(TEXT("Cancel"),
				FOnClicked::CreateLambda([this]() {
					ArmedScanCardIndex = -1;
					RefreshStatusText();
					Refresh();
					return FReply::Handled();
				}));
		}
		AddBottomBtn(TEXT("Keep all (finish scan)"),
			FOnClicked::CreateLambda([this]() {
				ArmedScanCardIndex = -1;
				RunCli(TEXT("scan_finish"));
				return FReply::Handled();
			}));
		return;
	}
	if (Subsystem->GetPendingEnergyZoneChoiceCount() > 0) {
		// A territory choice must be selected (armed) first so the player can read it; Confirm places it.
		if (ArmedZoneChoiceIndex >= 1) {
			const int32 Pick = ArmedZoneChoiceIndex;
			AddBottomBtn(TEXT("Confirm Territory"),
				FOnClicked::CreateLambda([this, Pick]() {
					ArmedZoneChoiceIndex = -1;
					RunCli(FString::Printf(TEXT("zonepick %d"), Pick));
					return FReply::Handled();
				}));
			AddBottomBtn(TEXT("Cancel"),
				FOnClicked::CreateLambda([this]() {
					ArmedZoneChoiceIndex = -1;
					RefreshStatusText();
					Refresh();
					return FReply::Handled();
				}));
		}
		AddBottomBtn(TEXT("Skip Territory"),
			FOnClicked::CreateLambda([this]() {
				ArmedZoneChoiceIndex = -1;
				RunCli(TEXT("zoneskip"));
				return FReply::Handled();
			}));
		return;
	}
	// Show confirm-move / confirm-cast only during phases where the player can take active actions.
	if (!Subsystem->IsAnyMainPhase() && !Subsystem->IsAnyAttackDeclarationPhase()) {
		return;
	}

	if (bHandArmedForTile || bReservesArmedForTile || bAbilityArmedForTile) {
		AddBottomBtn(TEXT("Cancel play"),
			FOnClicked::CreateLambda([this]() {
				ClearPlayArmingState();
				LastCliOutput = TEXT("Card / ability play cancelled.");
				RefreshStatusText();
				Refresh();
				return FReply::Handled();
			}));
	}

	if ((IsArmedXCostSpell() || IsArmedXCostAbility()) && Subsystem->CanControlledPlayerActInMainPhase() && ArmedXCostAmount > 0) {
		const FString EnergyLabel = ArmedXCostEnergyType.IsEmpty() ? TEXT("energy") : ArmedXCostEnergyType;
		AddBottomBtn(TEXT("X −"),
			FOnClicked::CreateLambda([this]() {
				if (ArmedXCostAmount > ArmedXCostMin) {
					--ArmedXCostAmount;
					LastCliOutput = FString::Printf(TEXT("X set to %d."), ArmedXCostAmount);
					RefreshStatusText();
					Refresh();
				}
				return FReply::Handled();
			}));
		AddBottomBtn(*FString::Printf(TEXT("X = %d (%s)"), ArmedXCostAmount, *EnergyLabel),
			FOnClicked::CreateLambda([this]() { return FReply::Handled(); }));
		AddBottomBtn(TEXT("X +"),
			FOnClicked::CreateLambda([this]() {
				if (ArmedXCostAmount < ArmedXCostMax) {
					++ArmedXCostAmount;
					LastCliOutput = FString::Printf(TEXT("X set to %d."), ArmedXCostAmount);
					RefreshStatusText();
					Refresh();
				}
				return FReply::Handled();
			}));
	}

	if (ArmedMulticastTargetTotal > 1 && ArmedMulticastTargetsPicked >= 1
		&& ArmedMulticastTargetsPicked < ArmedMulticastTargetTotal
		&& Subsystem->CanControlledPlayerActInMainPhase()) {
		const bool bReserves = bReservesArmedForTile;
		AddBottomBtn(TEXT("Finish multicast"),
			FOnClicked::CreateLambda([this, bReserves]() {
				RunMulticastCastCli(bReserves);
				return FReply::Handled();
			}));
	}

	if (Subsystem->HasPendingMoveForControlledPlayer()) {
		AddBottomBtn(TEXT("Confirm move"),
			FOnClicked::CreateLambda([this]() {
				RunCli(TEXT("move_confirm"));
				return FReply::Handled();
			}));
		AddBottomBtn(TEXT("Cancel move"),
			FOnClicked::CreateLambda([this]() {
				RunCli(TEXT("move_cancel"));
				return FReply::Handled();
			}));
	}

	if (IsArmedNoTargetSpell() && Subsystem->CanControlledPlayerActInMainPhase()) {
		if (bReservesArmedForTile) {
			const int32 Idx = DeployReservesIndex1Based;
			AddBottomBtn(TEXT("Confirm cast"),
				FOnClicked::CreateLambda([this, Idx]() {
					RunArmedCastCli(FString::Printf(TEXT("cast_reserve %d"), Idx));
					return FReply::Handled();
				}));
		} else {
			const int32 Idx = DeployHandIndex1Based;
			AddBottomBtn(TEXT("Confirm cast"),
				FOnClicked::CreateLambda([this, Idx]() {
					RunArmedCastCli(FString::Printf(TEXT("cast %d"), Idx));
					return FReply::Handled();
				}));
		}
	}

	if (IsArmedPlayerTargetSpell() && Subsystem->CanControlledPlayerActInMainPhase()) {
		const int32 PlayerCount = Subsystem->GetMatchPlayerCount();
		if (bReservesArmedForTile) {
			const int32 Idx = DeployReservesIndex1Based;
			for (int32 Seat = 1; Seat <= PlayerCount; ++Seat) {
				const int32 TargetSeat = Seat;
				AddBottomBtn(*FString::Printf(TEXT("Target P%d"), TargetSeat),
					FOnClicked::CreateLambda([this, Idx, TargetSeat]() {
						RunArmedCastCli(FString::Printf(TEXT("cast_reserve %d player %d"), Idx, TargetSeat));
						return FReply::Handled();
					}));
			}
		} else {
			const int32 Idx = DeployHandIndex1Based;
			for (int32 Seat = 1; Seat <= PlayerCount; ++Seat) {
				const int32 TargetSeat = Seat;
				AddBottomBtn(*FString::Printf(TEXT("Target P%d"), TargetSeat),
					FOnClicked::CreateLambda([this, Idx, TargetSeat]() {
						RunArmedCastCli(FString::Printf(TEXT("cast %d player %d"), Idx, TargetSeat));
						return FReply::Handled();
					}));
			}
		}
	}

	if (Subsystem->HasUnitSelected() && Subsystem->IsSelectedUnitControlled() && Subsystem->CanControlledUnitDefend()) {
		AddBottomBtn(TEXT("Defend"),
			FOnClicked::CreateLambda([this]() {
				FString Msg;
				if (Subsystem.IsValid()) {
					Subsystem->TryDefendSelectedUnit(Msg);
						LastCliOutput = Msg;
						RefreshStatusText();
				}
				return FReply::Handled();
			}));
	}
	if (Subsystem->HasUnitSelected() && Subsystem->IsSelectedUnitControlled() && Subsystem->CanControlledUnitDash()) {
		AddBottomBtn(TEXT("Dash"),
			FOnClicked::CreateLambda([this]() {
				FString Msg;
				if (Subsystem.IsValid()) {
					Subsystem->TryDashSelectedUnit(Msg);
						LastCliOutput = Msg;
						RefreshStatusText();
				}
				return FReply::Handled();
			}));
	}
	if (Subsystem->HasUnitSelected() && Subsystem->IsSelectedUnitControlled() && Subsystem->CanControlledUnitRecover()) {
		AddBottomBtn(TEXT("Recover"),
			FOnClicked::CreateLambda([this]() {
				FString Msg;
				if (Subsystem.IsValid()) {
					Subsystem->TryRecoverSelectedUnit(Msg);
						LastCliOutput = Msg;
						RefreshStatusText();
				}
				return FReply::Handled();
			}));
	}
}

void STacticsBoardPanel::ShowFailureAlert(const FString& Reason)
{
	FString Clean = Reason;
	Clean.TrimStartAndEndInline();
	Clean.RemoveFromStart(TEXT("Failed:"), ESearchCase::IgnoreCase);
	Clean.TrimStartInline();
	// Keep it to the first line - CLI replies may append a board dump after the message.
	int32 NewlineIdx = INDEX_NONE;
	if (Clean.FindChar(TEXT('\n'), NewlineIdx)) {
		Clean = Clean.Left(NewlineIdx).TrimEnd();
	}
	if (Clean.IsEmpty()) {
		return;
	}
	FailureAlertText = Clean;
	// Restart the fade timeline: hold, then fade out (opacity is computed from GetLerp in the widget).
	FailureAlertCurve = FCurveSequence();
	FailureAlertCurve.AddCurve(0.f, 3.2f, ECurveEaseFunction::Linear);
	FailureAlertCurve.Play(SharedThis(this));
}

void STacticsBoardPanel::ExecuteLocalCliLine(const FString& Line)
{
	if (!Subsystem.IsValid()) {
		return;
	}
	// Unstick a combat-viz pause that never started on-board presentation (would block pass/CLI).
	if (Subsystem->IsCombatVisualizationPaused() && !bBattleVizAwaitingResume && !bBattleVizShowingResult
		&& !Subsystem->HasActiveAbilityResolvePresentation()) {
		PresentPausedCombatOrAbilityVisualization();
	}
	if (bCombatScreenActive) {
		LastCliOutput = TEXT("Combat visualization in progress - click Continue on the combat screen.");
		RefreshStatusText();
		return;
	}

	FString Out;
	Subsystem->ExecMasterCliLine(Line, Out);
	LastCliOutput = Out;
	AppendCombatLogLine(Out);

	// A rejected card/action/cast surfaces as a "Failed: <reason>" CLI reply - toast the reason.
	if (Out.Contains(TEXT("Failed:"), ESearchCase::IgnoreCase)) {
		ShowFailureAlert(Out);
	}

	const FString TrimLine = Line;
	FString TrimAbilityLine = TrimLine;
	TrimAbilityLine.TrimStartAndEndInline();
	if (TrimAbilityLine.StartsWith(TEXT("ability "), ESearchCase::CaseSensitive)) {
		const bool bAbilityOk = !Out.Contains(TEXT("Failed:"), ESearchCase::IgnoreCase);
		if (Subsystem.IsValid()) {
			Subsystem->NotifyAbilityCastFlash(bAbilityOk);
		}
	}

	if (Subsystem->IsCombatVisualizationPaused()) {
		PresentPausedCombatOrAbilityVisualization();
		return;
	}

	// After CLI resolves, drain any passive attack viz events (mortar shots etc.)
	if (Subsystem->HasPendingPassiveAttackVizEvents()) {
		TryPresentNextPassiveAttackViz();
		return;
	}

	RefreshStatusText();
	Refresh();
}

void STacticsBoardPanel::RunCli(const FString& Line)
{
	if (!Subsystem.IsValid()) {
		return;
	}
	FString TrimmedLine = Line;
	TrimmedLine.TrimStartAndEndInline();
	const bool bClearsMoveTarget =
		TrimmedLine.Equals(TEXT("move_cancel"), ESearchCase::IgnoreCase) || TrimmedLine.Equals(TEXT("move_confirm"), ESearchCase::IgnoreCase);
	if (bClearsMoveTarget) {
		Subsystem->ClearPendingCliWorldCell();
	}
	UTacticsWebSocketSubsystem* Net = nullptr;
	if (ResolveWebSocketNet(Net)) {
		if (Net->GetRole() == ETacticsWsRole::ClientRemote) {
			FString Trim = Line;
			Trim.TrimStartAndEndInline();
			if (Trim.StartsWith(TEXT("as "), ESearchCase::IgnoreCase)) {
				LastCliOutput = FString::Printf(
					TEXT("Networking: fixed seat P%d - reconnect with ?seat= in the URL to change."),
					Net->GetClientRemoteSeatPlayerId());
				RefreshStatusText();
				Refresh();
				return;
			}
			// Selection is applied when the host broadcasts the authoritative `cmd` (no optimistic local select).
			Net->ClientSendCliLine(Line);
			LastCliOutput = TEXT("(client) Sent to host - host reply appears below.");
			RefreshStatusText();
			Refresh();
			return;
		}
		ExecuteLocalCliLine(Line);
		return;
	}
	ExecuteLocalCliLine(Line);
}

void STacticsBoardPanel::ResolvePendingAttacksWithVisualization()
{
	if (!Subsystem.IsValid() || !Subsystem->IsDefenseReactionPhase() || !Subsystem->HasPendingAttacksInQueue()) {
		return;
	}
	FString AutoPassLog;
	Subsystem->AutoPassDefenseWindowUntilClosed(AutoPassLog);
	LastCliOutput = AutoPassLog;
	if (Subsystem->IsCombatVisualizationPaused()) {
		PresentPausedCombatOrAbilityVisualization();
		return;
	}
	RefreshStatusText();
	Refresh();
}

void STacticsBoardPanel::OnHostCliAckFromNet(const FString& Message)
{
	LastCliOutput = Message;
	AppendCombatLogLine(Message);
	RefreshStatusText();
	Refresh();
}

bool STacticsBoardPanel::IsWebSocketClientP2() const
{
	UTacticsWebSocketSubsystem* Net = nullptr;
	return ResolveWebSocketNet(Net) && Net->GetRole() == ETacticsWsRole::ClientRemote;
}

bool STacticsBoardPanel::EnsureCliCell(FString& OutError) const
{
	if (!Subsystem.IsValid()) {
		OutError = TEXT("No subsystem.");
		return false;
	}
	int Px = 0, Py = 0;
	if (!Subsystem->TryGetPendingCliWorldCell(Px, Py)) {
		OutError = TEXT("Click a board cell first (sets 1-based CLI column/row).");
		return false;
	}
	FString C, R;
	if (!Subsystem->GetCliCellTokens(Px, Py, C, R)) {
		OutError = TEXT("Cell is outside merged CLI bounds.");
		return false;
	}
	return true;
}

void STacticsBoardPanel::RefreshStatusText()
{
	if (!StatusText.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	FString Target = TEXT("Target cell: (none - click grid)");
	FString C, R;
	int Px = 0, Py = 0;
	if (Subsystem->TryGetPendingCliWorldCell(Px, Py) && Subsystem->GetCliCellTokens(Px, Py, C, R)) {
		Target = FString::Printf(TEXT("Target cell: %s %s (1-based column, row)"), *C, *R);
	}
	FString ArmLine;
	if (Subsystem->IsControlledPlayerAwaitingTerritoryTarget()) {
		ArmLine = Subsystem->GetTerritoryTargetPrompt();
	} else if (bLandAbilityArmedNoTarget) {
		ArmLine = FString::Printf(TEXT("Use Land armed: %s - press Confirm use land (bottom-right), or Cancel."),
			*ArmedLandDescription);
	} else if (bLandAbilityArmedForTile) {
		ArmLine = FString::Printf(TEXT("Use Land armed: %s - click a target unit, or Cancel."), *ArmedLandDescription);
	} else if (Subsystem->IsAwaitingHandDiscard()) {
		ArmLine = TEXT(
			"Discard phase (end of turn): click a card in the strip to select it, then press Discard in the bottom-right corner (repeat until hand size is at most 8).");
	} else if (Subsystem->IsAwaitingScan() && ArmedScanCardIndex >= 1) {
		ArmLine = TEXT(
			"Scan: card selected - read it on the left, then press Discard selected (bottom-right), tap it again to discard, or Cancel to keep it.");
	} else if (Subsystem->IsAwaitingScan()) {
		ArmLine = TEXT("Scan: tap a card in the strip below to select and read it, then decide whether to discard it (or Keep all).");
	} else if (Subsystem->GetPendingEnergyZoneChoiceCount() > 0 && ArmedZoneChoiceIndex >= 1) {
		ArmLine = TEXT(
			"Conquering Territories: territory selected - read it, then press Confirm Territory (bottom-right), tap it again to confirm, or Cancel.");
	} else if (Subsystem->GetPendingEnergyZoneChoiceCount() > 0) {
		ArmLine = TEXT("Conquering Territories: tap a territory card in the strip below to select it (you then Confirm to claim it).");
	} else if (bModalSpellPickerActive) {
		ArmLine = TEXT("Choose a mode - click one of the card copies in the center overlay.");
	} else if (IsArmedStackTargetEffect()) {
		ArmLine = TEXT("Stack target armed - click a visible stack card to target, or Cancel play.");
	} else if (IsArmedPlayerTargetSpell() && Subsystem->CanControlledPlayerActInMainPhase()) {
		ArmLine = TEXT("Player-target spell armed - choose Target P# (bottom right), or Cancel play.");
	} else if (IsArmedPlayerTargetSpell()) {
		ArmLine = TEXT("Player-target spell armed - wait until Active matches Control, then choose Target P#, or Cancel play.");
	} else if (bAbilityArmedForTile && Subsystem.IsValid()) {
		const ETacticsAbilityVisualGroup Group = Subsystem->ResolveAbilityVisualGroup(ArmedAbilityKey);
		ArmLine = FString::Printf(TEXT("Ability armed - %s"), *TacticsAbilityVisual::GroupPrompt(Group));
	} else if (bReservesArmedForTile && IsArmedNoTargetSpell() && Subsystem->CanControlledPlayerActInMainPhase()) {
		ArmLine = TEXT("Reserves: spell armed - use Confirm cast (bottom right), or Cancel play.");
	} else if (bReservesArmedForTile && IsArmedNoTargetSpell()) {
		ArmLine = TEXT("Reserves: spell armed - wait until Active matches Control, then Confirm cast, or Cancel play.");
	} else if (bReservesArmedForTile) {
		FString CardKind;
		FString Name, Cost, Rules;
		if (Subsystem->TryGetReservesCardUi(DeployReservesIndex1Based, Name, CardKind, Cost, Rules)
			&& (CardKind.Equals(TEXT("unit"), ESearchCase::IgnoreCase)
				|| CardKind.Equals(TEXT("building"), ESearchCase::IgnoreCase))) {
			ArmLine = TEXT("Reserves: unit/building armed - click a highlighted tile to deploy.");
		} else if (IsArmedFocusSpell()) {
			ArmLine = bFocusSpellAwaitingCaster
				? TEXT("Reserves: Focus spell armed - click a highlighted unit to cast from.")
				: TEXT("Reserves: Focus spell armed - click a highlighted enemy in range and line of sight.");
		} else {
			ArmLine = TEXT("Reserves: armed - click a highlighted tile to play (your turn only).");
		}
	} else if (bHandArmedForTile && IsArmedNoTargetSpell() && Subsystem->CanControlledPlayerActInMainPhase()) {
		ArmLine = TEXT("Hand: spell armed - use Confirm cast (bottom right), or Cancel play.");
	} else if (bHandArmedForTile && IsArmedNoTargetSpell()) {
		ArmLine = TEXT("Hand: spell armed - wait until Active matches Control, then use Confirm cast (bottom right), or Cancel play.");
	} else if (bHandArmedForTile) {
		FString CardKind;
		FString Name, Cost, Rules;
		if (Subsystem->TryGetHandCardUi(DeployHandIndex1Based, Name, CardKind, Cost, Rules) &&
			(CardKind.Equals(TEXT("unit"), ESearchCase::IgnoreCase)
				|| CardKind.Equals(TEXT("building"), ESearchCase::IgnoreCase))) {
			ArmLine = TEXT("Hand: unit/building armed - click a highlighted tile to deploy.");
		} else if (IsArmedPushDirectionSpell()) {
			ArmLine = bPushSpellAwaitingUnit
				? TEXT("Hand: Push spell armed - click a unit to push, then choose a cardinal direction.")
				: TEXT("Hand: Unit selected - click a highlighted direction (N/E/S/W); orange shows the push path.");
		} else if (IsArmedFocusSpell()) {
			const FString XLine = IsArmedXCostSpell()
				? (ArmedXCostAmount > 0
					? FString::Printf(TEXT(" X=%d (%s) - adjust with bottom-right buttons."), ArmedXCostAmount, *ArmedXCostEnergyType)
					: FString::Printf(TEXT(" Need more %s energy for minimum X=%d."), *ArmedXCostEnergyType, ArmedXCostMin))
				: FString();
			ArmLine = bFocusSpellAwaitingCaster
				? FString::Printf(
					TEXT("Hand: Focus spell armed - click a highlighted unit to cast from.%s Cancel play to abort."), *XLine)
				: (IsArmedDirectionalFocusSpell()
					? FString::Printf(
						TEXT("Hand: Focus spell armed - click a highlighted direction cell; orange shows the helix footprint.%s"),
						*XLine)
					: FString::Printf(
						TEXT("Hand: Focus spell armed - click a highlighted enemy in range and line of sight from your selected unit.%s"),
						*XLine));
		} else if (ArmedMulticastTargetTotal > 1) {
			ArmLine = FString::Printf(
				TEXT("Hand: Multicast - pick target %d of %d (each must be different; Finish multicast to skip remaining)."),
				ArmedMulticastTargetsPicked + 1,
				ArmedMulticastTargetTotal);
		} else {
			ArmLine = TEXT("Hand: armed - click a tile to deploy/cast.");
		}
	} else if (Subsystem->HasUnitSelected() && Subsystem->IsSelectedUnitControlled() && Subsystem->CanControlledPlayerActInMainPhase()) {
		ArmLine = Subsystem->HasPendingMoveForControlledPlayer()
			? TEXT("Move preview: click another highlighted tile to change the preview, rotate if needed, then Confirm move.")
			: TEXT("Movement: click a highlighted tile to preview movement. Defend (main only) spends attack; consumes your standard move if unused. Recover (main only) spends attack - heal 2 at turn start if you take no damage. Dash spends attack for +1 movement instantly.");
	} else {
		ArmLine = TEXT("Hand: click a card to arm, then click a tile.");
	}
	const int32 ActivePid = Subsystem->GetActivePlayerId();
	const FString ActiveStr = ActivePid > 0 ? FString::Printf(TEXT("P%d"), ActivePid) : TEXT("-");
	FString Phase = FString::Printf(
		TEXT("Phase: %s | Active: %s | Controlling P%d | Hand idx: %d"),
		*Subsystem->PhaseLabel(),
		*ActiveStr,
		Subsystem->GetControlledPlayer(),
		DeployHandIndex1Based);
	if (Subsystem->IsAwaitingHandDiscard()) {
		const int32 Hv = Subsystem->GetHandViewPlayerId();
		Phase += FString::Printf(
			TEXT("\nDiscard step (hand > 8 at end turn): click a card in the strip, then Discard (bottom right). P%d."), Hv);
		if (Hv != Subsystem->GetControlledPlayer()) {
			Phase += FString::Printf(TEXT(" Hand strip follows P%d; Control P%d is unchanged."), Hv, Subsystem->GetControlledPlayer());
		}
	}
	if (Subsystem->IsAnyMainPhase() && !Subsystem->CanControlledPlayerActInMainPhase()) {
		Phase += TEXT("\n(Waiting for active player - use Control seat below when it is your turn.)");
	}
	if (IsArmedStackTargetEffect()) {
		Phase += TEXT("\n(Counter armed - click a highlighted queue entry on the right to target.)");
	}
	if (Subsystem->IsAnyMainPhase()) {
		const int32 Q = Subsystem->GetPhaseActionQueueCount();
		if (Q > 0) {
			Phase += FString::Printf(TEXT("\nBatch queue: %d item(s) queued - End main phase commits them."), Q);
		} else if (Subsystem->CanControlledPlayerActInMainPhase()) {
			Phase += TEXT("\n(Main: queue spells/abilities, then End main phase.)");
		}
	}
	if (Subsystem->IsAnyAttackDeclarationPhase() && Subsystem->CanControlledPlayerCommitAttacks()) {
		const int32 Q = Subsystem->GetPhaseActionQueueCount();
		Phase += FString::Printf(
			TEXT("\n(Attack Declaration: queue attacks%s, then Commit attacks.)"),
			Q > 0 ? *FString::Printf(TEXT(" + %d batched action(s)"), Q) : TEXT(""));
	}
	if (Subsystem->IsAnyReactionWindowPhase()) {
		const int32 Pri = Subsystem->GetReactionWindowPriorityPlayerId();
		const FString PriStr = Pri > 0 ? FString::Printf(TEXT("P%d"), Pri) : TEXT("-");
		const bool bYourTurn = Pri == Subsystem->GetControlledPlayer();
		Phase += FString::Printf(
			TEXT("\nReaction window - acting seat: %s%s. Cast reflex/blazing from hand or Pass."),
			*PriStr,
			bYourTurn ? TEXT(" (you)") : TEXT(""));
	}
	if (IsWebSocketClientP2()) {
		UTacticsWebSocketSubsystem* Net = nullptr;
		if (ResolveWebSocketNet(Net)) {
			Phase += FString::Printf(TEXT("\nNetwork: you are seat P%d - cast/pass only when rules allow this seat."),
				Net->GetClientRemoteSeatPlayerId());
		}
	} else {
		if (bShowDevTools) {
			Phase += FString::Printf(
				TEXT("\nLocal: match has %d seat(s); next Reset demo / New local uses %d (adjust with Local seats ±)."),
				Subsystem->GetMatchPlayerCount(),
				LocalDemoPlayerCount);
		}
	}
	Phase += FString::Printf(TEXT("\n%s"), *Subsystem->GetBoardMapDebugLine());
	FString CliFirst = LastCliOutput;
	UTacticsWebSocketSubsystem* Net = nullptr;
	if (ResolveWebSocketNet(Net)) {
		if (Net->IsClientConnecting()) {
			CliFirst = FString::Printf(TEXT("Connecting as seat P%d …"), Net->GetClientRemoteSeatPlayerId());
		} else if (!Net->GetClientConnectionError().IsEmpty()) {
			CliFirst = Net->GetClientConnectionError();
		} else if (Net->IsClientConnectedToHost()) {
			if (LastCliOutput.StartsWith(TEXT("Connecting"), ESearchCase::IgnoreCase)) {
				CliFirst = FString::Printf(TEXT("Connected as seat P%d - synced with host."), Net->GetClientRemoteSeatPlayerId());
			}
		}
	}
	if (bShowDevTools) {
		const FString TeamLine = Subsystem->FormatTeamAssignmentSummary();
		if (!TeamLine.IsEmpty()) {
			Phase += FString::Printf(TEXT("\n%s"), *TeamLine);
		}
	}
	if (!bShowDevTools) {
		Phase += TEXT("\nShortcuts: Esc / RMB cancel · Z undo · P/Space pass · Enter confirm · 1-9 hand · WASD/arrows pan · MMB drag pan · wheel zoom");
	}
	FString Body = FString::Printf(TEXT("%s\n\n%s\n%s\n%s"), *CliFirst, *Target, *ArmLine, *Phase);
	StatusText->SetColorAndOpacity(GetChromeTextColor());
	StatusText->SetText(FText::FromString(Body));
	if (SelectedStatsText.IsValid()) {
		SelectedStatsText->SetColorAndOpacity(GetChromeTextColor());
		FString Stats = TEXT("Click a unit or building to inspect stats and attributes.");
		if (Subsystem->HasUnitSelected()) {
			const FString SelectedStats = Subsystem->FormatSelectedUnitStats();
			if (!SelectedStats.IsEmpty()) {
				Stats = SelectedStats;
			}
		}
		SelectedStatsText->SetText(FText::FromString(Stats));
	}
	RefreshSelectedHandCardPanel();
}

void STacticsBoardPanel::RefreshSelectedHandCardPanel()
{
	if (!SelectedHandCardPanel.IsValid() || !SelectedHandCardTitleText.IsValid() || !SelectedHandCardDescVBox.IsValid()
		|| !Subsystem.IsValid()) {
		return;
	}
	// During the Energy-phase territory picker / a scan the normal detail sources are gone, but once the
	// player selects (arms) a territory or scanned card we show its full rules here - the same
	// read-before-you-commit view normal cards get.
	const bool bTerritoryArmed = Subsystem->GetPendingEnergyZoneChoiceCount() > 0 && ArmedZoneChoiceIndex >= 1;
	const bool bScanArmed = Subsystem->IsAwaitingScan() && ArmedScanCardIndex >= 1;
	// A placed territory selected in the Territories rail also shows its description here.
	const bool bTerritorySelected = SelectedTerritoryRepIndex >= 0
		&& SelectedTerritoryRepIndex < Subsystem->GetControlledTerritoryCount();
	const bool bTargetingAbilityOrPlay = bAbilityArmedForTile || bLandAbilityArmedForTile
		|| bHandArmedForTile || bReservesArmedForTile;
	const bool bShowDetailPanel = Subsystem->IsMatchReady()
		&& (bTerritoryArmed || bScanArmed || bTerritorySelected || bTargetingAbilityOrPlay
			|| (bDetailPanelShowsHandCard && Subsystem->GetPendingEnergyZoneChoiceCount() < 1
				&& !Subsystem->IsAwaitingScan())
			|| Subsystem->HasUnitSelected());
	if (!bShowDetailPanel) {
		SelectedHandCardPanel->SetVisibility(EVisibility::Collapsed);
		RebuildUnitAbilityHotbar(false);
		return;
	}
	FString Name, CardKind, Cost, Rules, ArtId, StatTokens;
	bool bGotCard = false;
	int32 DetailCardIndex1Based = 0;
	bool bDetailFromReserves = false;
	bool bDetailFromUnit = false;
	// Clicking a board unit fills this panel even if a territory or default hand card was showing.
	if (!bScanArmed && Subsystem->HasUnitSelected() && !bDetailPanelShowsHandCard) {
		bGotCard = Subsystem->TryGetSelectedUnitCardUi(Name, CardKind, Cost, Rules, ArtId);
		if (bGotCard) {
			bDetailFromUnit = true;
			Subsystem->TryGetSelectedUnitCardStatTokens(StatTokens);
			SelectedHandCardTitleText->SetText(
				FText::FromString(FString::Printf(TEXT("%s (%s)"), *Name, *CardKind.ToUpper())));
		}
	}
	if (!bGotCard && bTerritoryArmed && Subsystem->TryGetEnergyZoneChoiceDetail(ArmedZoneChoiceIndex, Name, Rules, ArtId)) {
		bGotCard = true;
		CardKind = TEXT("territory");
		SelectedHandCardTitleText->SetText(FText::FromString(FString::Printf(TEXT("%s (TERRITORY)"), *Name)));
	}
	if (!bGotCard && bTerritorySelected
		&& Subsystem->TryGetPlacedTerritoryDetail(SelectedTerritoryRepIndex, Name, Rules, ArtId)) {
		bGotCard = true;
		CardKind = TEXT("territory");
		SelectedHandCardTitleText->SetText(FText::FromString(FString::Printf(TEXT("%s (TERRITORY)"), *Name)));
	}
	if (!bGotCard && bScanArmed) {
		FString TypeTag;
		if (Subsystem->TryGetScanPeekCardDetail(ArmedScanCardIndex, Name, TypeTag, Cost, Rules, ArtId, StatTokens)) {
			bGotCard = true;
			CardKind = TypeTag;
			SelectedHandCardTitleText->SetText(
				FText::FromString(FString::Printf(TEXT("%s (%s)"), *Name, *TypeTag.ToUpper())));
		}
	}
	if (bReservesArmedForTile && bDetailPanelShowsHandCard && Subsystem->GetControlledReservesCount() > 0) {
		const int32 Rn = Subsystem->GetControlledReservesCount();
		const int32 Ridx = FMath::Clamp(DeployReservesIndex1Based, 1, Rn);
		bGotCard = Subsystem->TryGetReservesCardUi(Ridx, Name, CardKind, Cost, Rules);
		if (bGotCard) {
			bDetailFromReserves = true;
			DetailCardIndex1Based = Ridx;
			SelectedHandCardTitleText->SetText(
				FText::FromString(FString::Printf(TEXT("Reserves: %s (%s)"), *Name, *CardKind.ToUpper())));
			Subsystem->TryGetReservesCardArtId(Ridx, ArtId);
			Subsystem->TryGetReservesCardStatTokens(Ridx, StatTokens);
		}
	}
	if (!bGotCard && bHandArmedForTile && bDetailPanelShowsHandCard && Subsystem->GetControlledHandCount() > 0) {
		const int32 N = Subsystem->GetControlledHandCount();
		const int32 Idx = FMath::Clamp(DeployHandIndex1Based, 1, N);
		bGotCard = Subsystem->TryGetHandCardUi(Idx, Name, CardKind, Cost, Rules);
		if (bGotCard) {
			DetailCardIndex1Based = Idx;
			SelectedHandCardTitleText->SetText(FText::FromString(FString::Printf(TEXT("%s (%s)"), *Name, *CardKind.ToUpper())));
			Subsystem->TryGetHandCardArtId(Idx, ArtId);
			Subsystem->TryGetHandCardStatTokens(Idx, StatTokens);
		}
	}
	if (!bGotCard && Subsystem->HasUnitSelected()) {
		bGotCard = Subsystem->TryGetSelectedUnitCardUi(Name, CardKind, Cost, Rules, ArtId);
		if (bGotCard) {
			bDetailFromUnit = true;
			Subsystem->TryGetSelectedUnitCardStatTokens(StatTokens);
			SelectedHandCardTitleText->SetText(
				FText::FromString(FString::Printf(TEXT("%s (%s)"), *Name, *CardKind.ToUpper())));
		}
	}
	if (!bGotCard && bDetailPanelShowsHandCard && Subsystem->GetControlledHandCount() > 0) {
		const int32 N = Subsystem->GetControlledHandCount();
		const int32 Idx = FMath::Clamp(DeployHandIndex1Based, 1, N);
		bGotCard = Subsystem->TryGetHandCardUi(Idx, Name, CardKind, Cost, Rules);
		if (bGotCard) {
			DetailCardIndex1Based = Idx;
			SelectedHandCardTitleText->SetText(FText::FromString(FString::Printf(TEXT("%s (%s)"), *Name, *CardKind.ToUpper())));
			Subsystem->TryGetHandCardArtId(Idx, ArtId);
			Subsystem->TryGetHandCardStatTokens(Idx, StatTokens);
		}
	}
	if (!bGotCard) {
		SelectedHandCardPanel->SetVisibility(EVisibility::Collapsed);
		RebuildUnitAbilityHotbar(false);
		return;
	}
	SelectedHandCardPanel->SetVisibility(EVisibility::Visible);
	if (SelectedHandCardArtImage.IsValid()) {
		if (!ArtId.IsEmpty()) {
			if (const FSlateBrush* Brush = TacticsCardArtUi::GetCardArtBrush(ArtId, FVector2D(168.f, 232.f))) {
				SelectedHandCardArtImage->SetImage(Brush);
			} else {
				SelectedHandCardArtImage->SetImage(nullptr);
			}
		} else {
			SelectedHandCardArtImage->SetImage(nullptr);
		}
		SelectedHandCardArtImage->SetVisibility(EVisibility::Visible);
	}

	const bool bIsSpell = CardKind.Equals(TEXT("spell"), ESearchCase::IgnoreCase);
	// Stat row (HP / damage / range / movement icons) - units/buildings only.
	const bool bHasStats = !StatTokens.IsEmpty();
	// Parse ability strips before expanding targeting words - pills keep {ADJACENT} / {RANGE}n icons.
	FString RulesConverted = TacticsCardText::ConvertStatWords(TacticsCardText::ConvertSpeedAnnotations(Rules));
	if (bHasStats) {
		// Stats are shown in the dedicated row above; drop the duplicated leading stat preamble.
		RulesConverted = TacticsCardText::StripLeadingStatTokens(RulesConverted);
	}
	// Spell speed sits inline after energy cost (not in the description body).
	FString SpellSpeedStrip;
	if (CardKind.Equals(TEXT("spell"), ESearchCase::IgnoreCase)) {
		FString SpeedTag;
		const bool bGotSpeedTag = bDetailFromReserves
			? (DetailCardIndex1Based > 0
				&& Subsystem->TryGetReservesSpellSpeedTag(DetailCardIndex1Based, SpeedTag))
			: (DetailCardIndex1Based > 0
				&& Subsystem->TryGetHandSpellSpeedTag(DetailCardIndex1Based, SpeedTag));
		if (bGotSpeedTag && !SpeedTag.IsEmpty()) {
			SpellSpeedStrip = FString::Printf(TEXT("{%s}"), *SpeedTag.ToUpper());
		}
		FString SpeedFromRules;
		if (TacticsCardText::TryStripLeadingSpellSpeedMetadata(RulesConverted, SpeedFromRules)) {
			if (SpellSpeedStrip.IsEmpty()) {
				SpellSpeedStrip = SpeedFromRules;
			}
		}
	}
	const bool bHasCost = !Cost.IsEmpty();
	FString CostRowMarkup;
	if (bHasCost) {
		CostRowMarkup = TacticsCardText::MarkupEnergyTokens(Cost);
	}
	if (!SpellSpeedStrip.IsEmpty()) {
		const FString SpeedMarkup = TacticsCardText::MarkupEnergyTokens(SpellSpeedStrip);
		if (!CostRowMarkup.IsEmpty()) {
			CostRowMarkup += TEXT("  ");
		}
		CostRowMarkup += SpeedMarkup;
	}
	const bool bShowCostRow = !CostRowMarkup.IsEmpty();
	// Spells: cost + speed sit in the stat row (no empty stats gap). Units: cost above stats.
	const bool bSpellCostInStatsRow = bIsSpell && bShowCostRow;
	const bool bUnitCostInTopRow = !bIsSpell && bShowCostRow;
	if (SelectedHandCardCostRowBox.IsValid()) {
		SelectedHandCardCostRowBox->SetVisibility(
			bUnitCostInTopRow ? EVisibility::Visible : EVisibility::Collapsed);
	}
	if (SelectedHandCardCostText.IsValid()) {
		SelectedHandCardCostText->SetText(
			FText::FromString(bUnitCostInTopRow ? CostRowMarkup : FString()));
		SelectedHandCardCostText->SetVisibility(
			bUnitCostInTopRow ? EVisibility::Visible : EVisibility::Collapsed);
	}
	TArray<FString> MoveArtIds;
	TArray<FString> AttackArtIds;
	TArray<FString> ReactionArtIds;
	const bool bHasUnitActionIcons = bDetailFromUnit
		&& Subsystem->TryGetSelectedUnitActionIconStacks(MoveArtIds, AttackArtIds, ReactionArtIds);
	if (SelectedHandCardStatsRowBox.IsValid()) {
		SelectedHandCardStatsRowBox->SetVisibility(
			(bHasStats || bSpellCostInStatsRow || bHasUnitActionIcons) ? EVisibility::Visible : EVisibility::Collapsed);
	}
	if (SelectedHandCardStatsText.IsValid()) {
		FString StatsRowMarkup;
		if (bHasStats) {
			StatsRowMarkup = TacticsCardText::MarkupEnergyTokens(StatTokens);
		} else if (bSpellCostInStatsRow) {
			StatsRowMarkup = CostRowMarkup;
		}
		float StatsWrap = kHandCardDescWrapWidth;
		if (bHasUnitActionIcons) {
			StatsWrap -= kHandCardDetailActionIconsRowReserve;
		}
		StatsWrap = FMath::Max(StatsWrap, kHandCardArtW);
		SelectedHandCardStatsText->SetWrapTextAt(StatsWrap);
		SelectedHandCardStatsText->SetDecorators(
			TacticsCardText::MakeEnergyDecorators(14, TacticsCardText::ESpeedTooltipSubject::Attack));
		SelectedHandCardStatsText->SetText(FText::FromString(StatsRowMarkup));
		SelectedHandCardStatsText->SetVisibility(
			StatsRowMarkup.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible);
	}
	if (SelectedHandCardDivider.IsValid()) {
		SelectedHandCardDivider->SetVisibility(bShowCostRow && !Rules.IsEmpty() ? EVisibility::Visible : EVisibility::Hidden);
	}
	if (SelectedHandCardActionIconsBox.IsValid()) {
		if (bHasUnitActionIcons) {
			SelectedHandCardActionIconsBox->SetVisibility(EVisibility::Visible);
			RebuildUnitActionIconStack(SelectedHandCardMoveActionIconsVBox, MoveArtIds);
			RebuildUnitActionIconStack(SelectedHandCardAttackActionIconsVBox, AttackArtIds);
			RebuildUnitActionIconStack(SelectedHandCardReactionActionIconsVBox, ReactionArtIds);
		} else {
			SelectedHandCardActionIconsBox->SetVisibility(EVisibility::Collapsed);
			RebuildUnitActionIconStack(SelectedHandCardMoveActionIconsVBox, TArray<FString>());
			RebuildUnitActionIconStack(SelectedHandCardAttackActionIconsVBox, TArray<FString>());
			RebuildUnitActionIconStack(SelectedHandCardReactionActionIconsVBox, TArray<FString>());
		}
	}
	TArray<FTacticsCardGlossaryEntry> GlossaryEntries;
	if (CardKind.Equals(TEXT("territory"), ESearchCase::IgnoreCase)) {
		Subsystem->GetRulesTextGlossaryEntries(Rules, GlossaryEntries);
	} else if (bDetailFromReserves && DetailCardIndex1Based > 0) {
		Subsystem->GetReservesCardGlossaryEntries(DetailCardIndex1Based, GlossaryEntries);
	} else if (bDetailFromUnit) {
		Subsystem->GetSelectedUnitCardGlossaryEntries(GlossaryEntries);
	} else if (DetailCardIndex1Based > 0) {
		Subsystem->GetHandCardGlossaryEntries(DetailCardIndex1Based, GlossaryEntries);
	}
	TArray<FTacticsActiveEffectEntry> ActiveEffects;
	if (bDetailFromUnit) {
		Subsystem->GetSelectedUnitActiveEffectEntries(ActiveEffects);
	}
	const bool bAdvancedGlossary = Subsystem->IsShowingAdvancedCardText();
	// Live status stacks stay in the active-effects sidebar - not inline in description hovers.
	const TArray<FTacticsGlossaryNameBody> GlossaryMarkupEntries = bDetailFromUnit
		? BuildGlossaryNameBodies(GlossaryEntries, TArray<FTacticsActiveEffectEntry>{})
		: BuildGlossaryNameBodies(GlossaryEntries, ActiveEffects);
	RebuildHandCardGlossaryVBox(SelectedHandCardGlossaryVBox, GlossaryEntries, GlossaryMarkupEntries, bAdvancedGlossary);
	RebuildActiveEffectsSidebar(SelectedHandCardActiveEffectsVBox, ActiveEffects);
	if (SelectedHandCardGlossarySection.IsValid()) {
		SelectedHandCardGlossarySection->SetVisibility(
			bShowCardGlossaryDefinitions && !GlossaryEntries.IsEmpty()
				? EVisibility::Visible
				: EVisibility::Collapsed);
	}
	TacticsCardText::FCardRulesLayout BoardUnitLayout;
	const TacticsCardText::FCardRulesLayout* PrebuiltLayout = nullptr;
	if (bDetailFromUnit && TryComposeBoardUnitRulesLayout(BoardUnitLayout, RulesConverted)) {
		PrebuiltLayout = &BoardUnitLayout;
	}
	RebuildCardDetailDescVBox(SelectedHandCardDescVBox, RulesConverted, bDetailFromUnit, bIsSpell, DetailCardIndex1Based,
		bDetailFromReserves, GlossaryMarkupEntries, bAdvancedGlossary, PrebuiltLayout);
	RebuildUnitAbilityHotbar(bDetailFromUnit);
}

void STacticsBoardPanel::RebuildUnitAbilityHotbar(const bool bShowForSelectedUnit)
{
	if (!SelectedUnitAbilityHotbar.IsValid() || !SelectedUnitAbilityHotbarBox.IsValid() || !Subsystem.IsValid()) {
		return;
	}
	SelectedUnitAbilityHotbar->ClearChildren();
	if (!bShowForSelectedUnit || !Subsystem->HasUnitSelected() || !Subsystem->IsSelectedUnitControlled()) {
		SelectedUnitAbilityHotbarBox->SetVisibility(EVisibility::Collapsed);
		return;
	}
	const bool bReactionWindow = Subsystem->IsAnyReactionWindowPhase();
	const bool bAttackDeclaration = Subsystem->IsAnyAttackDeclarationPhase();
	const bool bMainPhase = Subsystem->IsAnyMainPhase();
	const bool bJammed = Subsystem->IsSelectedUnitJammed();

	const int32 AbilityCount = Subsystem->GetSelectedUnitActivatedAbilityCount();
	int32 VisibleCount = 0;
	for (int32 Ai = 1; Ai <= AbilityCount; ++Ai) {
		FString AKey, ALab, ASpeed, ACost;
		bool bUsed = false;
		bool bNeedCell = false;
		FString ARangeToken;
		int32 AUsesRemaining = 0;
		int32 AUsesMax = 0;
		if (!Subsystem->TryGetSelectedUnitActivatedAbilityUi(Ai, AKey, ALab, ASpeed, ACost, bUsed, bNeedCell, ARangeToken,
				AUsesRemaining, AUsesMax)) {
			continue;
		}
		if (bReactionWindow || bAttackDeclaration) {
			if (!ASpeed.Equals(TEXT("reflex"), ESearchCase::IgnoreCase) && !ASpeed.Equals(TEXT("blazing"), ESearchCase::IgnoreCase)) {
				continue;
			}
		} else if (!bMainPhase) {
			continue;
		}

		const bool bCanActivate = !bUsed && !bJammed && CanActivateAbilityFromDetail(ASpeed, bUsed);
		const bool bArmed = bAbilityArmedForTile && ArmedAbilityKey == AKey;
		const ETacticsAbilityVisualGroup Group = Subsystem->ResolveAbilityVisualGroup(AKey);
		const FLinearColor Frame = TacticsAbilityVisual::FrameColor(Group, bArmed);
		const FLinearColor SpeedColor = TacticsAbilityVisual::SpeedTagColor(ASpeed);

		FString BtnLabel = ALab;
		if (BtnLabel.Len() > 16) {
			BtnLabel = BtnLabel.Left(15) + TEXT("…");
		}
		FString UsesSuffix;
		if (AUsesMax > 0) {
			UsesSuffix = FString::Printf(TEXT(" (%d/%d)"), AUsesRemaining, AUsesMax);
		}

		FString Tooltip = ALab;
		Tooltip += FString::Printf(TEXT("\n[%s] %s"), *TacticsAbilityVisual::GroupShortLabel(Group),
			*TacticsAbilityVisual::GroupPrompt(Group));
		if (!ASpeed.IsEmpty()) {
			Tooltip += FString::Printf(TEXT("\nSpeed: %s"), *ASpeed);
		}
		if (bUsed) {
			Tooltip += TEXT("\nAlready used this turn.");
		} else if (bJammed) {
			Tooltip += TEXT("\nJammed - cannot use activated abilities.");
		} else if (!bCanActivate) {
			Tooltip += TEXT("\nUnavailable in the current phase or priority window.");
		}

		SelectedUnitAbilityHotbar->AddSlot()
			.Padding(0.f, 0.f, 6.f, 6.f)
			[
				SNew(SBorder)
					.BorderBackgroundColor(Frame)
					.Padding(FMargin(1.f))
					[
						SNew(SButton)
							.IsEnabled(bCanActivate)
							.ButtonColorAndOpacity(bCanActivate ? FLinearColor(0.1f, 0.12f, 0.18f, 1.f)
																: FLinearColor(0.08f, 0.08f, 0.1f, 0.55f))
							.ForegroundColor(kCardTextWhite)
							.ContentPadding(FMargin(10.f, 6.f))
							.ToolTipText(FText::FromString(Tooltip))
							.OnClicked_Lambda([this, AKey, bNeedCell]() {
								ArmSelectedAbilityForTargeting(AKey, bNeedCell);
								return FReply::Handled();
							})
							[
								SNew(SHorizontalBox)
								+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.f, 0.f, 6.f, 0.f)
									[
										SNew(SBorder)
											.BorderBackgroundColor(SpeedColor)
											.Padding(FMargin(4.f, 1.f))
											[
												SNew(STextBlock)
													.Text(FText::FromString(ASpeed.Left(1).ToUpper()))
													.Font(TacticsCardText::DefaultFont(TEXT("Bold"), 9))
													.ColorAndOpacity(kCardTextWhite)
											]
									]
								+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
									[
										SNew(STextBlock)
											.Text(FText::FromString(BtnLabel + UsesSuffix))
											.Font(TacticsCardText::DefaultFont(TEXT("Regular"), 11))
											.ColorAndOpacity(kCardTextWhite)
									]
							]
					]
			];
		++VisibleCount;
	}
	SelectedUnitAbilityHotbarBox->SetVisibility(VisibleCount > 0 ? EVisibility::Visible : EVisibility::Collapsed);
}

bool STacticsBoardPanel::CanActivateAbilityFromDetail(const FString& SpeedTag, const bool bUsed) const
{
	if (!Subsystem.IsValid() || bUsed || Subsystem->IsAwaitingHandDiscard() || Subsystem->IsAwaitingScan()
		|| Subsystem->IsAwaitingTerritoryLoot() || Subsystem->IsSelectedUnitJammed()
		|| !Subsystem->IsSelectedUnitControlled()) {
		return false;
	}
	if (Subsystem->IsAnyReactionWindowPhase()) {
		return Subsystem->CanControlledPlayerPassPriority()
			&& (SpeedTag.Equals(TEXT("reflex"), ESearchCase::IgnoreCase)
				|| SpeedTag.Equals(TEXT("blazing"), ESearchCase::IgnoreCase));
	}
	const bool bReflexOrBlazing = SpeedTag.Equals(TEXT("reflex"), ESearchCase::IgnoreCase)
		|| SpeedTag.Equals(TEXT("blazing"), ESearchCase::IgnoreCase);
	if (Subsystem->IsAnyAttackDeclarationPhase()) {
		return bReflexOrBlazing && Subsystem->CanControlledPlayerActInMainPhase();
	}
	return Subsystem->IsAnyMainPhase() && Subsystem->CanControlledPlayerActInMainPhase();
}

bool STacticsBoardPanel::TryResolveAbilityForDescBlock(const FString& BlockName, const int32 ActivatedAbilityIndex, FString& OutKey,
	bool& bNeedCell, bool& bUsed, bool& bEnabled) const
{
	OutKey.Reset();
	bNeedCell = false;
	bUsed = false;
	bEnabled = false;
	if (!Subsystem.IsValid() || BlockName.IsEmpty()) {
		return false;
	}
	const int32 AbilityCount = Subsystem->GetSelectedUnitActivatedAbilityCount();
	auto NamesMatch = [](const FString& A, const FString& B) {
		return A.Equals(B, ESearchCase::IgnoreCase)
			|| A.Contains(B, ESearchCase::IgnoreCase)
			|| B.Contains(A, ESearchCase::IgnoreCase);
	};
	auto ApplyResolved = [&](const int32 Index1Based) -> bool {
		FString Key, Label, Speed, Cost, RangeToken;
		bool bNeed = false;
		bool bTurnUsed = false;
		int32 UsesRemaining = 0;
		int32 UsesMax = 0;
		if (!Subsystem->TryGetSelectedUnitActivatedAbilityUi(Index1Based, Key, Label, Speed, Cost, bTurnUsed, bNeed, RangeToken,
				UsesRemaining, UsesMax)) {
			return false;
		}
		OutKey = Key;
		bNeedCell = bNeed;
		bUsed = bTurnUsed;
		bEnabled = CanActivateAbilityFromDetail(Speed, bTurnUsed);
		return true;
	};
	for (int32 Ai = 1; Ai <= AbilityCount; ++Ai) {
		FString Key, Label, Speed, Cost, RangeToken;
		bool bNeed = false;
		bool bTurnUsed = false;
		int32 UsesRemaining = 0;
		int32 UsesMax = 0;
		if (!Subsystem->TryGetSelectedUnitActivatedAbilityUi(Ai, Key, Label, Speed, Cost, bTurnUsed, bNeed, RangeToken, UsesRemaining,
				UsesMax)) {
			continue;
		}
		if (NamesMatch(Label, BlockName)) {
			OutKey = Key;
			bNeedCell = bNeed;
			bUsed = bTurnUsed;
			bEnabled = CanActivateAbilityFromDetail(Speed, bTurnUsed);
			return true;
		}
	}
	if (ActivatedAbilityIndex >= 0 && ActivatedAbilityIndex < AbilityCount) {
		return ApplyResolved(ActivatedAbilityIndex + 1);
	}
	OutKey.Reset();
	return false;
}

bool STacticsBoardPanel::TryComposeBoardUnitRulesLayout(TacticsCardText::FCardRulesLayout& OutLayout,
	FString& OutRulesConverted) const
{
	if (!Subsystem.IsValid() || !Subsystem->HasUnitSelected()) {
		return false;
	}
	FString BaseRules;
	if (!Subsystem->TryGetSelectedUnitCardRulesBase(BaseRules)) {
		return false;
	}
	FString StatTokens;
	Subsystem->TryGetSelectedUnitCardStatTokens(StatTokens);
	OutRulesConverted = TacticsCardText::ConvertStatWords(TacticsCardText::ConvertSpeedAnnotations(BaseRules));
	if (!StatTokens.IsEmpty()) {
		OutRulesConverted = TacticsCardText::StripLeadingStatTokens(OutRulesConverted);
	}
	OutLayout = TacticsCardText::ParseCardRulesLayout(OutRulesConverted);
	const FString GainedKeywords = Subsystem->GetSelectedUnitGainedKeywordsStrip();
	if (!GainedKeywords.IsEmpty()) {
		if (OutLayout.Preamble.IsEmpty()) {
			OutLayout.Preamble = GainedKeywords;
		} else {
			OutLayout.Preamble += TEXT(". ") + GainedKeywords;
		}
	}
	TArray<FTacticsRuntimePassiveStrip> RuntimePassives;
	Subsystem->GetSelectedUnitRuntimePassiveStrips(RuntimePassives);
	for (const FTacticsRuntimePassiveStrip& Passive : RuntimePassives) {
		if (Passive.Name.IsEmpty()) {
			continue;
		}
		TacticsCardText::FCardRulesAbilityBlock Block;
		Block.bIsPassive = true;
		Block.MetadataStrip = TEXT("{PASSIVE}");
		Block.Name = Passive.Name;
		Block.Effect = Passive.RulesText;
		OutLayout.Abilities.Add(MoveTemp(Block));
	}
	TacticsCardText::SortCardRulesLayoutPassivesBeforeAbilities(OutLayout);
	return true;
}

void STacticsBoardPanel::RebuildCardDetailDescVBox(const TSharedPtr<SVerticalBox>& Box, const FString& RulesConverted,
	const bool bUnitBoardDetail, const bool bIsSpellCard, const int32 DetailCardIndex1Based, const bool bDetailFromReserves,
	const TArray<FTacticsGlossaryNameBody>& GlossaryMarkupEntries, const bool bAdvancedGlossary,
	const TacticsCardText::FCardRulesLayout* PrebuiltLayout)
{
	if (!Box.IsValid()) {
		return;
	}
	Box->ClearChildren();

	const TacticsCardText::ESpeedTooltipSubject ProseSpeedSubject = bIsSpellCard
		? TacticsCardText::ESpeedTooltipSubject::Spell
		: TacticsCardText::ESpeedTooltipSubject::Ability;
	const auto MakeDescRichText = [&](const float WrapWidth, const FString& Markup, const bool bBoldName,
		const TacticsCardText::ESpeedTooltipSubject SpeedSubject) {
		const FTextBlockStyle& Style = bBoldName
			? TacticsCardText::DesignTextStyle(kHandCardDescFontSize, kCardTextWhite, TEXT("Bold"))
			: TacticsCardText::EnergyTextStyle(kHandCardDescFontSize, kCardTextWhite);
		return SNew(SBox)
			.WidthOverride(WrapWidth)
			[
				SNew(SRichTextBlock)
					.WrapTextAt(WrapWidth)
					.LineHeightPercentage(bBoldName ? 1.12f : 1.18f)
					.TextStyle(&Style)
					.DecoratorStyleSet(&FCoreStyle::Get())
					.Decorators(TacticsCardText::MakeEnergyDecorators(kHandCardDescFontSize, SpeedSubject))
					.Text(FText::FromString(Markup))
			];
	};

	const auto AddProseBlock = [&](const FString& Text, const int32 BottomPad) {
		if (Text.IsEmpty()) {
			return;
		}
		const FString Body = TacticsCardText::FormatDescriptionProse(TacticsCardText::ExpandTargetingTokensToWords(Text));
		Box->AddSlot()
			.AutoHeight()
			.Padding(0.f, 0.f, 0.f, BottomPad)
			[
				MakeDescRichText(kHandCardDescWrapWidth,
					TacticsCardText::MarkupDescriptionText(Body, GlossaryMarkupEntries, bAdvancedGlossary), false,
					ProseSpeedSubject)
			];
	};

	const TacticsCardText::FCardRulesLayout Layout = PrebuiltLayout ? *PrebuiltLayout
		: TacticsCardText::ParseCardRulesLayout(RulesConverted);
	if (Layout.Abilities.IsEmpty()) {
		const FString& ProseSource = !Layout.Preamble.IsEmpty() ? Layout.Preamble : RulesConverted;
		AddProseBlock(ProseSource.IsEmpty() ? TEXT("(No description.)") : ProseSource, 0.f);
		return;
	}

	AddProseBlock(Layout.Preamble, Layout.Preamble.IsEmpty() ? 0.f : 10.f);

	const auto MakeAbilityProseMarkup = [&](const FString& Text) {
		const FString Body = TacticsCardText::FormatDescriptionProse(
			TacticsCardText::ExpandTargetingTokensInAbilityProse(Text));
		return TacticsCardText::MarkupDescriptionText(Body, GlossaryMarkupEntries, bAdvancedGlossary);
	};

	int32 ActivatedAbilityIdx = 0;
	for (int32 AbilityIdx = 0; AbilityIdx < Layout.Abilities.Num(); ++AbilityIdx) {
		const TacticsCardText::FCardRulesAbilityBlock& Ability = Layout.Abilities[AbilityIdx];
		const bool bIsPassive = Ability.bIsPassive;
		const int32 CatalogAbilityIdx = bIsPassive ? -1 : ActivatedAbilityIdx;
		FString AbilityKey;
		bool bNeedCell = false;
		bool bUsed = false;
		bool bEnabled = false;
		int32 UsesRemaining = 0;
		int32 UsesMax = 0;
		const bool bResolvable = !bIsPassive && bUnitBoardDetail
			&& TryResolveAbilityForDescBlock(Ability.Name, CatalogAbilityIdx, AbilityKey, bNeedCell, bUsed, bEnabled);
		FString MetadataStripForPill = Ability.MetadataStrip;
		if (bResolvable && Subsystem.IsValid()) {
			const int32 AbilityCount = Subsystem->GetSelectedUnitActivatedAbilityCount();
			for (int32 Ai = 1; Ai <= AbilityCount; ++Ai) {
				FString KeyProbe, LabelProbe, SpeedProbe, CostProbe, RangeProbe;
				bool bUsedProbe = false;
				bool bNeedProbe = false;
				int32 RemProbe = 0;
				int32 MaxProbe = 0;
				if (!Subsystem->TryGetSelectedUnitActivatedAbilityUi(Ai, KeyProbe, LabelProbe, SpeedProbe, CostProbe, bUsedProbe,
						bNeedProbe, RangeProbe, RemProbe, MaxProbe)) {
					continue;
				}
				if (KeyProbe == AbilityKey) {
					UsesRemaining = RemProbe;
					UsesMax = MaxProbe;
					const FString LiveStrip = TacticsCardText::BuildAbilityMetadataStrip(SpeedProbe, RangeProbe, CostProbe);
					if (!LiveStrip.IsEmpty()) {
						MetadataStripForPill = LiveStrip;
					}
					break;
				}
			}
		} else if (!bIsPassive && !bUnitBoardDetail && DetailCardIndex1Based > 0 && Subsystem.IsValid()) {
			FString DetailStrip;
			if (Subsystem->TryGetDetailCardAbilityMetadataStrip(DetailCardIndex1Based, bDetailFromReserves, Ability.Name, CatalogAbilityIdx,
					DetailStrip)) {
				MetadataStripForPill = DetailStrip;
			}
			if (Subsystem->TryGetDetailCardAbilityUses(DetailCardIndex1Based, bDetailFromReserves, Ability.Name, CatalogAbilityIdx,
					UsesRemaining, UsesMax)) {
				UsesRemaining = UsesMax;
			}
		}

		if (!bIsPassive) {
			++ActivatedAbilityIdx;
		}

		const bool bArmed = bResolvable && bAbilityArmedForTile && ArmedAbilityKey == AbilityKey;
		const bool bShowUses = !bIsPassive && UsesMax > 0;
		const int32 MetadataFontSize = kHandCardDescFontSize - 1;
		const float PillWrapWidth = bResolvable ? kHandCardDescPillWrapWidth : kHandCardDescWrapWidth;

		TSharedRef<SVerticalBox> PillBody = SNew(SVerticalBox);
		if (!bIsPassive && (!MetadataStripForPill.IsEmpty() || bShowUses)) {
			PillBody->AddSlot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 6.f)
				[
					BuildAbilityPillHeaderRow(MetadataStripForPill, UsesRemaining, UsesMax, bShowUses, bArmed, MetadataFontSize)
				];
		} else if (bIsPassive && !Ability.MetadataStrip.IsEmpty()) {
			PillBody->AddSlot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 6.f)
				[
					TacticsCardText::BuildMetadataPillWidget(Ability.MetadataStrip, MetadataFontSize, kCardTextWhite)
				];
		}
		if (!Ability.Name.IsEmpty()) {
			PillBody->AddSlot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 2.f)
				[
					MakeDescRichText(PillWrapWidth, MakeAbilityProseMarkup(Ability.Name), true,
						TacticsCardText::ESpeedTooltipSubject::Ability)
				];
		}
		if (!Ability.Effect.IsEmpty()) {
			PillBody->AddSlot()
				.AutoHeight()
				[
					MakeDescRichText(PillWrapWidth, MakeAbilityProseMarkup(Ability.Effect), false,
						TacticsCardText::ESpeedTooltipSubject::Ability)
				];
		}

		bool bNeedStack = false;
		if (bResolvable && Subsystem.IsValid()) {
			Subsystem->TryGetSelectedAbilityRequiresStackTarget(AbilityKey, bNeedStack);
		}
		const FLinearColor Frame = bArmed
			? TacticsAbilityVisual::FrameColor(Subsystem->ResolveAbilityVisualGroup(AbilityKey), true)
			: TacticsAbilityVisual::FrameColor(Subsystem->ResolveAbilityVisualGroup(AbilityKey), false);

		TSharedRef<SWidget> PillChrome = PillBody;
		if (bResolvable) {
			PillChrome = SNew(SBorder)
				.BorderBackgroundColor(Frame)
				.Padding(FMargin(2.f))
				[
					SNew(SButton)
						.IsEnabled(bEnabled)
						.ButtonColorAndOpacity(FLinearColor(0.08f, 0.08f, 0.1f, 1.f))
						.ForegroundColor(kCardTextWhite)
						.ContentPadding(FMargin(8.f, 8.f))
						.OnClicked_Lambda([this, AbilityKey, bNeedCell]() {
							ArmSelectedAbilityForTargeting(AbilityKey, bNeedCell);
							return FReply::Handled();
						})
						[
							PillBody
						]
				];
		}

		Box->AddSlot()
			.AutoHeight()
			.Padding(0.f, 2.f, 0.f, 12.f)
			[
				PillChrome
			];
	}
}

