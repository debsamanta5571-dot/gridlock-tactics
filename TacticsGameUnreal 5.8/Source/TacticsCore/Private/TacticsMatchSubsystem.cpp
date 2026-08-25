#include "TacticsMatchSubsystem.h"
#include "TacticsProjectContentReader.h"

#include "tactics/actions/actions.hpp"
#include "tactics/actions/move_resolution.hpp"
#include "tactics/apps/master_cli_dispatch.hpp"
#include "tactics/apps/sandbox_match.hpp"
#include "tactics/attributes/attributes.hpp"
#include "tactics/board/board.hpp"
#include "tactics/board/board_layout.hpp"
#include "tactics/bot/bot_match_outcome.hpp"
#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/content/card_glossary.hpp"
#include "tactics/content/glossary_copy.hpp"
#include "tactics/content/project_content.hpp"
#include "tactics/cards/focus_spell.hpp"
#include "tactics/combat/taunt.hpp"
#include "tactics/cards/cards.hpp"
#include "tactics/cards/passive_catalog.hpp"
#include "tactics/combat/combat_resolver.hpp"
#include "tactics/actions/board_targeting.hpp"
#include "tactics/combat/ability_resolve_viz.hpp"
#include "tactics/entities/entity.hpp"
#include "tactics/entities/player_base.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/common/types.hpp"
#include "tactics/core/passive_action_order.hpp"
#include "tactics/core.hpp"
#include "tactics/sync/match_sync.hpp"
#include "tactics/sync/match_auth.hpp"
#include "tactics/effects/effect_registry.hpp"
#include "tactics/effects/status_effect_catalog.hpp"

#include "Containers/StringConv.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Containers/Ticker.h"

#include "TacticsMatchSubsystem_Internal.h"

static_assert(UTacticsMatchSubsystem::NetworkCheckpointCommandInterval == tactics::kNetworkCheckpointCommandInterval,
	"Keep UE NetworkCheckpointCommandInterval in sync with cpp_core match_sync.hpp");

namespace
{
/** Loads a deck JSON from TacticsData/decks by id (starter, test, or filename). */
bool TryLoadDeckListById(const FString& DeckId, tactics::DeckListDefinition& OutDl, FString& OutRel)
{
	if (DeckId.IsEmpty()) {
		return false;
	}
	TArray<FString> RelCandidates;
	if (DeckId == TEXT("starter")) {
		RelCandidates.Add(TEXT("TacticsData/decks/starter_deck.json"));
	} else if (DeckId == TEXT("test")) {
		RelCandidates.Add(TEXT("TacticsData/decks/test_deck.json"));
	} else if (DeckId == TEXT("example_tournament")) {
		RelCandidates.Add(TEXT("TacticsData/decks/example_tournament_deck.json"));
	}
	RelCandidates.Add(FString::Printf(TEXT("TacticsData/decks/%s.json"), *DeckId));
	RelCandidates.Add(FString::Printf(TEXT("TacticsData/decks/%s_deck.json"), *DeckId));
	for (const FString& Rel : RelCandidates) {
		std::string DeckUtf8, DeckErr;
		if (!TacticsProjectContentReader::ReadUtf8File(TCHAR_TO_UTF8(*Rel), DeckUtf8, DeckErr)) {
			continue;
		}
		if (!tactics::load_deck_list_from_json_utf8(DeckUtf8, OutDl, DeckErr)) {
			UE_LOG(LogTemp, Warning, TEXT("Match deck '%s': %s"), *DeckId, UTF8_TO_TCHAR(DeckErr.c_str()));
			continue;
		}
		OutRel = Rel;
		return true;
	}
	return false;
}
}  // namespace


UTacticsMatchSubsystem::~UTacticsMatchSubsystem()
{
	StopBotAutoPlayTicker();
}

int32 UTacticsMatchSubsystem::GetMatchPlayerCount() const
{
	if (!Game) {
		return 0;
	}
	return static_cast<int32>(Game->turn_manager.players.size());
}

void UTacticsMatchSubsystem::RebuildReachableMoveHighlights()
{
	ReachableMoveCells.Empty();
	if (IsBoardTargetAoEHoverPreviewActive()) {
		return;
	}
	if (!Game || !Selected) {
		return;
	}
	if (!CanControlledPlayerActInMainPhase()) {
		return;
	}
	if (!Selected->owner || *Selected->owner != ControlledPlayer) {
		return;
	}
	if (Selected->moves_remaining_this_turn <= 0) {
		return;
	}
	if (Game->turn_manager.current_phase == tactics::TurnPhase::BonusAttackDeclaration
		&& !tactics::unit_may_move_during_bonus_attack_declaration(*Selected)) {
		return;
	}
	const std::vector<std::pair<int, int>> cells = tactics::gather_reachable_move_goal_cells(*Game, Selected);
	ReachableMoveCells.Reserve(static_cast<int32>(cells.size()));
	for (const auto& [wx, wy] : cells) {
		ReachableMoveCells.Add(FIntPoint(wx, wy));
	}
}

void UTacticsMatchSubsystem::RebuildPendingMoveFootprintHighlights()
{
	PendingMoveDestinationCells.Empty();
	PendingMoveOriginCells.Empty();
	if (!Game) {
		return;
	}
	const std::optional<tactics::PendingMoveSelection> Pending = Game->get_pending_move_for(ControlledPlayer);
	if (!Pending) {
		return;
	}
	const auto It = Game->board.all_entities_map.find(Pending->unit_entity_id);
	if (It == Game->board.all_entities_map.end() || !It->second) {
		return;
	}
	const tactics::Unit* Unit = dynamic_cast<const tactics::Unit*>(It->second.get());
	if (!Unit) {
		return;
	}
	auto AddFootprint = [](TSet<FIntPoint>& Out, int AnchorX, int AnchorY, const tactics::Unit& U, int QuarterTurnsCw) {
		auto Shape = tactics::entity_shape_offsets(U);
		if (QuarterTurnsCw != 0) {
			tactics::rotate_shape_offsets_n_quarters_cw(Shape, QuarterTurnsCw);
		}
		for (const auto& [dx, dy] : Shape) {
			Out.Add(FIntPoint(AnchorX + dx, AnchorY + dy));
		}
	};
	AddFootprint(PendingMoveDestinationCells, Pending->resolved_ax, Pending->resolved_ay, *Unit, Pending->quarter_turns_cw);
	if (Unit->position) {
		AddFootprint(PendingMoveOriginCells, Unit->position->first, Unit->position->second, *Unit, 0);
	}
}

void UTacticsMatchSubsystem::RebuildAttackTargetHighlights()
{
	AttackTargetCells.Empty();
	if (IsBoardTargetAoEHoverPreviewActive()) {
		return;
	}
	if (!Game || !Selected || !Selected->owner || *Selected->owner != ControlledPlayer) {
		return;
	}
	if (!CanControlledPlayerActInMainPhase()) {
		return;
	}
	if (HasPendingMoveForControlledPlayer()) {
		return;
	}
	if (!Selected->position || Selected->attacks_remaining_this_turn <= 0) {
		return;
	}
	const std::shared_ptr<tactics::Unit> AttackActor = Game->unit_at_validation_pose(Selected);
	const auto cells = tactics::gather_attackable_goal_cells(*Game, AttackActor, ControlledPlayer);
	AttackTargetCells.Reserve(static_cast<int32>(cells.size()));
	for (const auto& [Wx, Wy] : cells) {
		AttackTargetCells.Add(FIntPoint(Wx, Wy));
	}
}



void UTacticsMatchSubsystem::RebuildBoardTargetAoEPreview()
{
	BoardTargetAoEBlastCells.Empty();
	if (!Game) {
		return;
	}
	int AimX = 0;
	int AimY = 0;
	if (!TryGetPendingCliWorldCell(AimX, AimY)) {
		return;
	}

	std::string EffectKey;
	std::map<std::string, int> Payload;
	std::map<std::string, std::string> StringPayload;
	const tactics::Entity* Actor = nullptr;

	if (!BoardTargetPreviewAbilityKey.IsEmpty() && Selected && IsSelectedUnitControlled()) {
		tactics::AbilitySpec Ability;
		bool bFound = tactics::try_get_ability_from_catalog(TCHAR_TO_UTF8(*BoardTargetPreviewAbilityKey), Ability);
		if (!bFound) {
			const std::string KeyUtf8 = TCHAR_TO_UTF8(*BoardTargetPreviewAbilityKey);
			for (const tactics::AbilitySpec& OnUnit : Selected->activated_abilities) {
				if (OnUnit.key == KeyUtf8) {
					Ability = OnUnit;
					bFound = true;
					break;
				}
			}
		}
		if (!bFound) {
			return;
		}
		EffectKey = Ability.effect_key;
		Payload = Ability.effect_payload;
		StringPayload = Ability.effect_string_payload;
		if (const std::shared_ptr<tactics::Unit> ActorUnit = Game->unit_at_validation_pose(Selected)) {
			Actor = ActorUnit.get();
		}
	} else if (!BoardTargetPreviewSpellEffectKey.IsEmpty() && !bBoardTargetPreviewSelectingFocusCaster) {
		EffectKey = TCHAR_TO_UTF8(*BoardTargetPreviewSpellEffectKey);
		Payload = BoardTargetPreviewSpellPayload;
		StringPayload = spell_string_payload_from_shape(BoardTargetPreviewSpellShapeKey);
		if (bBoardTargetPreviewUsesFocusCaster && Selected && IsSelectedUnitControlled()) {
			if (const std::shared_ptr<tactics::Unit> Caster = Game->unit_at_validation_pose(Selected)) {
				Actor = Caster.get();
			}
		}
	} else {
		return;
	}

	if (tactics::effect_key_uses_push_direction_aim(EffectKey) && bBoardTargetPreviewSelectingPushDirection
			&& BoardTargetPushEntityWorldX >= 0 && BoardTargetPushEntityWorldY >= 0) {
		const auto Cells = tactics::preview_push_direction_spell_path_cells(
			*Game, BoardTargetPushEntityWorldX, BoardTargetPushEntityWorldY, AimX, AimY, Payload);
		for (const auto& [Wx, Wy] : Cells) {
			BoardTargetAoEBlastCells.Add(FIntPoint(Wx, Wy));
		}
		return;
	}

	if (!tactics::effect_supports_aoe_blast_preview(EffectKey)) {
		return;
	}

	const auto Cells = tactics::preview_effect_aoe_blast_cells(*Game, Actor, AimX, AimY, EffectKey, Payload, StringPayload);
	for (const auto& [Wx, Wy] : Cells) {
		BoardTargetAoEBlastCells.Add(FIntPoint(Wx, Wy));
	}
}

void UTacticsMatchSubsystem::RebuildBoardTargetHighlights()
{
	BoardTargetEnemyCells.Empty();
	BoardTargetOtherCells.Empty();
	if (!Game) {
		return;
	}
	const bool bPreviewingAbility = !BoardTargetPreviewAbilityKey.IsEmpty();
	if (bPreviewingAbility && Selected && IsSelectedUnitControlled()) {
		const std::shared_ptr<tactics::Unit> AbilityActor = Game->unit_at_validation_pose(Selected);
		if (AbilityActor) {
			const auto gathered = tactics::gather_ability_board_target_cells(
				*Game, AbilityActor, ControlledPlayer, TCHAR_TO_UTF8(*BoardTargetPreviewAbilityKey));
			for (const auto& [Wx, Wy] : gathered.enemy_cells) {
				BoardTargetEnemyCells.Add(FIntPoint(Wx, Wy));
			}
			for (const auto& [Wx, Wy] : gathered.other_cells) {
				BoardTargetOtherCells.Add(FIntPoint(Wx, Wy));
			}
			RebuildBoardTargetAoEPreview();
			return;
		}
	}
	if (bBoardTargetPreviewSelectingFocusCaster && BoardTargetPreviewKind.has_value()) {
		if (bBoardTargetPreviewForcedDamageSpellFocus) {
			const auto gathered = tactics::gather_forced_damage_spell_focus_caster_cells(*Game, ControlledPlayer);
			for (const auto& [Wx, Wy] : gathered.other_cells) {
				BoardTargetOtherCells.Add(FIntPoint(Wx, Wy));
			}
		} else {
			const tactics::BoardTargetKind Kind = *BoardTargetPreviewKind;
			const auto gathered = tactics::gather_focus_caster_highlight_cells(
				*Game, ControlledPlayer, TCHAR_TO_UTF8(*BoardTargetPreviewSpellEffectKey), BoardTargetFocusRange, Kind,
				BoardTargetPreviewSpellPayload, spell_string_payload_from_shape(BoardTargetPreviewSpellShapeKey));
			for (const auto& [Wx, Wy] : gathered.other_cells) {
				BoardTargetOtherCells.Add(FIntPoint(Wx, Wy));
			}
		}
		return;
	}
	if (!BoardTargetPreviewSpellEffectKey.IsEmpty() && !bBoardTargetPreviewSelectingFocusCaster) {
		const std::string EffectKeyUtf8 = TCHAR_TO_UTF8(*BoardTargetPreviewSpellEffectKey);
		const tactics::BoardTargetKind Kind =
			BoardTargetPreviewKind.value_or(tactics::BoardTargetKind::Enemy);
		if (bBoardTargetPreviewSelectingPushDirection && BoardTargetPushEntityWorldX >= 0 && BoardTargetPushEntityWorldY >= 0) {
			const auto gathered = tactics::gather_push_direction_indicator_cells_for_target(
				*Game, BoardTargetPushEntityWorldX, BoardTargetPushEntityWorldY, BoardTargetPreviewSpellPayload);
			for (const auto& [Wx, Wy] : gathered.other_cells) {
				BoardTargetOtherCells.Add(FIntPoint(Wx, Wy));
			}
			RebuildBoardTargetAoEPreview();
			return;
		}
		if (tactics::effect_key_uses_push_direction_aim(EffectKeyUtf8)) {
			const auto gathered = tactics::gather_push_direction_spell_entity_cells(
				*Game, ControlledPlayer, EffectKeyUtf8, Kind, BoardTargetPreviewRequireUnitTypes);
			for (const auto& [Wx, Wy] : gathered.enemy_cells) {
				BoardTargetEnemyCells.Add(FIntPoint(Wx, Wy));
			}
			for (const auto& [Wx, Wy] : gathered.other_cells) {
				BoardTargetOtherCells.Add(FIntPoint(Wx, Wy));
			}
			return;
		}
		std::shared_ptr<tactics::Unit> SpellCaster;
		if (bBoardTargetPreviewUsesFocusCaster && Selected && IsSelectedUnitControlled()) {
			SpellCaster = std::dynamic_pointer_cast<tactics::Unit>(Selected);
			if (SpellCaster) {
				SpellCaster = Game->unit_at_validation_pose(SpellCaster);
			}
		}
		tactics::LobbedAoeCenterCellProbe SpellProbe;
		if (ArmedBoardTargetHandIndex1Based >= 1) {
			const auto HandIt = Game->players_hands.find(ControlledPlayer);
			if (HandIt != Game->players_hands.end() && HandIt->second != nullptr
				&& ArmedBoardTargetHandIndex1Based <= static_cast<int32>(HandIt->second->size())) {
				const tactics::CardInstanceId CardId =
					(*HandIt->second)[static_cast<size_t>(ArmedBoardTargetHandIndex1Based - 1)];
				std::shared_ptr<tactics::Entity> FocusEntity = SpellCaster
					? std::static_pointer_cast<tactics::Entity>(SpellCaster)
					: nullptr;
				SpellProbe = [this, CardId, FocusEntity](const int Wx, const int Wy) {
					return tactics::spell_probe_valid(
						*Game, CardId, ControlledPlayer, FocusEntity, tactics::CardPlayZone::Hand, Wx, Wy);
				};
			}
		} else if (ArmedBoardTargetReservesIndex1Based >= 1) {
			const auto DeckIt = Game->players_decks.find(ControlledPlayer);
			if (DeckIt != Game->players_decks.end()
				&& ArmedBoardTargetReservesIndex1Based <= static_cast<int32>(DeckIt->second.reserves.size())) {
				const tactics::CardInstanceId CardId =
					DeckIt->second.reserves[static_cast<size_t>(ArmedBoardTargetReservesIndex1Based - 1)];
				std::shared_ptr<tactics::Entity> FocusEntity = SpellCaster
					? std::static_pointer_cast<tactics::Entity>(SpellCaster)
					: nullptr;
				SpellProbe = [this, CardId, FocusEntity](const int Wx, const int Wy) {
					return tactics::spell_probe_valid(
						*Game, CardId, ControlledPlayer, FocusEntity, tactics::CardPlayZone::Reserves, Wx, Wy);
				};
			}
		}
		const auto gathered = tactics::gather_spell_board_target_cells(
			*Game, SpellCaster, ControlledPlayer, EffectKeyUtf8, BoardTargetFocusRange, Kind,
			BoardTargetPreviewSpellPayload, spell_string_payload_from_shape(BoardTargetPreviewSpellShapeKey), SpellProbe);
		for (const auto& [Wx, Wy] : gathered.enemy_cells) {
			BoardTargetEnemyCells.Add(FIntPoint(Wx, Wy));
		}
		for (const auto& [Wx, Wy] : gathered.other_cells) {
			BoardTargetOtherCells.Add(FIntPoint(Wx, Wy));
		}
		RebuildBoardTargetAoEPreview();
		return;
	}
	if (!BoardTargetPreviewKind.has_value()) {
		return;
	}
}

void UTacticsMatchSubsystem::RebuildTurnOrderRanks()
{
	TurnOrderRankByEntityId.Empty();
	if (!Game) {
		return;
	}
	TurnOrderRankByEntityId.Reserve(static_cast<int32>(Game->board.all_entities_map.size()));
	const std::unordered_map<std::string, int> ranks = tactics::compute_passive_action_order_ranks(Game->board);
	for (const auto& [entity_id, rank] : ranks) {
		TurnOrderRankByEntityId.Add(UTF8_TO_TCHAR(entity_id.c_str()), rank);
	}
}

void UTacticsMatchSubsystem::SetTurnOrderViewEnabled(const bool bEnabled)
{
	if (bTurnOrderViewEnabled == bEnabled) {
		return;
	}
	bTurnOrderViewEnabled = bEnabled;
	RebuildTurnOrderRanks();
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Units | ETacticsBoardVisualDirty::Highlights);
	OnBoardChanged.Broadcast();
}

int32 UTacticsMatchSubsystem::GetTurnOrderRankForEntityId(const FString& EntityId) const
{
	if (EntityId.IsEmpty()) {
		return 0;
	}
	const int32* Rank = TurnOrderRankByEntityId.Find(EntityId);
	return Rank ? *Rank : 0;
}

int32 UTacticsMatchSubsystem::GetTurnOrderRankAtWorld(const int WorldX, const int WorldY) const
{
	if (!Game || !bTurnOrderViewEnabled) {
		return 0;
	}
	const auto entity = Game->board.entity_at(WorldX, WorldY);
	if (!entity || !tactics::entity_participates_in_passive_action_order(*entity)) {
		return 0;
	}
	return GetTurnOrderRankForEntityId(UTF8_TO_TCHAR(entity->entity_id.c_str()));
}


void UTacticsMatchSubsystem::RequestBroadcastRefresh()
{
	if (bRefreshFlushScheduled) {
		return;
	}
	bRefreshFlushScheduled = true;
	if (!RefreshFlushTickerHandle.IsValid()) {
		RefreshFlushTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UTacticsMatchSubsystem::TickRefreshFlush), 0.0f);
	}
}

bool UTacticsMatchSubsystem::TickRefreshFlush(float /*DeltaTime*/)
{
	bRefreshFlushScheduled = false;
	if (RefreshFlushTickerHandle.IsValid()) {
		FTSTicker::GetCoreTicker().RemoveTicker(RefreshFlushTickerHandle);
		RefreshFlushTickerHandle.Reset();
	}
	BroadcastRefresh();
	return false;
}

void UTacticsMatchSubsystem::BroadcastRefresh()
{
	DrainAbilityDamagePopupEvents();
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Units | ETacticsBoardVisualDirty::Highlights);
	if (bBotAutoPlayEnabled) {
		EnsureBotAutoPlayTicker();
	}
	SyncControlledPlayerToActiveSeat();
	RebuildTurnOrderRanks();
	RebuildReachableMoveHighlights();
	RebuildPendingMoveFootprintHighlights();
	RebuildAttackTargetHighlights();
	RebuildBoardTargetHighlights();
	RebuildActionQueueHoverHighlights();
	RebuildDeployZoneHighlights();
	OnBoardChanged.Broadcast();
}

void UTacticsMatchSubsystem::SyncControlledPlayerToActiveSeat()
{
	if (!Game) {
		return;
	}
	if (!bAutoFollowActiveSeat) {
		if (FixedControlledPlayer && *FixedControlledPlayer > 0 && ControlledPlayer != *FixedControlledPlayer) {
			ControlledPlayer = *FixedControlledPlayer;
			ClearAbilityTargetPreviewArtifacts();
			ClearBoardTargetPreview();
			ClearDeployPreview();
		}
		return;
	}
	std::optional<int> Desired;
	{
		const auto Ph = Game->turn_manager.current_phase;
		if (Ph == tactics::TurnPhase::SpellWindow || Ph == tactics::TurnPhase::SecondSpellWindow
			|| Ph == tactics::TurnPhase::Defense || Ph == tactics::TurnPhase::BonusDefense)
		{
			// During reaction windows follow the reaction-window priority player,

			Desired = Game->reaction_window_priority_player();
		}
		else
		{
			Desired = Game->stack_manager.priority_player_id();
		}
	}
	if (!Desired) {
		Desired = Game->turn_manager.current_player();
	}
	if (!Desired || *Desired <= 0 || ControlledPlayer == *Desired) {
		return;
	}
	if (BotControlledSeats.Contains(*Desired)) {
		return;
	}
	ControlledPlayer = *Desired;
	ClearAbilityTargetPreviewArtifacts();
	ClearBoardTargetPreview();
	ClearDeployPreview();
}

void UTacticsMatchSubsystem::SkipEnergyUntilMainOrCap()
{
	if (!Game) {
		return;
	}
	for (int i = 0; i < 32 && Game->turn_manager.current_phase == tactics::TurnPhase::Energy; ++i) {
		const std::optional<int> Cp = Game->turn_manager.current_player();
		if (!Cp) {
			break;
		}
		const tactics::ActionResult R = Game->skip_energy_zone(*Cp);
		if (!R.ok) {
			break;
		}
	}
}

void UTacticsMatchSubsystem::EnsureProjectCatalogsLoaded()
{
	const auto ReadProjectFile = [](const std::string& RelPath, std::string& OutUtf8, std::string& Err) -> bool {
		return TacticsProjectContentReader::ReadUtf8File(RelPath, OutUtf8, Err);
	};
	tactics::ProjectContentLoadOptions Options;
	// Load ability + passive catalogs so validate_ability_ids / validate_passive_ids
	// inside load_project_card_catalogs see the full project catalog, not just builtins.
	// Cards that reference abilities/passives defined only in the JSON files (e.g. Lost Kingdom)
	// would otherwise fail per-card validation and their entire shard would be silently dropped.
	Options.load_status_catalog = false;
	Options.load_starter_deck = false;
	Options.load_test_deck = false;
	std::string Err;
	if (!tactics::load_all_project_content(ReadProjectFile, Err, Options)) {
		if (!Err.empty()) {
			UE_LOG(LogTemp, Warning, TEXT("EnsureProjectCatalogsLoaded: %s"), UTF8_TO_TCHAR(Err.c_str()));
		}
	}
}

TArray<TPair<FString, FString>> UTacticsMatchSubsystem::ListAvailableFactions() const
{
	const std::vector<std::string> SetCodes = tactics::list_playable_set_codes();
	TArray<TPair<FString, FString>> Result;
	Result.Reserve(static_cast<int32>(SetCodes.size()));
	for (const std::string& Code : SetCodes) {
		const FString Key = UTF8_TO_TCHAR(Code.c_str());
		const FString DisplayName = UTF8_TO_TCHAR(tactics::set_code_display_name(Code).c_str());
		Result.Add(TPair<FString, FString>{Key, DisplayName});
	}
	return Result;
}

void UTacticsMatchSubsystem::ResetDemoMatch()
{
	ResetMatchToPlayerCount(KDefaultDemoPlayerCount);
}

ETacticsMatchResult UTacticsMatchSubsystem::GetControlledMatchResult() const
{
	if (!Game || !tactics::bot::is_match_over(*Game)) {
		return ETacticsMatchResult::InProgress;
	}
	const std::optional<int> Winner = tactics::bot::winner_seat(*Game);
	if (!Winner) {
		return ETacticsMatchResult::Draw;
	}
	const int MyTeam = Game->team_of_seat(ControlledPlayer);
	const int WinTeam = Game->team_of_seat(*Winner);
	return (MyTeam == WinTeam) ? ETacticsMatchResult::Win : ETacticsMatchResult::Loss;
}

/** Starts a live match with the menu's map, objectives, and decks. */
void UTacticsMatchSubsystem::StartConfiguredMatch(bool bTeam2v2, bool bObjScanner, bool bObjOmni, bool bObjAether,
	const FString& DeckId, bool bGiveFieldRequisition)
{
	FTacticsMatchSetupProfile Profile;
	Profile.GameId = TEXT("unreal_gui");
	Profile.bSeedDemoState = false;  // configured matches start clean (players deploy from turn 1)
	Profile.bTeam2v2 = bTeam2v2;
	Profile.bObjScanner = bObjScanner;
	Profile.bObjOmni = bObjOmni;
	Profile.bObjAether = bObjAether;
	Profile.DeckId = DeckId;
	Profile.bGiveFieldRequisition = bGiveFieldRequisition;
	ResetMatchWithProfile(bTeam2v2 ? 4 : 2, Profile);
}

TArray<FString> UTacticsMatchSubsystem::GetAvailableDeckIds() const
{
	TArray<FString> Ids;
	TArray<FString> Files;
	const FString DecksDir = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("TacticsData/decks"));
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(DecksDir, TEXT("*.json")), true, false);
	for (const FString& File : Files) {
		Ids.Add(FPaths::GetBaseFilename(File));
	}
	Ids.Sort();
	return Ids;
}

FString UTacticsMatchSubsystem::GetActiveMatchDeckJson() const
{
	const std::optional<tactics::DeckListDefinition> Active = tactics::get_active_match_deck_list();
	if (!Active) {
		return FString();
	}
	std::string Out, Err;
	if (!tactics::save_deck_list_to_json_utf8(*Active, Out, Err)) {
		return FString();
	}
	return UTF8_TO_TCHAR(Out.c_str());
}

bool UTacticsMatchSubsystem::ApplyClientDeckToSeat(int32 Seat, const FString& DeckJson)
{
	if (!Game || Seat < 1 || DeckJson.IsEmpty()) {
		return false;
	}
	tactics::DeckListDefinition Dl;
	std::string Err;
	if (!tactics::load_deck_list_from_json_utf8(TCHAR_TO_UTF8(*DeckJson), Dl, Err)) {
		UE_LOG(LogTemp, Warning, TEXT("Client deck for seat P%d: %s"), Seat, UTF8_TO_TCHAR(Err.c_str()));
		return false;
	}
	if (!tactics::validate_deck_list(Dl, Err)) {
		UE_LOG(LogTemp, Warning, TEXT("Client deck for seat P%d illegal: %s"), Seat, UTF8_TO_TCHAR(Err.c_str()));
		return false;
	}
	Game->set_player_deck_from_list(Seat, Dl);
	UE_LOG(LogTemp, Log, TEXT("Applied joining client's deck to seat P%d"), Seat);
	return true;
}

void UTacticsMatchSubsystem::ResetSandboxMatch()
{
	ResetSandboxMatchWithFaction(TEXT("all"));
}

void UTacticsMatchSubsystem::ResetSandboxMatchWithFaction(const FString& FactionKey)
{
	FTacticsMatchSetupProfile Profile;
	Profile.GameId = TEXT("unreal_gui_sandbox");
	Profile.bSandboxMatch = true;
	Profile.bSeedDemoState = false;
	Profile.bSkipEnergyToMain = true;
	Profile.bAutoFollowActiveSeat = true;
	Profile.BoardWidth = tactics::kStandardBoardWidth;
	Profile.BoardHeight = tactics::kStandardBoardHeight;
	ResetMatchWithProfile(2, Profile);

	// Override every player's hand with the requested faction deck.
	const std::string FactionStd = TCHAR_TO_UTF8(*FactionKey);
	if (!FactionStd.empty() && FactionStd != "all") {
		if (Game) {
			std::string Err;
			if (tactics::apply_sandbox_faction_deck_to_all_players(*Game, FactionStd, &Err)) {
				const auto it = Game->players_decks.find(1);
				const int32 HandCount = (it != Game->players_decks.end())
					? static_cast<int32>(it->second.hand.size())
					: 0;
				UE_LOG(LogTemp, Log, TEXT("Sandbox hands: faction '%s' (%d cards per player)"),
					*FactionKey, HandCount);
			} else if (!Err.empty()) {
				UE_LOG(LogTemp, Warning, TEXT("Sandbox faction deck: %s"), UTF8_TO_TCHAR(Err.c_str()));
			}
		}
	}
}

void UTacticsMatchSubsystem::ResetMatchToPlayerCount(int32 PlayerCount)
{
	ResetMatchWithProfile(PlayerCount, FTacticsMatchSetupProfile{});
}

void UTacticsMatchSubsystem::ResetMatchWithProfile(int32 PlayerCount, const FTacticsMatchSetupProfile& Profile)
{
	MarkBoardVisualDirty(ETacticsBoardVisualDirty::Layout);
	tactics::clear_ability_catalog();
	tactics::clear_passive_catalog();
	{
		const auto ReadProjectFile = [](const std::string& RelPath, std::string& OutUtf8, std::string& Err) -> bool {
			return TacticsProjectContentReader::ReadUtf8File(RelPath, OutUtf8, Err);
		};
		tactics::ProjectContentLoadOptions Options;
		Options.load_starter_deck = false;
		Options.load_test_deck = false;
		std::string Err;
		if (!tactics::load_all_project_content(ReadProjectFile, Err, Options)) {
			UE_LOG(LogTemp, Warning, TEXT("TacticsData project content: %s"), UTF8_TO_TCHAR(Err.c_str()));
		}
	}

	const bool bSandbox = Profile.bSandboxMatch || Profile.GameId.Contains(TEXT("sandbox"), ESearchCase::IgnoreCase);
	const bool bTeam2v2 = Profile.bTeam2v2 && !bSandbox;

	// Pre-match deck selection: load decks/<DeckId>.json as the active match deck list BEFORE any
	// add_player (which reads the active list). Empty = keep whatever content-load selected.
	if (!bSandbox && !Profile.DeckId.IsEmpty()) {
		tactics::DeckListDefinition Dl;
		FString Rel;
		if (TryLoadDeckListById(Profile.DeckId, Dl, Rel)) {
			tactics::set_active_match_deck_list(std::move(Dl));
			UE_LOG(LogTemp, Log, TEXT("Match using deck '%s' (%s)"), *Profile.DeckId, *Rel);
		} else {
			UE_LOG(LogTemp, Warning, TEXT("Match deck '%s' not found under Content/TacticsData/decks"), *Profile.DeckId);
		}
	}

	int32 N = FMath::Clamp(PlayerCount, 1, 32);
	if (bTeam2v2) {
		N = 4;
	}
	if (bSandbox) {
		Game = MakeUnique<tactics::GameState>(
			TCHAR_TO_UTF8(*Profile.GameId), tactics::make_sandbox_map_layout());
		UE_LOG(LogTemp, Warning, TEXT("Tactics match reset: sandbox %dx%d (%d tiles, layout %s)"),
			Game->board_width(),
			Game->board_height(),
			Game->board.cell_count(),
			UTF8_TO_TCHAR(tactics::kDefaultBoardLayoutId));
	} else if (bTeam2v2) {
		Game = MakeUnique<tactics::GameState>(TCHAR_TO_UTF8(*Profile.GameId), tactics::make_2v2_map_layout());
		tactics::seed_2v2_map_objectives(*Game, Profile.bObjScanner, Profile.bObjOmni, Profile.bObjAether);
		UE_LOG(LogTemp, Warning, TEXT("Tactics match reset: 12x12 2v2 team map (%d tiles, layout %s) - four bases"),
			Game->board.cell_count(), UTF8_TO_TCHAR(tactics::k2v2BoardLayoutId));
	} else {
		// Include substring "footprint_test" in the id for footprint + deploy-keyword test units.
		// Include substring "test_deck" in the id for the three-card haste/surge/charge deck only.
		Game = MakeUnique<tactics::GameState>(TCHAR_TO_UTF8(*Profile.GameId), tactics::make_default_map_layout());
		Game->repair_standard_duel_board_geometry_if_needed();
		tactics::seed_standard_duel_objectives(*Game, Profile.bObjScanner, Profile.bObjOmni, Profile.bObjAether);
		UE_LOG(LogTemp, Warning, TEXT("Tactics match reset: 8x12 duel map (%d tiles, layout %s) - bases on TOP and BOTTOM"),
			Game->board.cell_count(), UTF8_TO_TCHAR(tactics::kDefaultBoardLayoutId));
	}
	for (int32 i = 1; i <= N; ++i) {
		Game->add_player(i, TCHAR_TO_UTF8(*FString::Printf(TEXT("P%d"), i)));
	}
	if (!bSandbox && !Profile.OpponentDeckId.IsEmpty() && N >= 2) {
		tactics::DeckListDefinition OppDl;
		FString OppRel;
		if (TryLoadDeckListById(Profile.OpponentDeckId, OppDl, OppRel)) {
			Game->set_player_deck_from_list(2, OppDl);
			UE_LOG(LogTemp, Log, TEXT("Opponent seat P2 using deck '%s' (%s)"), *Profile.OpponentDeckId, *OppRel);
		} else {
			UE_LOG(LogTemp, Warning, TEXT("Opponent deck '%s' not found"), *Profile.OpponentDeckId);
		}
	}
	// 2v2 teams: seats 1 & 3 (top) vs seats 2 & 4 (bottom).
	if (bTeam2v2) {
		Game->seat_team_id[1] = 1;
		Game->seat_team_id[3] = 1;
		Game->seat_team_id[2] = 2;
		Game->seat_team_id[4] = 2;
	}
	Game->set_field_requisition_enabled(Profile.bGiveFieldRequisition);
	Game->start_game();
	if (bSandbox) {
		tactics::master_cli_seed_sandbox_state(*Game);
	} else if (Profile.bSeedDemoState) {
		tactics::master_cli_seed_demo_state(*Game);
	}
	bAutoFollowActiveSeat = Profile.bAutoFollowActiveSeat;
	ControlledPlayer = 1;
	FixedControlledPlayer = bAutoFollowActiveSeat ? std::nullopt : std::optional<int>{ControlledPlayer};
	Selected.reset();
	BoardTargetPreviewKind.reset();
	BoardTargetEnemyCells.Empty();
	BoardTargetOtherCells.Empty();
	RemoteSeatUnitSelections.clear();
	LastAppliedNetworkSnapSeq = 0;
	LastAppliedCommandSeq = 0;
	ClearPendingCliWorldCell();
	if (Profile.bSkipEnergyToMain) {
		SkipEnergyUntilMainOrCap();
	}
	ResetOpponentPlayPresentationState(true);
	ResetBotRuntimeState();
	if (bBotAutoPlayEnabled) {
		EnsureBotAutoPlayTicker();
	}
	BroadcastRefresh();
}

int32 UTacticsMatchSubsystem::RegisterNetworkClientSeat(int32 RequestedSeat, const TSet<int32>* SeatsTakenByOtherRemotes)
{
	// Returns the assigned remote seat, or 0 when the match has no open seat. Seats are capped at the
	// match's configured player count (2 for a 1v1, 4 for a 2v2) - joining a full match must FAIL rather
	// than silently growing the match with extra seats, which is what this used to do.
	if (!Game) {
		return 0;
	}
	const int32 cur = GetMatchPlayerCount();
	if (cur < 2) {
		return 0;  // no remote seat exists in this match
	}

	TSet<int32> Taken;
	if (SeatsTakenByOtherRemotes) {
		Taken = *SeatsTakenByOtherRemotes;
	}

	// Honour an in-range explicit request first (the caller omits that seat from `Taken` so an explicit
	// request may reclaim/replace its own seat).
	const int32 Req = RequestedSeat < 2 ? 2 : RequestedSeat;
	if (Req <= cur && !Taken.Contains(Req)) {
		BroadcastRefresh();
		return Req;
	}
	// Otherwise the first free remote seat within the configured player count.
	for (int32 s = 2; s <= cur; ++s) {
		if (!Taken.Contains(s)) {
			BroadcastRefresh();
			return s;
		}
	}
	return 0;  // full
}

void UTacticsMatchSubsystem::SetControlledPlayer(int PlayerId)
{
	ControlledPlayer = PlayerId;
	if (!bAutoFollowActiveSeat) {
		FixedControlledPlayer = PlayerId;
	}
	BroadcastRefresh();
}

bool UTacticsMatchSubsystem::ShouldFlipBoardPresentation() const
{
	// Auto-follow tracks the acting seat (Play vs AI). Keep P1 orientation so the board does not
	// flip every turn. Network / locked seats use the local home seat instead.
	if (bAutoFollowActiveSeat) {
		return false;
	}
	return GetLocalViewingPlayerId() > 1;
}

void UTacticsMatchSubsystem::SetAutoFollowActiveSeat(bool bEnabled)
{
	if (bAutoFollowActiveSeat == bEnabled) {
		return;
	}
	bAutoFollowActiveSeat = bEnabled;
	FixedControlledPlayer = bAutoFollowActiveSeat ? std::nullopt : std::optional<int>{ControlledPlayer};
	BroadcastRefresh();
}

void UTacticsMatchSubsystem::SetPendingCliWorldCell(int Wx, int Wy)
{
	if (PendingCliWx == Wx && PendingCliWy == Wy) {
		return;
	}
	PendingCliWx = Wx;
	PendingCliWy = Wy;
	RebuildBoardTargetAoEPreview();
	OnTargetPreviewChanged.Broadcast();
}

void UTacticsMatchSubsystem::ClearPendingCliWorldCell()
{
	if (PendingCliWx < 0 || PendingCliWy < 0) {
		return;
	}
	PendingCliWx = -1;
	PendingCliWy = -1;
	RebuildBoardTargetAoEPreview();
	OnTargetPreviewChanged.Broadcast();
}

bool UTacticsMatchSubsystem::TryGetPendingCliWorldCell(int& OutWx, int& OutWy) const
{
	if (PendingCliWx < 0 || PendingCliWy < 0) {
		return false;
	}
	OutWx = PendingCliWx;
	OutWy = PendingCliWy;
	return true;
}

void UTacticsMatchSubsystem::SetUses3DBoardTiles(bool b)
{
	if (bUses3DBoardTiles == b) {
		return;
	}
	bUses3DBoardTiles = b;
	BroadcastRefresh();
}

bool UTacticsMatchSubsystem::IsMatchReady() const
{
	return Game != nullptr;
}

bool UTacticsMatchSubsystem::IsMainPhase() const
{
	return Game && Game->turn_manager.current_phase == tactics::TurnPhase::Main;
}

bool UTacticsMatchSubsystem::IsSecondMainPhase() const
{
	return Game && Game->turn_manager.current_phase == tactics::TurnPhase::SecondMain;
}

bool UTacticsMatchSubsystem::IsAnyMainPhase() const
{
	if (!Game) return false;
	const auto P = Game->turn_manager.current_phase;
	return P == tactics::TurnPhase::Main || P == tactics::TurnPhase::SecondMain;
}

bool UTacticsMatchSubsystem::IsEnergyPhase() const
{
	return Game && Game->turn_manager.current_phase == tactics::TurnPhase::Energy;
}


bool UTacticsMatchSubsystem::IsAnyAttackDeclarationPhase() const
{
	if (!Game) return false;
	const auto P = Game->turn_manager.current_phase;
	return P == tactics::TurnPhase::AttackDeclaration || P == tactics::TurnPhase::BonusAttackDeclaration;
}

bool UTacticsMatchSubsystem::IsAnyDefensePhase() const
{
	if (!Game) return false;
	const auto P = Game->turn_manager.current_phase;
	return P == tactics::TurnPhase::SpellWindow || P == tactics::TurnPhase::SecondSpellWindow
		|| P == tactics::TurnPhase::Defense || P == tactics::TurnPhase::BonusDefense;
}

bool UTacticsMatchSubsystem::IsSpellWindowPhase() const
{
	if (!Game) return false;
	const auto P = Game->turn_manager.current_phase;
	return P == tactics::TurnPhase::SpellWindow || P == tactics::TurnPhase::SecondSpellWindow;
}

bool UTacticsMatchSubsystem::IsAnyReactionWindowPhase() const
{
	if (!Game) return false;
	const auto P = Game->turn_manager.current_phase;
	return P == tactics::TurnPhase::SpellWindow || P == tactics::TurnPhase::SecondSpellWindow
		|| P == tactics::TurnPhase::Defense || P == tactics::TurnPhase::BonusDefense;
}

FString UTacticsMatchSubsystem::PhaseLabel() const
{
	if (!Game) return TEXT("No match");
	if (Game->IsAwaitingHandDiscard()) {
		return TEXT("Discard (end of turn)");
	}
	if (Game->IsAwaitingScan()) {
		return TEXT("Scan");
	}
	if (Game->IsAwaitingTerritoryLoot()) {
		return TEXT("Territory loot");
	}
	switch (Game->turn_manager.current_phase)
	{
		case tactics::TurnPhase::Energy:               return TEXT("Conquering Territories");
		case tactics::TurnPhase::Main:                 return TEXT("Main");
		case tactics::TurnPhase::SpellWindow:          return TEXT("Reaction Window");
		case tactics::TurnPhase::AttackDeclaration:    return TEXT("Attack Declaration");
		case tactics::TurnPhase::Defense:              return TEXT("Defense");
		case tactics::TurnPhase::SecondMain:           return TEXT("Second Main");
		case tactics::TurnPhase::SecondSpellWindow:    return TEXT("Second Reaction Window");
		case tactics::TurnPhase::BonusAttackDeclaration: return TEXT("Bonus Attack Declaration");
		case tactics::TurnPhase::BonusDefense:         return TEXT("Bonus Defense");
		default:                                        return TEXT("Main");
	}
}

FString UTacticsMatchSubsystem::GetPhaseBannerText() const
{
	if (!Game) {
		return TEXT("No match");
	}
	const FString Phase = PhaseLabel();
	// "Phase" suffix reads nicely for the short turn-phase names; reaction windows already read as phrases.
	const bool bNeedsPhaseWord = !Phase.Contains(TEXT("Phase")) && !Phase.Contains(TEXT("Window"))
		&& !Phase.Contains(TEXT("Discard")) && !Phase.Contains(TEXT("No match"));
	const FString PhaseText = bNeedsPhaseWord ? Phase + TEXT(" Phase") : Phase;

	// Acting seat: priority holder during reaction windows, otherwise the active turn player.
	const int32 Actor = IsAnyReactionWindowPhase() ? GetReactionWindowPriorityPlayerId() : GetActivePlayerId();
	if (Actor <= 0) {
		return PhaseText;
	}
	const int32 You = GetControlledPlayer();
	const FString Who = (Actor == You)
		? FString::Printf(TEXT("Player %d (You)"), Actor)
		: FString::Printf(TEXT("Player %d"), Actor);
	return FString::Printf(TEXT("%s  -  %s"), *PhaseText, *Who);
}

int32 UTacticsMatchSubsystem::GetActivePlayerId() const
{
	if (!Game) {
		return 0;
	}
	const auto Cp = Game->turn_manager.current_player();
	return Cp ? static_cast<int32>(*Cp) : 0;
}

int32 UTacticsMatchSubsystem::GetReactionWindowPriorityPlayerId() const
{
	if (!Game || !IsAnyReactionWindowPhase()) {
		return 0;
	}
	const auto P = Game->reaction_window_priority_player();
	return P ? static_cast<int32>(*P) : 0;
}

int32 UTacticsMatchSubsystem::GetStackPriorityPlayerId() const
{
	return GetReactionWindowPriorityPlayerId();
}

int32 UTacticsMatchSubsystem::GetPhaseActionQueueCount() const
{
	TArray<FTacticsActionQueueEntryUi> Entries;
	GetActionQueueUiEntries(Entries);
	return Entries.Num();
}

bool UTacticsMatchSubsystem::CanControlledPlayerActInMainPhase() const
{
	// True during any phase where the active player can freely deploy, move, and attack.
	if (!Game || (!IsAnyMainPhase() && !IsAnyAttackDeclarationPhase())) {
		return false;
	}
	const auto Cp = Game->turn_manager.current_player();
	return Cp && *Cp == ControlledPlayer;
}

bool UTacticsMatchSubsystem::CanControlledPlayerPassPriority() const
{
	if (!Game || ControlledPlayer <= 0) {
		return false;
	}
	return Game->can_pass_priority(ControlledPlayer);
}

bool UTacticsMatchSubsystem::CanControlledPlayerEndTurn() const
{
	if (!Game || ControlledPlayer <= 0) {
		return false;
	}
	if (Game->IsAwaitingHandDiscard()) {
		return false;
	}
	if (Game->IsAwaitingScan()) {
		return false;
	}
	if (Game->IsAwaitingTerritoryLoot()) {
		return false;
	}
	if (GetPendingEnergyZoneChoiceCount() > 0) {
		return false;
	}
	// Valid in either main phase (Main or SecondMain) - sends end_main.
	if (!IsAnyMainPhase()) {
		return false;
	}
	const auto Cp = Game->turn_manager.current_player();
	if (!Cp || *Cp != ControlledPlayer) {
		return false;
	}
	if (Game->has_pending_move_for(ControlledPlayer)) {
		return false;
	}
	return true;
}

bool UTacticsMatchSubsystem::CanControlledPlayerCommitAttacks() const
{
	if (!Game || ControlledPlayer <= 0 || !IsAnyAttackDeclarationPhase()) {
		return false;
	}
	const auto Cp = Game->turn_manager.current_player();
	if (!Cp || *Cp != ControlledPlayer) {
		return false;
	}
	if (Game->has_pending_move_for(ControlledPlayer)) {
		return false;
	}
	return true;
}

bool UTacticsMatchSubsystem::IsSelectedUnitControlled() const
{
	return Selected && Selected->owner && *Selected->owner == ControlledPlayer;
}

bool UTacticsMatchSubsystem::IsSelectedUnitSilenced() const
{
	return Selected && tactics::entity_is_silenced(*Selected);
}

bool UTacticsMatchSubsystem::IsSelectedUnitJammed() const
{
	return Selected && tactics::entity_is_jammed(*Selected);
}

FString UTacticsMatchSubsystem::FormatSelectedUnitStats() const
{
	if (!Selected) {
		return FString();
	}
	const tactics::Unit& U = *Selected;
	const tactics::Entity& E = U;
	const bool bBuilding = E.entity_type == "building";
	const bool bBase = E.entity_type == "base";
	const int EffectiveHealth = tactics::entity_effective_base_health(E);
	FString Out;
	Out += FString::Printf(TEXT("%s (%s%s)\n"),
		UTF8_TO_TCHAR(E.entity_id.c_str()),
		bBase ? TEXT("Base: ") : (bBuilding ? TEXT("Building: ") : TEXT("")),
		UTF8_TO_TCHAR(U.unit_type.c_str()));
	if (bBase) {
		Out += TEXT("Immune to all effects (combat damage still applies).\n");
	}
	const int BonusHealth = tactics::bonus_health_value(E);
	Out += BonusHealth > 0
		? FString::Printf(TEXT("HP: %d / %d (+%d bonus)\n"), E.current_health, EffectiveHealth, BonusHealth)
		: FString::Printf(TEXT("HP: %d / %d\n"), E.current_health, EffectiveHealth);
	const tactics::DamageRange MeleeRange = tactics::unit_effective_melee_damage_range(U);
	const tactics::DamageRange RangedRange = tactics::unit_effective_ranged_damage_range(U);
	const auto FormatDamageRange = [](const tactics::DamageRange& Range) -> FString {
		if (Range.max <= 0 && Range.min <= 0) {
			return TEXT("0");
		}
		if (Range.min == Range.max) {
			return FString::FromInt(Range.min);
		}
		return FString::Printf(TEXT("%d-%d"), Range.min, Range.max);
	};
	Out += FString::Printf(TEXT("Damage ? melee %s (reach %d) | ranged %s (reach %d, deadzone %d) | crit %d%%\n"),
		*FormatDamageRange(MeleeRange), U.melee_range, *FormatDamageRange(RangedRange), U.ranged_range, U.ranged_deadzone,
		U.crit_chance_percent);
	Out += bBuilding
		? FString::Printf(TEXT("Movement: immobile | Attack mode: %s\n"), AttackTypeToUi(E.attack_type))
		: FString::Printf(TEXT("Movement: %d | Attack mode: %s\n"), tactics::unit_effective_movement(U), AttackTypeToUi(E.attack_type));
	std::vector<std::string> DisplayAttributes = E.keywords;
	for (const std::string& Attr : E.aura_granted_keywords) {
		if (std::find(DisplayAttributes.begin(), DisplayAttributes.end(), Attr) == DisplayAttributes.end()) {
			DisplayAttributes.push_back(Attr);
		}
	}
	for (const tactics::TemporaryEntityEffect& Effect : E.temporary_effects) {
		for (const tactics::PassiveAttributeGrant& Grant : Effect.granted_attributes) {
			if (std::find(DisplayAttributes.begin(), DisplayAttributes.end(), Grant.key) == DisplayAttributes.end()) {
				DisplayAttributes.push_back(Grant.key);
			}
		}
	}
	if (tactics::entity_has_defend_stance(E)) {
		Out += TEXT("Stance: Defending (+1 armor until your next turn, cap 5; cannot use abilities)\n");
	}
	if (tactics::entity_has_dash_movement(E)) {
		Out += TEXT("Stance: Dashed (+1 movement this turn; burst)\n");
	}
	if (tactics::entity_has_recover_stance(E)) {
		Out += TEXT("Stance: Recovering (heal 2 at turn start if you take no damage)\n");
	}
	if (tactics::entity_has_deployment_fatigue(E)) {
		if (tactics::entity_has_attribute(E, "haste")) {
			Out += TEXT("Stance: Deployment fatigue - Haste (can move; no attack/abilities/actions)\n");
		} else if (tactics::entity_has_attribute(E, "surge")) {
			Out += TEXT("Stance: Deployment fatigue - Surge (can attack/abilities/actions; no move)\n");
		} else {
			Out += TEXT("Stance: Deployment fatigue (melee attacks only; bonus moves allow move)\n");
		}
	}
	if (tactics::entity_is_silenced(E)) {
		Out += TEXT("Keywords: (suppressed ? Silenced)\n");
	} else if (bBase) {
		const std::string TermBody = tactics::term_glossary_body(tactics::kPlayerBaseInnateGlossaryTerm, true);
		Out += TEXT("Keywords: Base Turret\n");
		if (!TermBody.empty()) {
			Out += FString::Printf(TEXT("  Base Turret: %s\n"), UTF8_TO_TCHAR(TermBody.c_str()));
		}
		std::vector<std::string> ExtraKeywords;
		for (const std::string& Attr : DisplayAttributes) {
			if (!tactics::is_player_base_innate_keyword(Attr)) {
				ExtraKeywords.push_back(Attr);
			}
		}
		if (!ExtraKeywords.empty()) {
			Out += FString::Printf(TEXT("  Also: %s\n"),
				UTF8_TO_TCHAR(tactics::format_attribute_names(ExtraKeywords).c_str()));
			for (const std::string& Attr : ExtraKeywords) {
				const std::string Rules = tactics::attribute_rules_text(Attr);
				if (!Rules.empty()) {
					const int Amount = tactics::entity_attribute_amount(E, Attr, -1);
					const FString Label = Amount >= 0
						? FString::Printf(TEXT("%s %d"), UTF8_TO_TCHAR(tactics::attribute_display_name(Attr).c_str()), Amount)
						: FString(UTF8_TO_TCHAR(tactics::attribute_display_name(Attr).c_str()));
					Out += FString::Printf(TEXT("  %s: %s\n"), *Label, UTF8_TO_TCHAR(Rules.c_str()));
				}
			}
		}
	} else if (!DisplayAttributes.empty()) {
		Out += FString::Printf(TEXT("Keywords: %s\n"), UTF8_TO_TCHAR(tactics::format_attribute_names(DisplayAttributes).c_str()));
		for (const std::string& Attr : DisplayAttributes) {
			const std::string Rules = tactics::attribute_rules_text(Attr);
			if (!Rules.empty()) {
				const int Amount = tactics::entity_attribute_amount(E, Attr, -1);
				const FString Label = Amount >= 0
					? FString::Printf(TEXT("%s %d"), UTF8_TO_TCHAR(tactics::attribute_display_name(Attr).c_str()), Amount)
					: FString(UTF8_TO_TCHAR(tactics::attribute_display_name(Attr).c_str()));
				Out += FString::Printf(TEXT("  %s: %s\n"),
					*Label,
					UTF8_TO_TCHAR(Rules.c_str()));
			}
		}
	}
	if (!E.entity_effects.empty()) {
		Out += TEXT("Effects:\n");
		for (const tactics::EntityEffectInstance& Effect : E.entity_effects) {
			if (Effect.amount > 0) {
				Out += FString::Printf(TEXT("  %s %d\n"), UTF8_TO_TCHAR(Effect.key.c_str()), Effect.amount);
			}
		}
	}
	Out += FString::Printf(TEXT("This turn ? moves left: %d | attacks left: %d\n"),
		E.moves_remaining_this_turn, E.attacks_remaining_this_turn);
	if (tactics::entity_is_silenced(E)) {
		Out += TEXT("Passive abilities: (suppressed ? Silenced)\n");
	} else if (!E.passive_abilities.empty()) {
		Out += TEXT("Passive abilities:\n");
		for (const tactics::PassiveAbilitySpec& Passive : E.passive_abilities) {
			Out += FString::Printf(TEXT("  [%s] %s"),
				UTF8_TO_TCHAR(Passive.key.c_str()),
				UTF8_TO_TCHAR(Passive.name.empty() ? Passive.key.c_str() : Passive.name.c_str()));
			if (!Passive.rules_text.empty()) {
				Out += FString::Printf(TEXT(": %s"), UTF8_TO_TCHAR(Passive.rules_text.c_str()));
			}
			Out += TEXT("\n");
		}
	}
	if (!E.temporary_effects.empty()) {
		Out += TEXT("Temporary effects:\n");
		for (const tactics::TemporaryEntityEffect& Effect : E.temporary_effects) {
			Out += FString::Printf(TEXT("  [%s] %s (%s %d)"),
				UTF8_TO_TCHAR(Effect.effect_id.c_str()),
				UTF8_TO_TCHAR(Effect.name.empty() ? Effect.effect_id.c_str() : Effect.name.c_str()),
				UTF8_TO_TCHAR(Effect.expire_on.c_str()),
				Effect.remaining_turns);
			if (!Effect.rules_text.empty()) {
				Out += FString::Printf(TEXT(": %s"), UTF8_TO_TCHAR(Effect.rules_text.c_str()));
			}
			Out += TEXT("\n");
		}
	}
	if (tactics::entity_is_jammed(E)) {
		const int JamStacks = tactics::entity_effect_amount(E, "jammed");
		Out += FString::Printf(TEXT("Activated abilities: (blocked - Jammed %d; loses 1 stack at end of your turn)\n"), JamStacks);
	} else if (!U.activated_abilities.empty()) {
		Out += TEXT("Abilities (stack + speeds like spells):\n");
		for (size_t i = 0; i < U.activated_abilities.size(); ++i) {
			const tactics::AbilitySpec& A = U.activated_abilities[i];
			const int32 GameUsesMax = static_cast<int32>(tactics::ability_effective_uses_per_game(A));
			const int32 TurnUsesMax = static_cast<int32>(tactics::ability_effective_uses_per_turn(A));
			const int32 UsesMax = GameUsesMax > 0 ? GameUsesMax : TurnUsesMax;
			const int32 UsesRemaining = UsesMax > 0
				? (GameUsesMax > 0
					? static_cast<int32>(tactics::entity_ability_uses_remaining_game(E, A))
					: static_cast<int32>(tactics::entity_ability_uses_remaining(E, A)))
				: -1;
			const bool Used = !tactics::entity_can_use_ability(E, A);
			FString Cost;
			for (const auto& Pr : A.energy_cost) {
				if (!Cost.IsEmpty()) {
					Cost += TEXT(" ");
				}
				Cost += FString::Printf(TEXT("%s:%d"), UTF8_TO_TCHAR(tactics::to_string(Pr.first).c_str()), Pr.second);
			}
			if (Cost.IsEmpty()) {
				Cost = TEXT("0");
			}
			const TCHAR* Sp = TEXT("channeled");
			if (A.speed == tactics::EffectSpeed::Reflex) {
				Sp = TEXT("reflex");
			} else if (A.speed == tactics::EffectSpeed::Blazing) {
				Sp = TEXT("blazing");
			}
			FString UsesSuffix;
			if (UsesMax > 0) {
				UsesSuffix = GameUsesMax > 0
					? FString::Printf(TEXT(" match uses %d/%d"), UsesRemaining, UsesMax)
					: FString::Printf(TEXT(" uses %d/%d"), UsesRemaining, UsesMax);
			}
			Out += FString::Printf(TEXT("  %d. [%s] %s (%s) cost %s%s\n"),
				static_cast<int>(i) + 1,
				UTF8_TO_TCHAR(A.key.c_str()),
				UTF8_TO_TCHAR(A.name.c_str()),
				Sp,
				*Cost,
				Used ? TEXT(" ? no uses left") : *UsesSuffix);
		}
		Out += TEXT("CLI: ability <key> [col row] when a unit is selected.\n");
	}
	return Out;
}


bool UTacticsMatchSubsystem::TryBuildUnitHoverAtWorld(const int WorldX, const int WorldY, FTacticsUnitHoverPresentation& Out) const
{
	Out = {};
	if (!Game) {
		return false;
	}
	const std::shared_ptr<tactics::Unit> U = DisplayUnitAtWorld(*Game, WorldX, WorldY);
	if (!U) {
		return false;
	}
	const tactics::Entity& E = *U;
	const int EffectiveHealth = tactics::entity_effective_base_health(E);
	const int32 OwnerSeat = E.owner ? *E.owner : 0;
	Out.bHasUnit = true;
	Out.WorldX = WorldX;
	Out.WorldY = WorldY;
	Out.HeaderLine = FString::Printf(TEXT("%s - %d/%d HP"),
		UTF8_TO_TCHAR(U->unit_type.c_str()),
		E.current_health,
		EffectiveHealth);
	if (OwnerSeat > 0) {
		Out.OwnerLine = OwnerSeat == ControlledPlayer
			? FString::Printf(TEXT("Owner: Player %d (You)"), OwnerSeat)
			: FString::Printf(TEXT("Owner: Player %d"), OwnerSeat);
	}
	AppendActiveEffectRowsFromEntity(E, bShowAdvancedCardText, Out.Effects);
	if (tactics::entity_has_attribute(E, "taunt")) {
		const std::string TauntBody = tactics::keyword_glossary_body("taunt", bShowAdvancedCardText);
		if (!TauntBody.empty()) {
			FTacticsActiveEffectEntry TauntRow;
			TauntRow.Key = TEXT("kw:taunt");
			TauntRow.Name = TEXT("Taunt");
			TauntRow.Body = UTF8_TO_TCHAR(TauntBody.c_str());
			Out.Effects.Add(std::move(TauntRow));
		}
	}
	return true;
}

bool UTacticsMatchSubsystem::TryBuildBoardHoverAtWorld(const int WorldX, const int WorldY,
	FTacticsUnitHoverPresentation& OutUnit, FString& OutCellHint) const
{
	OutUnit = {};
	OutCellHint.Empty();
	if (!Game) {
		return false;
	}
	const FTacticsBoardCellPresentation Cell = GetCellPresentationAtWorld(WorldX, WorldY);
	const bool bCanAct = CanControlledPlayerActInMainPhase();
	const bool bHasSelection = HasUnitSelected();
	const bool bPendingMove = HasPendingMoveForControlledPlayer();

	if (Cell.bPendingMoveDestination) {
		OutCellHint = TEXT("Click to adjust move preview · Enter confirms");
	} else if (Cell.bPendingMoveOrigin) {
		OutCellHint = TEXT("Previous footprint");
	} else if (IsDeployValidCellAtWorld(WorldX, WorldY)) {
		OutCellHint = TEXT("Click to deploy");
	} else if (Cell.bAttackTarget || Cell.bBoardTargetEnemy) {
		OutCellHint = bPendingMove ? TEXT("Cancel pending move to attack") : TEXT("Click / RMB to queue attack");
	} else if (Cell.bBoardTargetAoE) {
		OutCellHint = IsAbilityBoardTargetPreviewActive() ? TEXT("Ability blast preview") : TEXT("Directional blast preview");
	} else if (Cell.bAbilityBoardTargetEnemy) {
		OutCellHint = TEXT("Click to target with ability");
	} else if (Cell.bAbilityBoardTarget) {
		OutCellHint = TEXT("Click to target with ability");
	} else if (Cell.bBoardTargetOther) {
		OutCellHint = TEXT("Click to target");
	} else if (Cell.bReachableMove && bHasSelection && bCanAct) {
		OutCellHint = TEXT("Click to preview move · Shift+click keeps unit selected");
	}

	const bool bHasUnit = TryBuildUnitHoverAtWorld(WorldX, WorldY, OutUnit);
	if (bHasUnit) {
		OutUnit.ActionHint = OutCellHint;
		return true;
	}
	return !OutCellHint.IsEmpty();
}

int32 UTacticsMatchSubsystem::GetSelectedUnitActivatedAbilityCount() const
{
	if (!Selected) {
		return 0;
	}
	return static_cast<int32>(Selected->activated_abilities.size());
}

bool UTacticsMatchSubsystem::TryGetSelectedUnitActivatedAbilityUi(int32 Index1Based, FString& OutKey, FString& OutLabel, FString& OutSpeedTag,
	FString& OutCostLine, bool& bUsedThisTurn, bool& bNeedsBoardTarget, FString& OutRangeToken, int32& OutUsesRemaining,
	int32& OutUsesMax) const
{
	if (!Selected || Index1Based < 1) {
		return false;
	}
	const size_t Idx = static_cast<size_t>(Index1Based - 1);
	if (Idx >= Selected->activated_abilities.size()) {
		return false;
	}
	const tactics::AbilitySpec& A = Selected->activated_abilities[Idx];
	const tactics::Entity& E = *Selected;
	OutKey = UTF8_TO_TCHAR(A.key.c_str());
	OutLabel = UTF8_TO_TCHAR(A.name.empty() ? A.key.c_str() : A.name.c_str());
	if (A.speed == tactics::EffectSpeed::Reflex) {
		OutSpeedTag = TEXT("reflex");
	} else if (A.speed == tactics::EffectSpeed::Blazing) {
		OutSpeedTag = TEXT("blazing");
	} else {
		OutSpeedTag = TEXT("channeled");
	}
	// Brace tokens so the UI renders the cost as inline energy icons (see TacticsCardText).
	OutCostLine = AbilityCostBraceTokens(A);
	if (OutCostLine.IsEmpty()) {
		OutCostLine = TEXT("0");
	}
	const int32 GameUsesMax = static_cast<int32>(tactics::ability_effective_uses_per_game(A));
	const int32 TurnUsesMax = static_cast<int32>(tactics::ability_effective_uses_per_turn(A));
	OutUsesMax = GameUsesMax > 0 ? GameUsesMax : TurnUsesMax;
	OutUsesRemaining = OutUsesMax > 0
		? (GameUsesMax > 0
			? static_cast<int32>(tactics::entity_ability_uses_remaining_game(E, A))
			: static_cast<int32>(tactics::entity_ability_uses_remaining(E, A)))
		: -1;
	bUsedThisTurn = !tactics::entity_can_use_ability(E, A);
	bNeedsBoardTarget = tactics::ability_requires_board_target(A)
		|| tactics::effect_key_targets_empty_cell(A.effect_key);
	OutRangeToken = AbilityCatalogRangeToken(A, Game.Get(), std::dynamic_pointer_cast<tactics::Unit>(Selected), ControlledPlayer);
	return true;
}

bool UTacticsMatchSubsystem::TryGetAbilityDescriptionForKey(const FString& AbilityKey, FString& OutDescription,
	const bool bIncludeTargetingHints) const
{
	OutDescription.Reset();
	if (AbilityKey.IsEmpty()) {
		return false;
	}
	const std::string KeyUtf8 = TCHAR_TO_UTF8(*AbilityKey);
	tactics::AbilitySpec Spec;
	bool bFound = false;
	if (Selected) {
		for (const tactics::AbilitySpec& OnUnit : Selected->activated_abilities) {
			if (OnUnit.key == KeyUtf8) {
				Spec = OnUnit;
				bFound = true;
				break;
			}
		}
	}
	if (!bFound && !tactics::try_get_ability_from_catalog(KeyUtf8, Spec)) {
		return false;
	}
	FString Body = UTF8_TO_TCHAR(Spec.name.empty() ? Spec.key.c_str() : Spec.name.c_str());
	const std::string chosen_desc = CardTextForDisplay(Spec.normal_description, Spec.description, bShowAdvancedCardText);
	if (!chosen_desc.empty()) {
		Body += TEXT("\n\n");
		Body += StripRedundantTargetingFromText(UTF8_TO_TCHAR(chosen_desc.c_str()), AbilityCatalogRangeToken(Spec));
	} else {
		// Fall back to rules_text on the effect definition
		tactics::EffectDefinition EffectDef;
		if (tactics::try_get_effect_definition(Spec.effect_key, EffectDef) && !EffectDef.rules_text.empty()) {
			Body += TEXT("\n\n");
			Body += UTF8_TO_TCHAR(EffectDef.rules_text.c_str());
		}
	}
	if (bIncludeTargetingHints) {
		if (tactics::effect_uses_directional_aim(Spec.effect_key)) {
			if (tactics::effect_key_is_movement_landing(Spec.effect_key)) {
				Body += TEXT("\n\n► Click a highlighted landing cell.");
			} else {
				Body += TEXT("\n\n► Click a direction cell to aim.");
			}
		} else if (tactics::effect_key_targets_empty_cell(Spec.effect_key)) {
			Body += TEXT("\n\n► Click a highlighted empty cell.");
		} else if (tactics::ability_requires_board_target(Spec)) {
			Body += TEXT("\n\n► Click a highlighted cell to target.");
		}

	}
	OutDescription = Body;
	return true;
}

int32 UTacticsMatchSubsystem::GetSelectedUnitPassiveCount() const
{
	if (!Selected) {
		return 0;
	}
	return static_cast<int32>(Selected->passive_abilities.size());
}

void UTacticsMatchSubsystem::SetShowAdvancedCardText(bool bAdvanced)
{
	if (bShowAdvancedCardText == bAdvanced) {
		return;
	}
	bShowAdvancedCardText = bAdvanced;
	BroadcastRefresh();
}

bool UTacticsMatchSubsystem::GetAllowDeploymentUndo() const
{
	return Game ? Game->allow_deployment_undo() : false;
}

void UTacticsMatchSubsystem::SetAllowDeploymentUndo(const bool bAllow)
{
	if (!Game) {
		return;
	}
	const FString Line = FString::Printf(TEXT("match_setting allow_deployment_undo %d"), bAllow ? 1 : 0);
	FString Err;
	(void)ExecMasterCliLine(Line, Err);
}

TArray<int32> UTacticsMatchSubsystem::GetMatchPlayerSeats() const
{
	TArray<int32> Seats;
	if (!Game) {
		return Seats;
	}
	for (const auto& [SeatId, Deck] : Game->players_decks) {
		Seats.Add(SeatId);
	}
	Seats.Sort();
	return Seats;
}

int32 UTacticsMatchSubsystem::GetTeamForSeat(const int32 Seat) const
{
	return Game ? Game->team_of_seat(Seat) : Seat;
}

void UTacticsMatchSubsystem::SetTeamForSeat(const int32 Seat, const int32 TeamId)
{
	if (!Game) {
		return;
	}
	const FString Line = FString::Printf(TEXT("team %d %d"), Seat, TeamId);
	FString Err;
	(void)ExecMasterCliLine(Line, Err);
	BroadcastRefresh();
}

void UTacticsMatchSubsystem::GetControlledOpenAttackUndeclareOptions(TArray<FString>& OutEntityIds, TArray<FString>& OutLabels) const
{
	OutEntityIds.Reset();
	OutLabels.Reset();
	if (!Game) {
		return;
	}
	for (const auto& entry : Game->attack_phase_queue()) {
		if (!entry.is_attack || entry.attack.attacker_id.empty()) {
			continue;
		}
		const auto ItOwner = Game->board.all_entities_map.find(entry.attack.attacker_id);
		if (ItOwner == Game->board.all_entities_map.end() || !ItOwner->second || !ItOwner->second->owner
			|| *ItOwner->second->owner != ControlledPlayer) {
			continue;
		}
		const FString EntityId = UTF8_TO_TCHAR(entry.attack.attacker_id.c_str());
		FString Label = EntityId;
		if (const auto It = Game->board.all_entities_map.find(entry.attack.attacker_id); It != Game->board.all_entities_map.end() && It->second) {
			if (const auto* AsUnit = dynamic_cast<const tactics::Unit*>(It->second.get())) {
				if (!AsUnit->unit_type.empty()) {
					Label = UTF8_TO_TCHAR(AsUnit->unit_type.c_str());
				}
			}
		}
		OutEntityIds.Add(EntityId);
		OutLabels.Add(FString::Printf(TEXT("Undeclare %s"), *Label));
	}
}

FString UTacticsMatchSubsystem::FormatTeamAssignmentSummary() const
{
	if (!Game || Game->seat_team_id.empty()) {
		return FString();
	}
	bool bNonDefault = false;
	for (const auto& [seat, team] : Game->seat_team_id) {
		if (team != seat) {
			bNonDefault = true;
			break;
		}
	}
	if (!bNonDefault) {
		return FString();
	}
	TArray<FString> Parts;
	for (const auto& [seat, team] : Game->seat_team_id) {
		Parts.Add(FString::Printf(TEXT("P%d→T%d"), seat, team));
	}
	Parts.Sort();
	return FString::Printf(TEXT("Teams: %s"), *FString::Join(Parts, TEXT(", ")));
}

bool UTacticsMatchSubsystem::TryGetSelectedUnitPassiveUi(int32 Index1Based, FString& OutName, FString& OutRulesText, FString& OutAppliesTo) const
{
	if (!Selected || Index1Based < 1) {
		return false;
	}
	const size_t Idx = static_cast<size_t>(Index1Based - 1);
	if (Idx >= Selected->passive_abilities.size()) {
		return false;
	}
	const tactics::PassiveAbilitySpec& P = Selected->passive_abilities[Idx];
	OutName = UTF8_TO_TCHAR(P.name.empty() ? P.key.c_str() : P.name.c_str());
	const std::string chosen_rules = CardTextForDisplay(P.normal_rules_text, P.rules_text, bShowAdvancedCardText);
	OutRulesText = UTF8_TO_TCHAR(chosen_rules.c_str());
	OutAppliesTo = UTF8_TO_TCHAR(P.applies_to.c_str());
	return true;
}

bool UTacticsMatchSubsystem::GetMergedBounds(int& OutMinX, int& OutMinY, int& OutSpanX, int& OutSpanY) const
{
	if (!Game) {
		return false;
	}
	const tactics::BoardCellBounds B = MergedBounds(*Game);
	if (B.empty()) {
		return false;
	}
	OutMinX = B.min_x;
	OutMinY = B.min_y;
	OutSpanX = B.span_x();
	OutSpanY = B.span_y();
	return true;
}

FString UTacticsMatchSubsystem::GetBoardMapDebugLine() const
{
	if (!Game) {
		return TEXT("Map: (no match)");
	}
	const int32 Tiles = Game->board.cell_count();
	const FString LayoutId = UTF8_TO_TCHAR(Game->board_layout().layout_id.c_str());
	FString Line = FString::Printf(
		TEXT("Map: %dx%d, %d tiles, layout %s"),
		Game->board_width(),
		Game->board_height(),
		Tiles,
		*LayoutId);
	if (Game->board_width() == tactics::kStandardBoardWidth && Game->board_height() == tactics::kStandardBoardHeight &&
		Tiles != 80) {
		Line += TEXT(" ? WRONG (expect 80 jigsaw tiles). Close editor, full rebuild, Reset Demo Match.");
	}
	return Line;
}

/** Snapshot of every unit/base the 3D board and 2D overlay need to draw. */
void UTacticsMatchSubsystem::GatherBoardUnitPoses(TArray<FTacticsBoardUnitPose>& OutPoses) const
{
	OutPoses.Reset();
	if (!Game) {
		return;
	}
	const std::vector<std::shared_ptr<tactics::Entity>> ents = Game->board.all_entities();
	for (const std::shared_ptr<tactics::Entity>& entPtr : ents) {
		if (!entPtr) {
			continue;
		}
		const auto u = std::dynamic_pointer_cast<tactics::Unit>(entPtr);
		if (!u) {
			continue;
		}
		std::optional<tactics::PendingMoveSelection> Pending;
		if (u->owner) {
			Pending = Game->get_pending_move_for(*u->owner);
			if (Pending && Pending->unit_entity_id != u->entity_id) {
				Pending.reset();
			}
		}
		std::vector<std::pair<int, int>> footprint = tactics::entity_shape_offsets(*u);
		if (Pending && Pending->quarter_turns_cw != 0) {
			tactics::rotate_shape_offsets_n_quarters_cw(footprint, Pending->quarter_turns_cw);
		}
		double sx = 0.0;
		double sy = 0.0;
		int n = 0;
		if (Pending) {
			for (const auto& [dx, dy] : footprint) {
				sx += static_cast<double>(Pending->resolved_ax + dx);
				sy += static_cast<double>(Pending->resolved_ay + dy);
				++n;
			}
		} else if (!u->occupied_positions.empty()) {
			for (const auto& [x, y] : u->occupied_positions) {
				sx += static_cast<double>(x);
				sy += static_cast<double>(y);
				++n;
			}
		} else if (u->position) {
			const auto [ax, ay] = *u->position;
			for (const auto& [dx, dy] : tactics::entity_shape_offsets(*u)) {
				sx += static_cast<double>(ax + dx);
				sy += static_cast<double>(ay + dy);
				++n;
			}
		}
		if (n < 1) {
			continue;
		}
		FTacticsBoardUnitPose p;
		p.EntityId = UTF8_TO_TCHAR(u->entity_id.c_str());
		if (p.EntityId.IsEmpty()) {
			continue;
		}
		p.ArtId = ResolveArtIdForEntity(*Game, *u);
		p.OwnerPlayerId = u->owner ? *u->owner : 0;
		p.GridCenterX = static_cast<float>(sx / static_cast<double>(n));
		p.GridCenterY = static_cast<float>(sy / static_cast<double>(n));
		if (footprint.empty() && !u->occupied_positions.empty()) {
			p.FootprintCellCount = FMath::Max(1, static_cast<int32>(u->occupied_positions.size()));
			p.FootprintSpanX = p.FootprintCellCount;
			p.FootprintSpanY = 1;
		} else {
			int32 min_dx = 0;
			int32 max_dx = 0;
			int32 min_dy = 0;
			int32 max_dy = 0;
			bool first = true;
			for (const auto& [dx, dy] : footprint) {
				if (first) {
					min_dx = max_dx = dx;
					min_dy = max_dy = dy;
					first = false;
				} else {
					min_dx = FMath::Min(min_dx, dx);
					max_dx = FMath::Max(max_dx, dx);
					min_dy = FMath::Min(min_dy, dy);
					max_dy = FMath::Max(max_dy, dy);
				}
			}
			p.FootprintSpanX = first ? 1 : (max_dx - min_dx + 1);
			p.FootprintSpanY = first ? 1 : (max_dy - min_dy + 1);
			p.FootprintCellCount = FMath::Max(1, static_cast<int32>(footprint.size()));
		}
		p.bIsBase = tactics::entity_is_base(*entPtr);
		if (!p.bIsBase) {
			p.bHasTaunt = tactics::entity_has_attribute(*entPtr, "taunt");
			if (const int32* Rank = TurnOrderRankByEntityId.Find(p.EntityId)) {
				p.TurnOrderRank = *Rank;
			}
		}
		if (!AbilityCastFlashEntityId.IsEmpty() && p.EntityId == AbilityCastFlashEntityId) {
			constexpr double kFlashDurationSec = 0.45;
			const double Elapsed = FPlatformTime::Seconds() - AbilityCastFlashStartTime;
			if (Elapsed >= 0.0 && Elapsed < kFlashDurationSec) {
				p.AbilityCastFlashAlpha = 1.f - static_cast<float>(Elapsed / kFlashDurationSec);
				p.bAbilityCastFlashSuccess = bAbilityCastFlashSuccess;
			}
		}
		OutPoses.Add(std::move(p));
	}
}

void UTacticsMatchSubsystem::GatherBoardObstacleCells(TArray<FIntPoint>& OutCells) const
{
	OutCells.Reset();
	if (!Game) {
		return;
	}
	const std::vector<std::shared_ptr<tactics::Entity>> ents = Game->board.all_entities();
	for (const std::shared_ptr<tactics::Entity>& entPtr : ents) {
		if (!entPtr || entPtr->entity_type != "obstacle") {
			continue;
		}
		if (!entPtr->occupied_positions.empty()) {
			for (const auto& [wx, wy] : entPtr->occupied_positions) {
				OutCells.Add(FIntPoint(wx, wy));
			}
		} else if (entPtr->position) {
			OutCells.Add(FIntPoint(entPtr->position->first, entPtr->position->second));
		}
	}
}

void UTacticsMatchSubsystem::GatherBoardTerrainCells(TArray<FTacticsBoardTerrainCell>& OutCells) const
{
	OutCells.Reset();
	if (!Game) {
		return;
	}
	const tactics::BoardCellBounds B = MergedBounds(*Game);
	for (int y = B.min_y; y <= B.max_y; ++y) {
		for (int x = B.min_x; x <= B.max_x; ++x) {
			const auto Sq = Game->board.get_square(x, y);
			if (!Sq) {
				continue;
			}
			for (const tactics::SquareModifier& Mod : Sq->modifiers) {
				FTacticsBoardTerrainCell Cell;
				Cell.Cell = FIntPoint(x, y);
				Cell.TerrainName = UTF8_TO_TCHAR(Mod.name.c_str());
				Cell.MovementCost = Mod.movement_cost;
				Cell.DamageOnEnter = Mod.damage_on_enter;
				Cell.bIsVoid = Mod.is_void;
				OutCells.Add(std::move(Cell));
			}
		}
	}
}

bool UTacticsMatchSubsystem::IsRoadTerrainAtWorld(int WorldX, int WorldY) const
{
	if (!Game) {
		return false;
	}
	const auto Sq = Game->board.get_square(WorldX, WorldY);
	if (!Sq) {
		return false;
	}
	for (const tactics::SquareModifier& Mod : Sq->modifiers) {
		if (Mod.name == "road") {
			return true;
		}
	}
	return false;
}

bool UTacticsMatchSubsystem::IsRoughTerrainAtWorld(int WorldX, int WorldY) const
{
	if (!Game) {
		return false;
	}
	const auto Sq = Game->board.get_square(WorldX, WorldY);
	if (!Sq) {
		return false;
	}
	for (const tactics::SquareModifier& Mod : Sq->modifiers) {
		if (Mod.name == "rough") {
			return true;
		}
	}
	return false;
}

bool UTacticsMatchSubsystem::IsDamagingTerrainAtWorld(int WorldX, int WorldY) const
{
	if (!Game) {
		return false;
	}
	const auto Sq = Game->board.get_square(WorldX, WorldY);
	if (!Sq) {
		return false;
	}
	for (const tactics::SquareModifier& Mod : Sq->modifiers) {
		if (Mod.damage_on_enter > 0) {
			return true;
		}
	}
	return false;
}

bool UTacticsMatchSubsystem::IsVoidTerrainAtWorld(int WorldX, int WorldY) const
{
	if (!Game) {
		return false;
	}
	const auto Sq = Game->board.get_square(WorldX, WorldY);
	if (!Sq) {
		return false;
	}
	for (const tactics::SquareModifier& Mod : Sq->modifiers) {
		if (Mod.is_void) {
			return true;
		}
	}
	return false;
}

FString UTacticsMatchSubsystem::CellSummaryWorld(int WorldX, int WorldY) const
{
	if (!Game) {
		return TEXT("?");
	}
	const auto EmptyCellLabel = [this, WorldX, WorldY]() {
		if (IsVoidTerrainAtWorld(WorldX, WorldY)) {
			return FString(TEXT("void"));
		}
		if (IsDamagingTerrainAtWorld(WorldX, WorldY)) {
			return FString(TEXT("hazard"));
		}
		if (IsRoadTerrainAtWorld(WorldX, WorldY)) {
			return FString(TEXT("road"));
		}
		if (IsRoughTerrainAtWorld(WorldX, WorldY)) {
			return FString(TEXT("rough"));
		}
		return FString(TEXT("."));
	};
	for (const std::shared_ptr<tactics::Entity>& EntPtr : Game->board.all_entities()) {
		const auto U = std::dynamic_pointer_cast<tactics::Unit>(EntPtr);
		if (!U) {
			continue;
		}
		const auto Pending = PendingMoveForEntity(*Game, *U);
		if (Pending && PendingMoveFootprintContains(*U, *Pending, WorldX, WorldY)) {
			FString Id(UTF8_TO_TCHAR(U->entity_id.c_str()));
			return U->owner ? FString::Printf(TEXT("%s P%d"), *Id, *U->owner) : Id;
		}
	}
	const auto E = Game->board.entity_at(WorldX, WorldY);
	if (!E) {
		return EmptyCellLabel();
	}
	if (const auto U = std::dynamic_pointer_cast<tactics::Unit>(E)) {
		if (PendingMoveForEntity(*Game, *U)) {
			return EmptyCellLabel();
		}
	}
	FString Id(UTF8_TO_TCHAR(E->entity_id.c_str()));
	if (E->owner) {
		return FString::Printf(TEXT("%s P%d"), *Id, *E->owner);
	}
	return Id;
}

FTacticsBoardCellPresentation UTacticsMatchSubsystem::GetCellPresentationAtWorld(int WorldX, int WorldY) const
{
	FTacticsBoardCellPresentation Out;
	Out.Cell = FIntPoint(WorldX, WorldY);
	Out.Summary = CellSummaryWorld(WorldX, WorldY);
	Out.bSelected = IsSelectedAtWorld(WorldX, WorldY);
	int Px = 0;
	int Py = 0;
	Out.bPendingCliTarget = TryGetPendingCliWorldCell(Px, Py) && Px == WorldX && Py == WorldY;
	Out.bReachableMove = IsReachableMoveCellAtWorld(WorldX, WorldY);
	Out.bPendingMoveDestination = IsPendingMoveDestinationCellAtWorld(WorldX, WorldY);
	Out.bPendingMoveOrigin = IsPendingMoveOriginCellAtWorld(WorldX, WorldY);
	Out.bAttackTarget = IsAttackTargetCellAtWorld(WorldX, WorldY);
	Out.bBoardTargetEnemy = IsBoardTargetEnemyHighlightAtWorld(WorldX, WorldY);
	Out.bBoardTargetOther = IsBoardTargetOtherHighlightAtWorld(WorldX, WorldY);
	Out.bBoardTargetAoE = IsBoardTargetAoEHighlightAtWorld(WorldX, WorldY);
	Out.bActionQueueHoverSource = IsActionQueueHoverSourceAtWorld(WorldX, WorldY);
	Out.bActionQueueHoverTarget = IsActionQueueHoverTargetAtWorld(WorldX, WorldY);
	Out.bActionQueueHoverAoE = IsActionQueueHoverAoEAtWorld(WorldX, WorldY);
	const bool bAbilityPreview = IsAbilityBoardTargetPreviewActive();
	Out.bAbilityBoardTarget = bAbilityPreview && (Out.bBoardTargetOther || Out.bBoardTargetEnemy);
	Out.bAbilityBoardTargetEnemy = bAbilityPreview && Out.bBoardTargetEnemy;
	TryGetAbilityResolvePresentationAtWorld(WorldX, WorldY, Out.ResolveFlashAlpha, Out.ResolveFlashScale, Out.ResolveFlashColor);
	Out.bResolveFlash = Out.ResolveFlashAlpha > 0.01f;
	Out.bHasUnit = HasUnitAtWorld(WorldX, WorldY);
	Out.bHasControllableUnit = HasControllableUnitAtWorld(WorldX, WorldY);
	Out.bRoad = IsRoadTerrainAtWorld(WorldX, WorldY);
	Out.bRough = IsRoughTerrainAtWorld(WorldX, WorldY);
	Out.bDamagingTerrain = IsDamagingTerrainAtWorld(WorldX, WorldY);
	Out.bVoidTerrain = IsVoidTerrainAtWorld(WorldX, WorldY);
	if (Game) {
		if (const auto Sq = Game->board.get_square(WorldX, WorldY)) {
			for (const tactics::SquareModifier& Mod : Sq->modifiers) {
				if (Mod.name == "aether") {
					Out.bAetherTerrain = true;
				} else if (Mod.name == "scanner") {
					Out.bScannerTerrain = true;
				} else if (Mod.name == "omni_energy") {
					Out.bOmniEnergyTerrain = true;
				}
			}
		}
	}
	Out.bDeployZone = IsDeployZoneCellForPlayer(GetControlledPlayer(), WorldX, WorldY);
	Out.TurnOrderRank = GetTurnOrderRankAtWorld(WorldX, WorldY);
	Out.TerrainName = Out.bVoidTerrain ? TEXT("void")
		: Out.bDamagingTerrain ? TEXT("hazard")
		: Out.bAetherTerrain ? TEXT("aether")
		: Out.bScannerTerrain ? TEXT("scanner")
		: Out.bOmniEnergyTerrain ? TEXT("omni_energy")
		: Out.bRoad ? TEXT("road")
		: Out.bRough ? TEXT("rough")
		: FString();
	if (Game) {
		const auto E = Game->board.entity_at(WorldX, WorldY);
		if (E && E->entity_type == "unit") {
			Out.UnitArtId = ResolveArtIdForEntity(*Game, *E);
		}
	}
	return Out;
}

bool UTacticsMatchSubsystem::IsReachableMoveCellAtWorld(int WorldX, int WorldY) const
{
	return ReachableMoveCells.Contains(FIntPoint(WorldX, WorldY));
}

bool UTacticsMatchSubsystem::IsPendingMoveDestinationCellAtWorld(int WorldX, int WorldY) const
{
	return PendingMoveDestinationCells.Contains(FIntPoint(WorldX, WorldY));
}

bool UTacticsMatchSubsystem::IsPendingMoveOriginCellAtWorld(int WorldX, int WorldY) const
{
	return PendingMoveOriginCells.Contains(FIntPoint(WorldX, WorldY));
}

bool UTacticsMatchSubsystem::IsAttackTargetCellAtWorld(int WorldX, int WorldY) const
{
	return AttackTargetCells.Contains(FIntPoint(WorldX, WorldY));
}

bool UTacticsMatchSubsystem::IsBoardTargetEnemyHighlightAtWorld(int WorldX, int WorldY) const
{
	return BoardTargetEnemyCells.Contains(FIntPoint(WorldX, WorldY));
}

bool UTacticsMatchSubsystem::IsBoardTargetOtherHighlightAtWorld(int WorldX, int WorldY) const
{
	return BoardTargetOtherCells.Contains(FIntPoint(WorldX, WorldY));
}

bool UTacticsMatchSubsystem::IsBoardTargetAoEHighlightAtWorld(int WorldX, int WorldY) const
{
	return BoardTargetAoEBlastCells.Contains(FIntPoint(WorldX, WorldY));
}

bool UTacticsMatchSubsystem::IsActionQueueHoverSourceAtWorld(const int WorldX, const int WorldY) const
{
	return ActionQueueHoverSourceCells.Contains(FIntPoint(WorldX, WorldY));
}

bool UTacticsMatchSubsystem::IsActionQueueHoverTargetAtWorld(const int WorldX, const int WorldY) const
{
	return ActionQueueHoverTargetCells.Contains(FIntPoint(WorldX, WorldY));
}

bool UTacticsMatchSubsystem::IsActionQueueHoverAoEAtWorld(const int WorldX, const int WorldY) const
{
	return ActionQueueHoverAoECells.Contains(FIntPoint(WorldX, WorldY));
}

bool UTacticsMatchSubsystem::IsActionQueueHoverActive() const
{
	return PinnedActionQueueIndex >= 0 || HoveredActionQueueIndex >= 0;
}


