#include "TacticsMatchSubsystem.h"
#include "TacticsProjectContentReader.h"

#include "tactics/actions/actions.hpp"
#include "tactics/actions/move_resolution.hpp"
#include "tactics/apps/master_cli_dispatch.hpp"
#include "tactics/apps/sandbox_match.hpp"
#include "tactics/attributes/attributes.hpp"
#include "tactics/board/board.hpp"
#include "tactics/board/board_layout.hpp"
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


void UTacticsMatchSubsystem::ClearBoardTargetPreview()
{
	const bool bHadPreview = BoardTargetPreviewKind.has_value() || bBoardTargetPreviewSelectingFocusCaster
		|| bBoardTargetPreviewForcedDamageSpellFocus
		|| bBoardTargetPreviewSelectingPushDirection
		|| !BoardTargetEnemyCells.IsEmpty() || !BoardTargetOtherCells.IsEmpty() || !BoardTargetAoEBlastCells.IsEmpty();
	BoardTargetPreviewKind.reset();
	BoardTargetPreviewAbilityKey.Empty();
	BoardTargetPreviewSpellEffectKey.Empty();
	BoardTargetPreviewSpellShapeKey = TEXT("rectangle");
	BoardTargetPreviewSpellPayload.clear();
	BoardTargetPreviewSpellMaxRange = 4;
	bBoardTargetPreviewUsesFocusCaster = false;
	bBoardTargetPreviewSelectingFocusCaster = false;
	bBoardTargetPreviewForcedDamageSpellFocus = false;
	bBoardTargetPreviewSelectingPushDirection = false;
	BoardTargetPushEntityWorldX = -1;
	BoardTargetPushEntityWorldY = -1;
	BoardTargetPreviewRequireUnitTypes.clear();
	BoardTargetFocusRange = 0;
	ArmedBoardTargetHandIndex1Based = 0;
	ArmedBoardTargetReservesIndex1Based = 0;
	BoardTargetEnemyCells.Empty();
	BoardTargetOtherCells.Empty();
	BoardTargetAoEBlastCells.Empty();
	if (bHadPreview) {
		BroadcastRefresh();
	}
}

void UTacticsMatchSubsystem::ClearDeployPreview()
{
	if (ArmedDeployHandIndex1Based == 0 && ArmedDeployReservesIndex1Based == 0 && DeployValidCells.IsEmpty()) {
		return;
	}
	ArmedDeployHandIndex1Based = 0;
	ArmedDeployReservesIndex1Based = 0;
	DeployValidCells.Empty();
	BroadcastRefresh();
}

bool UTacticsMatchSubsystem::SetBoardTargetPreviewForHandCard(int32 Index1Based)
{
	ClearDeployPreview();
	ArmedDeployReservesIndex1Based = 0;
	if (!Game || Index1Based < 1) {
		ClearBoardTargetPreview();
		return false;
	}
	const auto It = Game->players_hands.find(ControlledPlayer);
	if (It == Game->players_hands.end() || It->second == nullptr || Index1Based > static_cast<int32>(It->second->size())) {
		ClearBoardTargetPreview();
		return false;
	}
	const tactics::CardInstanceId InstId = (*It->second)[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, InstId);
	if (!Def || !tactics::definition_is_spell(*Def) || !tactics::definition_spell_requires_mandatory_board_cell(*Def)) {
		ClearBoardTargetPreview();
		return false;
	}
	bBoardTargetPreviewSelectingFocusCaster = false;
	bBoardTargetPreviewSelectingPushDirection = false;
	BoardTargetPushEntityWorldX = -1;
	BoardTargetPushEntityWorldY = -1;
	if (tactics::spell_requires_focus_caster(*Def)) {
		if (!Selected || !IsSelectedUnitControlled()) {
			ClearBoardTargetPreview();
			return false;
		}
		bBoardTargetPreviewUsesFocusCaster = true;
		BoardTargetFocusRange = tactics::spell_focus_range(tactics::definition_spell(*Def));
	} else if (tactics::spell_requires_forced_damage_spell_focus_caster(*Game, ControlledPlayer, *Def)) {
		if (!Selected || !IsSelectedUnitControlled()
				|| !tactics::cast_uses_forced_damage_spell_focus_caster(*Game, *Def, Selected)) {
			ClearBoardTargetPreview();
			return false;
		}
		bBoardTargetPreviewUsesFocusCaster = true;
		if (const auto forced_range = tactics::entity_forced_damage_spell_focus_range(*Selected)) {
			BoardTargetFocusRange = *forced_range;
		} else {
			BoardTargetFocusRange = 0;
		}
	} else {
		bBoardTargetPreviewUsesFocusCaster = false;
		BoardTargetFocusRange = 0;
	}
	apply_spell_preview_fields_from_def(*Def, BoardTargetPreviewSpellEffectKey, BoardTargetPreviewSpellShapeKey, BoardTargetPreviewSpellPayload, BoardTargetPreviewSpellMaxRange);
	BoardTargetPreviewKind = tactics::spell_board_target_kind(*Def);
	BoardTargetPreviewRequireUnitTypes = tactics::definition_spell(*Def).require_target_unit_types;
	ArmedBoardTargetHandIndex1Based = Index1Based;
	ArmedBoardTargetReservesIndex1Based = 0;
	BroadcastRefresh();
	return true;
}

bool UTacticsMatchSubsystem::SetBoardTargetPreviewForHandCardMode(int32 Index1Based, int32 ModeIndex0)
{
	ClearDeployPreview();
	ArmedDeployReservesIndex1Based = 0;
	if (!Game || Index1Based < 1) {
		ClearBoardTargetPreview();
		return false;
	}
	const auto It = Game->players_hands.find(ControlledPlayer);
	if (It == Game->players_hands.end() || It->second == nullptr || Index1Based > static_cast<int32>(It->second->size())) {
		ClearBoardTargetPreview();
		return false;
	}
	const tactics::CardInstanceId InstId = (*It->second)[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, InstId);
	if (!Def || !tactics::definition_is_spell(*Def)
		|| !tactics::definition_spell_mode_requires_board_cell(*Def, ModeIndex0)) {
		ClearBoardTargetPreview();
		return false;
	}
	bBoardTargetPreviewSelectingFocusCaster = false;
	bBoardTargetPreviewSelectingPushDirection = false;
	BoardTargetPushEntityWorldX = -1;
	BoardTargetPushEntityWorldY = -1;
	if (tactics::spell_requires_focus_caster(*Def)) {
		if (!Selected || !IsSelectedUnitControlled()) {
			ClearBoardTargetPreview();
			return false;
		}
		bBoardTargetPreviewUsesFocusCaster = true;
		BoardTargetFocusRange = tactics::spell_focus_range(tactics::definition_spell(*Def));
	} else if (tactics::spell_requires_forced_damage_spell_focus_caster(*Game, ControlledPlayer, *Def)) {
		if (!Selected || !IsSelectedUnitControlled()
				|| !tactics::cast_uses_forced_damage_spell_focus_caster(*Game, *Def, Selected)) {
			ClearBoardTargetPreview();
			return false;
		}
		bBoardTargetPreviewUsesFocusCaster = true;
		if (const auto forced_range = tactics::entity_forced_damage_spell_focus_range(*Selected)) {
			BoardTargetFocusRange = *forced_range;
		} else {
			BoardTargetFocusRange = 0;
		}
	} else {
		bBoardTargetPreviewUsesFocusCaster = false;
		BoardTargetFocusRange = 0;
	}
	apply_spell_preview_fields_from_mode(*Def, ModeIndex0, BoardTargetPreviewSpellEffectKey, BoardTargetPreviewSpellShapeKey,
		BoardTargetPreviewSpellPayload, BoardTargetPreviewSpellMaxRange);
	BoardTargetPreviewKind = tactics::definition_spell_mode_board_target_kind(*Def, ModeIndex0);
	BoardTargetPreviewRequireUnitTypes.clear();
	ArmedBoardTargetHandIndex1Based = Index1Based;
	ArmedBoardTargetReservesIndex1Based = 0;
	BroadcastRefresh();
	return true;
}

bool UTacticsMatchSubsystem::SetBoardTargetPreviewForReservesCardMode(int32 Index1Based, int32 ModeIndex0)
{
	ClearDeployPreview();
	ArmedDeployHandIndex1Based = 0;
	if (!Game || Index1Based < 1) {
		ClearBoardTargetPreview();
		return false;
	}
	const auto DeckIt = Game->players_decks.find(ControlledPlayer);
	if (DeckIt == Game->players_decks.end() || Index1Based > static_cast<int32>(DeckIt->second.reserves.size())) {
		ClearBoardTargetPreview();
		return false;
	}
	const tactics::CardInstanceId InstId = DeckIt->second.reserves[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, InstId);
	if (!Def || !tactics::definition_is_spell(*Def)
		|| !tactics::definition_spell_mode_requires_board_cell(*Def, ModeIndex0)) {
		ClearBoardTargetPreview();
		return false;
	}
	bBoardTargetPreviewSelectingFocusCaster = false;
	bBoardTargetPreviewSelectingPushDirection = false;
	BoardTargetPushEntityWorldX = -1;
	BoardTargetPushEntityWorldY = -1;
	if (tactics::spell_requires_focus_caster(*Def)) {
		if (!Selected || !IsSelectedUnitControlled()) {
			ClearBoardTargetPreview();
			return false;
		}
		bBoardTargetPreviewUsesFocusCaster = true;
		BoardTargetFocusRange = tactics::spell_focus_range(tactics::definition_spell(*Def));
	} else if (tactics::spell_requires_forced_damage_spell_focus_caster(*Game, ControlledPlayer, *Def)) {
		if (!Selected || !IsSelectedUnitControlled()
				|| !tactics::cast_uses_forced_damage_spell_focus_caster(*Game, *Def, Selected)) {
			ClearBoardTargetPreview();
			return false;
		}
		bBoardTargetPreviewUsesFocusCaster = true;
		if (const auto forced_range = tactics::entity_forced_damage_spell_focus_range(*Selected)) {
			BoardTargetFocusRange = *forced_range;
		} else {
			BoardTargetFocusRange = 0;
		}
	} else {
		bBoardTargetPreviewUsesFocusCaster = false;
		BoardTargetFocusRange = 0;
	}
	apply_spell_preview_fields_from_mode(*Def, ModeIndex0, BoardTargetPreviewSpellEffectKey, BoardTargetPreviewSpellShapeKey,
		BoardTargetPreviewSpellPayload, BoardTargetPreviewSpellMaxRange);
	BoardTargetPreviewKind = tactics::definition_spell_mode_board_target_kind(*Def, ModeIndex0);
	BoardTargetPreviewRequireUnitTypes.clear();
	ArmedBoardTargetReservesIndex1Based = Index1Based;
	ArmedBoardTargetHandIndex1Based = 0;
	BroadcastRefresh();
	return true;
}

bool UTacticsMatchSubsystem::SetBoardTargetPreviewForReservesCard(int32 Index1Based)
{
	ClearDeployPreview();
	ArmedDeployHandIndex1Based = 0;
	if (!Game || Index1Based < 1) {
		ClearBoardTargetPreview();
		return false;
	}
	const auto DeckIt = Game->players_decks.find(ControlledPlayer);
	if (DeckIt == Game->players_decks.end() || Index1Based > static_cast<int32>(DeckIt->second.reserves.size())) {
		ClearBoardTargetPreview();
		return false;
	}
	const tactics::CardInstanceId InstId = DeckIt->second.reserves[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, InstId);
	if (!Def || !tactics::definition_is_spell(*Def) || !tactics::definition_spell_requires_mandatory_board_cell(*Def)) {
		ClearBoardTargetPreview();
		return false;
	}
	bBoardTargetPreviewSelectingFocusCaster = false;
	bBoardTargetPreviewSelectingPushDirection = false;
	BoardTargetPushEntityWorldX = -1;
	BoardTargetPushEntityWorldY = -1;
	if (tactics::spell_requires_focus_caster(*Def)) {
		if (!Selected || !IsSelectedUnitControlled()) {
			ClearBoardTargetPreview();
			return false;
		}
		bBoardTargetPreviewUsesFocusCaster = true;
		BoardTargetFocusRange = tactics::spell_focus_range(tactics::definition_spell(*Def));
	} else if (tactics::spell_requires_forced_damage_spell_focus_caster(*Game, ControlledPlayer, *Def)) {
		if (!Selected || !IsSelectedUnitControlled()
				|| !tactics::cast_uses_forced_damage_spell_focus_caster(*Game, *Def, Selected)) {
			ClearBoardTargetPreview();
			return false;
		}
		bBoardTargetPreviewUsesFocusCaster = true;
		if (const auto forced_range = tactics::entity_forced_damage_spell_focus_range(*Selected)) {
			BoardTargetFocusRange = *forced_range;
		} else {
			BoardTargetFocusRange = 0;
		}
	} else {
		bBoardTargetPreviewUsesFocusCaster = false;
		BoardTargetFocusRange = 0;
	}
	apply_spell_preview_fields_from_def(*Def, BoardTargetPreviewSpellEffectKey, BoardTargetPreviewSpellShapeKey, BoardTargetPreviewSpellPayload, BoardTargetPreviewSpellMaxRange);
	BoardTargetPreviewKind = tactics::spell_board_target_kind(*Def);
	BoardTargetPreviewRequireUnitTypes = tactics::definition_spell(*Def).require_target_unit_types;
	ArmedBoardTargetReservesIndex1Based = Index1Based;
	ArmedBoardTargetHandIndex1Based = 0;
	BroadcastRefresh();
	return true;
}

bool UTacticsMatchSubsystem::SetBoardPushDirectionPreviewForHandCard(int32 Index1Based, int32 TargetWorldX, int32 TargetWorldY)
{
	if (!SetBoardTargetPreviewForHandCard(Index1Based)) {
		return false;
	}
	bBoardTargetPreviewSelectingPushDirection = true;
	BoardTargetPushEntityWorldX = TargetWorldX;
	BoardTargetPushEntityWorldY = TargetWorldY;
	BroadcastRefresh();
	return true;
}

bool UTacticsMatchSubsystem::SetBoardPushDirectionPreviewForReservesCard(int32 Index1Based, int32 TargetWorldX, int32 TargetWorldY)
{
	if (!SetBoardTargetPreviewForReservesCard(Index1Based)) {
		return false;
	}
	bBoardTargetPreviewSelectingPushDirection = true;
	BoardTargetPushEntityWorldX = TargetWorldX;
	BoardTargetPushEntityWorldY = TargetWorldY;
	BroadcastRefresh();
	return true;
}

bool UTacticsMatchSubsystem::SetBoardFocusCasterSelectionPreviewForHandCard(int32 Index1Based)
{
	ClearDeployPreview();
	ArmedDeployReservesIndex1Based = 0;
	if (!Game || Index1Based < 1) {
		ClearBoardTargetPreview();
		return false;
	}
	const auto It = Game->players_hands.find(ControlledPlayer);
	if (It == Game->players_hands.end() || It->second == nullptr || Index1Based > static_cast<int32>(It->second->size())) {
		ClearBoardTargetPreview();
		return false;
	}
	const tactics::CardInstanceId InstId = (*It->second)[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, InstId);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		ClearBoardTargetPreview();
		return false;
	}
	const bool bFocus = tactics::spell_requires_focus_caster(*Def);
	const bool bForced = tactics::spell_requires_forced_damage_spell_focus_caster(*Game, ControlledPlayer, *Def);
	if (!bFocus && !bForced) {
		ClearBoardTargetPreview();
		return false;
	}
	bBoardTargetPreviewSelectingFocusCaster = true;
	bBoardTargetPreviewForcedDamageSpellFocus = bForced && !bFocus;
	bBoardTargetPreviewUsesFocusCaster = true;
	BoardTargetFocusRange = bFocus
		? tactics::spell_focus_range(tactics::definition_spell(*Def))
		: 2;
	apply_spell_preview_fields_from_def(*Def, BoardTargetPreviewSpellEffectKey, BoardTargetPreviewSpellShapeKey, BoardTargetPreviewSpellPayload, BoardTargetPreviewSpellMaxRange);
	BoardTargetPreviewKind = tactics::spell_board_target_kind(*Def);
	ArmedBoardTargetHandIndex1Based = Index1Based;
	ArmedBoardTargetReservesIndex1Based = 0;
	BroadcastRefresh();
	return true;
}

bool UTacticsMatchSubsystem::SetBoardFocusCasterSelectionPreviewForReservesCard(int32 Index1Based)
{
	ClearDeployPreview();
	ArmedDeployHandIndex1Based = 0;
	if (!Game || Index1Based < 1) {
		ClearBoardTargetPreview();
		return false;
	}
	const auto DeckIt = Game->players_decks.find(ControlledPlayer);
	if (DeckIt == Game->players_decks.end() || Index1Based > static_cast<int32>(DeckIt->second.reserves.size())) {
		ClearBoardTargetPreview();
		return false;
	}
	const tactics::CardInstanceId InstId = DeckIt->second.reserves[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, InstId);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		ClearBoardTargetPreview();
		return false;
	}
	const bool bFocus = tactics::spell_requires_focus_caster(*Def);
	const bool bForced = tactics::spell_requires_forced_damage_spell_focus_caster(*Game, ControlledPlayer, *Def);
	if (!bFocus && !bForced) {
		ClearBoardTargetPreview();
		return false;
	}
	bBoardTargetPreviewSelectingFocusCaster = true;
	bBoardTargetPreviewForcedDamageSpellFocus = bForced && !bFocus;
	bBoardTargetPreviewUsesFocusCaster = true;
	BoardTargetFocusRange = bFocus
		? tactics::spell_focus_range(tactics::definition_spell(*Def))
		: 2;
	apply_spell_preview_fields_from_def(*Def, BoardTargetPreviewSpellEffectKey, BoardTargetPreviewSpellShapeKey, BoardTargetPreviewSpellPayload, BoardTargetPreviewSpellMaxRange);
	BoardTargetPreviewKind = tactics::spell_board_target_kind(*Def);
	ArmedBoardTargetReservesIndex1Based = Index1Based;
	ArmedBoardTargetHandIndex1Based = 0;
	BroadcastRefresh();
	return true;
}

bool UTacticsMatchSubsystem::SetBoardTargetPreviewForSelectedAbility(const FString& AbilityKey)
{
	ClearDeployPreview();
	if (!Selected || !IsSelectedUnitControlled() || tactics::entity_is_jammed(*Selected) ||
		tactics::deployment_fatigue_blocks_abilities(*Selected)) {
		ClearBoardTargetPreview();
		return false;
	}
	const std::string Key = TCHAR_TO_UTF8(*AbilityKey);
	for (const tactics::AbilitySpec& Ability : Selected->activated_abilities) {
		if (Ability.key != Key) {
			continue;
		}
		const bool bNeedsEntityTarget = tactics::ability_requires_board_target(Ability);
		const bool bNeedsEmptyCell = tactics::effect_key_targets_empty_cell(Ability.effect_key);
		if (!bNeedsEntityTarget && !bNeedsEmptyCell) {
			ClearBoardTargetPreview();
			return false;
		}
		BoardTargetPreviewKind = tactics::ability_board_target_kind(Ability);
		BoardTargetPreviewAbilityKey = AbilityKey;
		BroadcastRefresh();
		return true;
	}
	ClearBoardTargetPreview();
	return false;
}

bool UTacticsMatchSubsystem::IsSelectedAtWorld(int WorldX, int WorldY) const
{
	if (!Selected) {
		return false;
	}
	if (Game) {
		const auto Pending = PendingMoveForEntity(*Game, *Selected);
		if (Pending) {
			return PendingMoveFootprintContains(*Selected, *Pending, WorldX, WorldY);
		}
	}
	for (const auto& [ox, oy] : Selected->occupied_positions) {
		if (ox == WorldX && oy == WorldY) {
			return true;
		}
	}
	if (Selected->position) {
		return Selected->position->first == WorldX && Selected->position->second == WorldY;
	}
	return false;
}

void UTacticsMatchSubsystem::ClearSelection()
{
	if (Game) {
		Game->clear_pending_move_for(ControlledPlayer);
	}
	Selected.reset();
	ClearAbilityTargetPreviewArtifacts();
	ClearBoardTargetPreview();
	ClearDeployPreview();
	BroadcastRefresh();
}

void UTacticsMatchSubsystem::ClearSelectedUnitOnly()
{
	Selected.reset();
	ClearAbilityTargetPreviewArtifacts();
	ClearBoardTargetPreview();
	ClearDeployPreview();
	BroadcastRefresh();
}

bool UTacticsMatchSubsystem::HasPendingMoveForControlledPlayer() const
{
	return Game && Game->has_pending_move_for(ControlledPlayer);
}

int32 UTacticsMatchSubsystem::GetPendingMoveQuarterTurnsCw() const
{
	if (!Game) {
		return 0;
	}
	const std::optional<tactics::PendingMoveSelection> Pm = Game->get_pending_move_for(ControlledPlayer);
	if (!Pm) {
		return 0;
	}
	return static_cast<int32>(Pm->quarter_turns_cw);
}

bool UTacticsMatchSubsystem::HasControllableUnitAtWorld(int WorldX, int WorldY) const
{
	// Allow focus-caster selection during AttackDeclaration as well (fast spells can be queued
	// then), not just during Main.  Reaction windows (SpellWindow, Defense, etc.) still return
	// false - the player cannot freely pick a caster in those phases.
	if (!Game || (!IsAnyMainPhase() && !IsAnyAttackDeclarationPhase())) {
		return false;
	}
	const tactics::BoardCellBounds B = MergedBounds(*Game);
	if (!InBoundsWorld(B, WorldX, WorldY)) {
		return false;
	}
	const auto U = DisplayUnitAtWorld(*Game, WorldX, WorldY);
	return U && U->owner && *U->owner == ControlledPlayer;
}

bool UTacticsMatchSubsystem::HasBoardCellAtWorld(int WorldX, int WorldY) const
{
	return Game && Game->board.get_square(WorldX, WorldY) != nullptr;
}

bool UTacticsMatchSubsystem::IsDeployZoneCellForPlayer(int32 PlayerId, int32 WorldX, int32 WorldY) const
{
	return Game && Game->is_deploy_zone_cell_for_player(PlayerId, WorldX, WorldY);
}

void UTacticsMatchSubsystem::RebuildDeployZoneHighlights()
{
	DeployValidCells.Empty();
	if (!Game) {
		return;
	}

	std::shared_ptr<tactics::Unit> DeployProbe;
	auto CanDeployArmedAt = [&](int AnchorX, int AnchorY) -> bool {
		if (ArmedDeployReservesIndex1Based >= 1) {
			return CanDeployReservesCardAt(ArmedDeployReservesIndex1Based, AnchorX, AnchorY);
		}
		if (ArmedDeployHandIndex1Based >= 1) {
			return CanDeployHandCardAt(ArmedDeployHandIndex1Based, AnchorX, AnchorY);
		}
		return false;
	};

	if (ArmedDeployReservesIndex1Based >= 1) {
		const auto DeckIt = Game->players_decks.find(ControlledPlayer);
		if (DeckIt == Game->players_decks.end()
			|| ArmedDeployReservesIndex1Based > static_cast<int32>(DeckIt->second.reserves.size())) {
			ArmedDeployReservesIndex1Based = 0;
			return;
		}
		const tactics::CardInstanceId InstId = DeckIt->second.reserves[static_cast<size_t>(ArmedDeployReservesIndex1Based - 1)];
		const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, InstId);
		if (!Def || !tactics::definition_is_unit(*Def)) {
			return;
		}
		DeployProbe = tactics::create_unit_from_definition(*Def, DeckIt->second.pool.at(InstId), ControlledPlayer, "deploy_highlight_probe");
		if (!DeployProbe) {
			ArmedDeployReservesIndex1Based = 0;
			return;
		}
	} else if (ArmedDeployHandIndex1Based >= 1) {
		const auto HandIt = Game->players_hands.find(ControlledPlayer);
		if (HandIt == Game->players_hands.end() || HandIt->second == nullptr
			|| ArmedDeployHandIndex1Based > static_cast<int32>(HandIt->second->size())) {
			ArmedDeployHandIndex1Based = 0;
			return;
		}
		const tactics::CardInstanceId InstId = (*HandIt->second)[static_cast<size_t>(ArmedDeployHandIndex1Based - 1)];
		const auto DeckIt = Game->players_decks.find(ControlledPlayer);
		if (DeckIt == Game->players_decks.end()) {
			ArmedDeployHandIndex1Based = 0;
			return;
		}
		const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, InstId);
		if (!Def || !tactics::definition_is_unit(*Def)) {
			return;
		}
		DeployProbe = tactics::create_unit_from_definition(*Def, DeckIt->second.pool.at(InstId), ControlledPlayer, "deploy_highlight_probe");
		if (!DeployProbe) {
			ArmedDeployHandIndex1Based = 0;
			return;
		}
	} else {
		return;
	}

	const std::shared_ptr<tactics::Unit> Probe = DeployProbe;
	if (!Probe) {
		return;
	}
	const auto TryAnchor = [&](int AnchorX, int AnchorY) {
		if (!CanDeployArmedAt(AnchorX, AnchorY)) {
			return;
		}
		for (const auto& [Dx, Dy] : tactics::entity_shape_offsets(*Probe)) {
			DeployValidCells.Add(FIntPoint(AnchorX + Dx, AnchorY + Dy));
		}
	};
	// Full-board scan: buildings may deploy next to allies off-zone; 1x1 units may use Command range.
	const tactics::BoardCellBounds B = MergedBounds(*Game);
	for (int Y = B.min_y; Y <= B.max_y; ++Y) {
		for (int X = B.min_x; X <= B.max_x; ++X) {
			if (!Game->board.get_square(X, Y)) {
				continue;
			}
			TryAnchor(X, Y);
		}
	}
}

bool UTacticsMatchSubsystem::IsDeployPreviewArmed() const
{
	return ArmedDeployHandIndex1Based > 0 || ArmedDeployReservesIndex1Based > 0;
}

bool UTacticsMatchSubsystem::IsDeployValidCellAtWorld(int WorldX, int WorldY) const
{
	return DeployValidCells.Contains(FIntPoint(WorldX, WorldY));
}

bool UTacticsMatchSubsystem::SetDeployPreviewForHandCard(int32 Index1Based)
{
	ClearBoardTargetPreview();
	ClearDeployPreview();
	if (!Game || Index1Based < 1) {
		return false;
	}
	const auto HandIt = Game->players_hands.find(ControlledPlayer);
	if (HandIt == Game->players_hands.end() || HandIt->second == nullptr || Index1Based > static_cast<int32>(HandIt->second->size())) {
		return false;
	}
	const tactics::CardInstanceId InstId = (*HandIt->second)[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, InstId);
	if (!Def || !tactics::definition_is_unit(*Def)) {
		return false;
	}
	ArmedDeployReservesIndex1Based = 0;
	ArmedDeployHandIndex1Based = Index1Based;
	RebuildDeployZoneHighlights();
	BroadcastRefresh();
	return !DeployValidCells.IsEmpty();
}

bool UTacticsMatchSubsystem::SetDeployPreviewForReservesCard(int32 Index1Based)
{
	ClearBoardTargetPreview();
	ClearDeployPreview();
	if (!Game || Index1Based < 1) {
		return false;
	}
	const auto DeckIt = Game->players_decks.find(ControlledPlayer);
	if (DeckIt == Game->players_decks.end() || Index1Based > static_cast<int32>(DeckIt->second.reserves.size())) {
		return false;
	}
	const tactics::CardInstanceId InstId = DeckIt->second.reserves[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, InstId);
	if (!Def || !tactics::definition_is_unit(*Def)) {
		return false;
	}
	ArmedDeployHandIndex1Based = 0;
	ArmedDeployReservesIndex1Based = Index1Based;
	RebuildDeployZoneHighlights();
	BroadcastRefresh();
	return !DeployValidCells.IsEmpty();
}

bool UTacticsMatchSubsystem::CanDeployHandCardAt(int32 HandIndex1Based, int32 WorldX, int32 WorldY) const
{
	if (!Game) {
		return false;
	}
	auto hand_it = Game->players_hands.find(ControlledPlayer);
	if (hand_it == Game->players_hands.end() || hand_it->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* hand = hand_it->second;
	const auto idx = tactics::parse_cli_index_1based(static_cast<int>(hand->size()), std::to_string(HandIndex1Based));
	if (!idx) {
		return false;
	}
	const tactics::CardInstanceId cid = (*hand)[static_cast<size_t>(*idx)];
	const tactics::CardDefinition* def = MatchCardDef(*Game, ControlledPlayer, cid);
	if (!def || !tactics::definition_is_unit(*def)) {
		return false;
	}
	tactics::DeployAction probe(cid, ControlledPlayer, {WorldX, WorldY});
	return probe.validate(*Game).ok;
}

bool UTacticsMatchSubsystem::CanDeployReservesCardAt(int32 ReservesIndex1Based, int32 WorldX, int32 WorldY) const
{
	if (!Game) {
		return false;
	}
	const auto deck_it = Game->players_decks.find(ControlledPlayer);
	if (deck_it == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& reserves = deck_it->second.reserves;
	const auto idx = tactics::parse_cli_index_1based(static_cast<int>(reserves.size()), std::to_string(ReservesIndex1Based));
	if (!idx) {
		return false;
	}
	const tactics::CardInstanceId cid = reserves[static_cast<size_t>(*idx)];
	const tactics::CardDefinition* def = MatchCardDef(*Game, ControlledPlayer, cid);
	if (!def || !tactics::definition_is_unit(*def)) {
		return false;
	}
	tactics::DeployAction probe(cid, ControlledPlayer, {WorldX, WorldY}, tactics::CardPlayZone::Reserves);
	return probe.validate(*Game).ok;
}

bool UTacticsMatchSubsystem::HasUnitAtWorld(int WorldX, int WorldY) const
{
	if (!Game) {
		return false;
	}
	const tactics::BoardCellBounds B = MergedBounds(*Game);
	if (!InBoundsWorld(B, WorldX, WorldY)) {
		return false;
	}
	return static_cast<bool>(DisplayUnitAtWorld(*Game, WorldX, WorldY));
}

bool UTacticsMatchSubsystem::TrySelectWorld(int WorldX, int WorldY, FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	const tactics::BoardCellBounds B = MergedBounds(*Game);
	if (!InBoundsWorld(B, WorldX, WorldY)) {
		OutMessage = TEXT("Cell out of bounds.");
		return false;
	}
	const auto E = Game->board.entity_at(WorldX, WorldY);
	const auto U = std::dynamic_pointer_cast<tactics::Unit>(E);
	if (!U) {
		Selected.reset();
		BoardTargetPreviewKind.reset();
		BoardTargetEnemyCells.Empty();
		BoardTargetOtherCells.Empty();
		OutMessage = TEXT("No unit or building there.");
		BroadcastRefresh();
		return false;
	}
	Game->reconcile_pending_move_for_unit_selection(ControlledPlayer, U->entity_id);
	Selected = U;
	OutMessage = IsSelectedUnitControlled()
		? FString::Printf(TEXT("Selected %s"), UTF8_TO_TCHAR(Selected->entity_id.c_str()))
		: FString::Printf(TEXT("Inspecting %s"), UTF8_TO_TCHAR(Selected->entity_id.c_str()));
	BroadcastRefresh();
	return true;
}

bool UTacticsMatchSubsystem::TrySelectFromCli1BasedCell(int32 Col1, int32 Row1, FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	const tactics::BoardCellBounds B = MergedBounds(*Game);
	const int Wx = B.min_x + Col1 - 1;
	const int Wy = B.min_y + Row1 - 1;
	return TrySelectWorld(Wx, Wy, OutMessage);
}

bool UTacticsMatchSubsystem::CanControlledUnitSpendAttackAction() const
{
	if (!Game || !Selected || !IsSelectedUnitControlled()) {
		return false;
	}
	if (!CanControlledPlayerActInMainPhase() || HasPendingMoveForControlledPlayer()) {
		return false;
	}
	if (tactics::deployment_fatigue_blocks_attack_actions(*Selected)) {
		return false;
	}
	return Selected->attacks_remaining_this_turn > 0;
}

bool UTacticsMatchSubsystem::CanControlledUnitDefend() const
{
	if (!Game || !Selected || !IsSelectedUnitControlled()) {
		return false;
	}
	if (!IsAnyMainPhase() || IsAnyAttackDeclarationPhase()) {
		return false;
	}
	if (!CanControlledPlayerActInMainPhase() || HasPendingMoveForControlledPlayer()) {
		return false;
	}
	if (tactics::deployment_fatigue_blocks_attack_actions(*Selected)) {
		return false;
	}
	return tactics::validate_unit_defend_budget(*Game, *Selected, ControlledPlayer).ok;
}

bool UTacticsMatchSubsystem::TryDefendSelectedUnit(FString& OutMessage)
{
	if (!CanControlledUnitDefend()) {
		OutMessage = TEXT("Cannot defend right now.");
		return false;
	}
	tactics::DefendAction Action(Selected, ControlledPlayer);
	const tactics::ActionResult R = Game->perform_action(ControlledPlayer, Action);
	OutMessage = R.ok ? (TEXT("Defend: ") + StdToF(R.message)) : StdToF(R.message);
	if (R.ok) {
		BroadcastRefresh();
	}
	return R.ok;
}

bool UTacticsMatchSubsystem::CanControlledUnitDash() const
{
	if (!Game || !Selected || !IsSelectedUnitControlled()) {
		return false;
	}
	if (!CanControlledPlayerActInMainPhase() || HasPendingMoveForControlledPlayer()) {
		return false;
	}
	if (tactics::deployment_fatigue_blocks_attack_actions(*Selected)) {
		return false;
	}
	return tactics::validate_unit_dash_budget(*Game, *Selected, ControlledPlayer).ok;
}

bool UTacticsMatchSubsystem::TryDashSelectedUnit(FString& OutMessage)
{
	if (!CanControlledUnitDash()) {
		OutMessage = TEXT("Cannot dash right now.");
		return false;
	}
	tactics::DashAction Action(Selected, ControlledPlayer);
	const tactics::ActionResult R = Game->perform_action(ControlledPlayer, Action);
	OutMessage = R.ok ? (TEXT("Dash: ") + StdToF(R.message)) : StdToF(R.message);
	if (R.ok) {
		BroadcastRefresh();
	}
	return R.ok;
}

bool UTacticsMatchSubsystem::CanControlledUnitRecover() const
{
	if (!Game || !Selected || !IsSelectedUnitControlled()) {
		return false;
	}
	if (!IsAnyMainPhase() || IsAnyAttackDeclarationPhase()) {
		return false;
	}
	if (!CanControlledPlayerActInMainPhase() || HasPendingMoveForControlledPlayer()) {
		return false;
	}
	if (tactics::deployment_fatigue_blocks_attack_actions(*Selected)) {
		return false;
	}
	return tactics::validate_unit_recover_budget(*Game, *Selected, ControlledPlayer).ok;
}

bool UTacticsMatchSubsystem::TryRecoverSelectedUnit(FString& OutMessage)
{
	if (!CanControlledUnitRecover()) {
		OutMessage = TEXT("Cannot recover right now.");
		return false;
	}
	tactics::RecoverAction Action(Selected, ControlledPlayer);
	const tactics::ActionResult R = Game->perform_action(ControlledPlayer, Action);
	OutMessage = R.ok ? (TEXT("Recover: ") + StdToF(R.message)) : StdToF(R.message);
	if (R.ok) {
		BroadcastRefresh();
	}
	return R.ok;
}


bool UTacticsMatchSubsystem::CanControlledPlayerUndo() const
{
	if (!Game || ControlledPlayer <= 0) {
		return false;
	}
	return Game->can_undo_last_action(ControlledPlayer);
}

bool UTacticsMatchSubsystem::TryUndoLastAction(FString& OutMessage)
{
	if (!Game || ControlledPlayer <= 0) {
		OutMessage = TEXT("No match.");
		return false;
	}
	const tactics::ActionResult R = Game->undo_last_action(ControlledPlayer);
	OutMessage = StdToF(R.message);
	if (R.ok) {
		BroadcastRefresh();
	}
	return R.ok;
}

bool UTacticsMatchSubsystem::CanControlledPlayerCancelBatchItem(const FString& ItemId, const int32 ControllerPlayerId) const
{
	if (!Game || ControlledPlayer <= 0 || ItemId.IsEmpty()) {
		return false;
	}
	if (ControllerPlayerId != ControlledPlayer) {
		return false;
	}
	if (!IsAnyMainPhase() && !IsAnyReactionWindowPhase()) {
		return false;
	}
	const std::string Id = TCHAR_TO_UTF8(*ItemId);
	const tactics::StackItem* Item = Game->find_batched_item(Id);
	return Item && Item->controller_id == ControlledPlayer && Game->allows_queued_batch_invalidation_refund();
}

bool UTacticsMatchSubsystem::TryCancelBatchItem(const FString& ItemId, FString& OutMessage)
{
	if (!Game || ControlledPlayer <= 0) {
		OutMessage = TEXT("No match.");
		return false;
	}
	const tactics::ActionResult R = Game->cancel_queued_batch_item_for_player(ControlledPlayer, TCHAR_TO_UTF8(*ItemId));
	OutMessage = StdToF(R.message);
	if (R.ok) {
		BroadcastRefresh();
	}
	return R.ok;
}

bool UTacticsMatchSubsystem::TryMoveOrAttackWorld(int WorldX, int WorldY, FString& OutMessage)
{
	if (!Game || !Selected) {
		OutMessage = TEXT("Select a unit first.");
		return false;
	}
	if (!IsSelectedUnitControlled()) {
		OutMessage = TEXT("Selected unit is not controlled by you.");
		return false;
	}
	const tactics::BoardCellBounds B = MergedBounds(*Game);
	if (!InBoundsWorld(B, WorldX, WorldY)) {
		OutMessage = TEXT("Cell out of bounds.");
		return false;
	}
	if (!IsAnyMainPhase()) {
		OutMessage = TEXT("Not in a main phase.");
		return false;
	}
	if (const auto Cp = Game->turn_manager.current_player()) {
		if (*Cp != ControlledPlayer) {
			OutMessage = FString::Printf(
				TEXT("Active player is P%d; you control P%d. Pass or end turn before move/attack."),
				*Cp, ControlledPlayer);
			return false;
		}
	}
	const auto Tgt = Game->board.entity_at(WorldX, WorldY);
	const auto TgtUnit = Tgt ? std::dynamic_pointer_cast<tactics::Unit>(Tgt) : nullptr;
	const bool EnemyHere = TgtUnit && Tgt->owner && Selected->owner && tactics::teams_hostile(*Game, *Selected->owner, *Tgt->owner);

	if (EnemyHere) {
		const std::shared_ptr<tactics::Unit> AttackActor = Game->unit_at_validation_pose(Selected);
		bool Ranged = false;
		if (AttackActor && AttackActor->position) {
			const int Dx = std::abs(AttackActor->position->first - WorldX);
			const int Dy = std::abs(AttackActor->position->second - WorldY);
			Ranged = std::max(Dx, Dy) > 1;
		}
		tactics::AttackAction A(AttackActor, ControlledPlayer, {WorldX, WorldY}, Ranged);
		const tactics::ActionResult R = Game->perform_action(ControlledPlayer, A);
		OutMessage = R.ok ? (TEXT("Attack: ") + StdToF(R.message)) : StdToF(R.message);
		if (R.ok) {
			BroadcastRefresh();
		}
		return R.ok;
	}

	tactics::MovePreviewAction M(Selected, ControlledPlayer, {WorldX, WorldY});
	const tactics::ActionResult R = Game->perform_action(ControlledPlayer, M);
	OutMessage = R.ok ? StdToF(R.message) : StdToF(R.message);
	if (R.ok) {
		BroadcastRefresh();
	}
	return R.ok;
}

bool UTacticsMatchSubsystem::IsEnemyUnitAtWorldVsSelected(int WorldX, int WorldY) const
{
	if (!Game || !Selected || !Selected->owner) {
		return false;
	}
	const auto E = Game->board.entity_at(WorldX, WorldY);
	const auto TgtUnit = E ? std::dynamic_pointer_cast<tactics::Unit>(E) : nullptr;
	return TgtUnit && TgtUnit->owner && (*TgtUnit->owner != *Selected->owner);
}

bool UTacticsMatchSubsystem::TryDeployWorld(int HandIndex1Based, int WorldX, int WorldY, FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	const tactics::BoardCellBounds B = MergedBounds(*Game);
	if (!InBoundsWorld(B, WorldX, WorldY)) {
		OutMessage = TEXT("Cell out of bounds.");
		return false;
	}
	auto* Hand = Game->players_hands.at(ControlledPlayer);
	if (Hand->empty()) {
		OutMessage = TEXT("Hand is empty.");
		return false;
	}
	const auto IdxOpt = tactics::parse_cli_index_1based(static_cast<int>(Hand->size()), std::to_string(HandIndex1Based));
	if (!IdxOpt) {
		OutMessage = FString::Printf(TEXT("Hand index must be 1-%d."), static_cast<int>(Hand->size()));
		return false;
	}
	const int Idx = *IdxOpt;
	const tactics::CardInstanceId cid = (*Hand)[Idx];
	const tactics::CardDefinition* def = MatchCardDef(*Game, ControlledPlayer, cid);
	if (!def || !tactics::definition_is_unit(*def)) {
		OutMessage = TEXT("That slot is not deployable.");
		return false;
	}
	tactics::DeployAction A(cid, ControlledPlayer, {WorldX, WorldY});
	const tactics::ActionResult R = Game->perform_action(ControlledPlayer, A);
	OutMessage = R.ok ? TEXT("Deploy ok.") : StdToF(R.message);
	if (R.ok) {
		BroadcastRefresh();
	}
	return R.ok;
}

bool UTacticsMatchSubsystem::TrySkipEnergyZone(FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	const std::optional<int> Cp = Game->turn_manager.current_player();
	if (!Cp || *Cp != ControlledPlayer) {
		OutMessage = TEXT("Not your territory choice (turn).");
		return false;
	}
	const tactics::ActionResult R = Game->skip_energy_zone(ControlledPlayer);
	OutMessage = R.ok ? TEXT("Territory skipped.") : StdToF(R.message);
	BroadcastRefresh();
	return R.ok;
}

bool UTacticsMatchSubsystem::TryEndTurn(FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	const bool Ok = Game->end_current_turn();
	if (Game->IsAwaitingHandDiscard()) {
		OutMessage = TEXT(
			"Hand size was greater than 8 ? discard phase: select cards in the hand strip, then use Discard selected card until at most 8.");
		BroadcastRefresh();
		return true;
	}
	OutMessage = Ok ? TEXT("Turn ended.") : TEXT("Could not end turn (stack must be empty).");
	if (Ok) {
		Selected.reset();
		BoardTargetPreviewKind.reset();
		BoardTargetEnemyCells.Empty();
		BoardTargetOtherCells.Empty();
	}
	BroadcastRefresh();
	return Ok;
}

bool UTacticsMatchSubsystem::IsAwaitingHandDiscard() const
{
	return Game && Game->IsAwaitingHandDiscard();
}

int32 UTacticsMatchSubsystem::GetHandViewPlayerId() const
{
	if (!Game) {
		return ControlledPlayer;
	}
	if (const std::optional<int> Pd = Game->PendingDiscardPlayerId()) {
		return static_cast<int32>(*Pd);
	}
	return ControlledPlayer;
}

bool UTacticsMatchSubsystem::TryDiscardHandCard(int32 HandIndex1Based, FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	const std::optional<int> Pd = Game->PendingDiscardPlayerId();
	if (!Pd) {
		OutMessage = TEXT("You are not choosing discards right now.");
		return false;
	}
	const tactics::ActionResult R = Game->discard_hand_card_at(*Pd, HandIndex1Based);
	OutMessage = R.ok ? TEXT("Discarded.") : StdToF(R.message);
	if (R.ok && !Game->IsAwaitingHandDiscard()) {
		Selected.reset();
		BoardTargetPreviewKind.reset();
		BoardTargetEnemyCells.Empty();
		BoardTargetOtherCells.Empty();
	}
	BroadcastRefresh();
	return R.ok;
}

bool UTacticsMatchSubsystem::IsAwaitingScan() const
{
	return Game && Game->IsAwaitingScan();
}

int32 UTacticsMatchSubsystem::GetPendingScanPlayerId() const
{
	if (!Game) {
		return 0;
	}
	const std::optional<int> P = Game->PendingScanPlayerId();
	return P ? static_cast<int32>(*P) : 0;
}

int32 UTacticsMatchSubsystem::GetPendingScanPeekCount() const
{
	if (!Game || !Game->IsPendingScanForPlayer(ControlledPlayer)) {
		return 0;
	}
	const std::vector<tactics::CardInstanceId>* Peeked = Game->pending_scan_peeked_for(ControlledPlayer);
	return Peeked ? static_cast<int32>(Peeked->size()) : 0;
}

bool UTacticsMatchSubsystem::TryGetScanPeekCardUi(int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutCostLine) const
{
	if (!Game || Index1Based < 1 || !Game->IsPendingScanForPlayer(ControlledPlayer)) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Peeked = Game->pending_scan_peeked_for(ControlledPlayer);
	if (!Peeked || Index1Based > static_cast<int32>(Peeked->size())) {
		return false;
	}
	const tactics::CardInstanceId InstId = (*Peeked)[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, InstId);
	if (!Def) {
		return false;
	}
	FString RulesUnused;
	return fill_card_ui_strings(*Def, MatchCardInst(*Game, ControlledPlayer, InstId), OutName, OutTypeTag, OutCostLine, RulesUnused, bShowAdvancedCardText);
}

bool UTacticsMatchSubsystem::TryGetScanPeekCardDetail(int32 Index1Based, FString& OutName, FString& OutTypeTag,
	FString& OutCostLine, FString& OutRules, FString& OutArtId, FString& OutStatTokens) const
{
	OutArtId.Reset();
	OutStatTokens.Reset();
	if (!Game || Index1Based < 1 || !Game->IsPendingScanForPlayer(ControlledPlayer)) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Peeked = Game->pending_scan_peeked_for(ControlledPlayer);
	if (!Peeked || Index1Based > static_cast<int32>(Peeked->size())) {
		return false;
	}
	const tactics::CardInstanceId InstId = (*Peeked)[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, InstId);
	if (!Def) {
		return false;
	}
	if (!fill_card_ui_strings(*Def, MatchCardInst(*Game, ControlledPlayer, InstId), OutName, OutTypeTag, OutCostLine,
			OutRules, bShowAdvancedCardText)) {
		return false;
	}
	if (!Def->art_id.empty()) {
		OutArtId = UTF8_TO_TCHAR(Def->art_id.c_str());
	}
	OutStatTokens = build_unit_stat_tokens(*Def, bShowAdvancedCardText);
	return true;
}

bool UTacticsMatchSubsystem::TryScanDiscardAt(int32 Index1Based, FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	if (!Game->IsPendingScanForPlayer(ControlledPlayer)) {
		OutMessage = TEXT("You are not scanning right now.");
		return false;
	}
	const tactics::ActionResult R = Game->scan_discard_at(ControlledPlayer, Index1Based);
	OutMessage = R.ok ? TEXT("Discarded scanned card.") : StdToF(R.message);
	BroadcastRefresh();
	return R.ok;
}

bool UTacticsMatchSubsystem::TryScanFinish(FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	if (!Game->IsPendingScanForPlayer(ControlledPlayer)) {
		OutMessage = TEXT("You are not scanning right now.");
		return false;
	}
	const tactics::ActionResult R = Game->scan_finish(ControlledPlayer);
	OutMessage = R.ok ? TEXT("Scan complete.") : StdToF(R.message);
	BroadcastRefresh();
	return R.ok;
}

bool UTacticsMatchSubsystem::IsAwaitingTerritoryLoot() const
{
	return Game && Game->IsAwaitingTerritoryLoot();
}

bool UTacticsMatchSubsystem::TryTerritoryLootDiscard(int32 HandIndex1Based, FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	if (!Game->IsPendingTerritoryLootForPlayer(ControlledPlayer)) {
		OutMessage = TEXT("No territory loot choice pending.");
		return false;
	}
	const tactics::ActionResult R = Game->territory_loot_discard_at(ControlledPlayer, HandIndex1Based);
	OutMessage = R.ok ? TEXT("Discarded a card and drew 1.") : StdToF(R.message);
	BroadcastRefresh();
	return R.ok;
}

bool UTacticsMatchSubsystem::TryTerritoryLootSkip(FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	if (!Game->IsPendingTerritoryLootForPlayer(ControlledPlayer)) {
		OutMessage = TEXT("No territory loot choice pending.");
		return false;
	}
	const tactics::ActionResult R = Game->territory_loot_skip(ControlledPlayer);
	OutMessage = R.ok ? TEXT("Skipped territory loot.") : StdToF(R.message);
	BroadcastRefresh();
	return R.ok;
}

void UTacticsMatchSubsystem::GetControlledAvailableEnergyCounters(TArray<FString>& OutTypeLabels, TArray<int32>& OutFreeFloating,
	TArray<int32>& OutTaggedFloating, TArray<int32>& OutFromZones) const
{
	OutTypeLabels.Reset();
	OutFreeFloating.Reset();
	OutTaggedFloating.Reset();
	OutFromZones.Reset();
	if (!Game) {
		return;
	}
	const int Pid = ControlledPlayer;

	// Unrestricted floating energy (usable for any action type).
	std::map<tactics::EnergyType, int> Free;
	if (const auto It = Game->turn_manager.player_energy.find(Pid); It != Game->turn_manager.player_energy.end()) {
		for (const auto& [Et, Amt] : It->second) {
			if (Amt > 0) {
				Free[Et] += Amt;
			}
		}
	}

	// Tagged floating energy (restricted pools - spell_ability, spell_only, unit_deploy, etc.).
	std::map<tactics::EnergyType, int> Tagged;
	for (const auto& [Tag, PerPlayer] : Game->turn_manager.player_tagged_float) {
		if (const auto Pit = PerPlayer.find(Pid); Pit != PerPlayer.end()) {
			for (const auto& [Et, Amt] : Pit->second) {
				if (Amt > 0) {
					Tagged[Et] += Amt;
				}
			}
		}
	}

	// Potential from untapped energy zones ("reserves"): sum of each untapped zone's producible energy.
	std::map<tactics::EnergyType, int> FromZones;
	if (const auto Zit = Game->players_energy_zones.find(Pid); Zit != Game->players_energy_zones.end()) {
		for (const tactics::EnergyZone& Z : Zit->second) {
			// Passive legacy production (if untapped).
			if (!Z.is_tapped) {
				for (const auto& [Et, Amt] : Z.energy_produced) {
					if (Amt > 0) {
						FromZones[Et] += Amt;
					}
				}
			}
			// Conquering Territories: energy the game can auto-tap from an energy-only "use land" ability.
			const int Ai = Z.auto_tap_ability_index();
			if (Ai >= 0) {
				for (const auto& [Et, Amt] : Z.land_abilities[static_cast<std::size_t>(Ai)].energy_produced) {
					if (Amt > 0) {
						FromZones[Et] += Amt;
					}
				}
			}
		}
	}

	// Emit one entry per type (canonical order) that has any nonzero amount in any bucket.
	for (const tactics::EnergyType Et : tactics::kEnergyBillingAllTypes) {
		const int Fr = Free.count(Et) ? Free.at(Et) : 0;
		const int Tg = Tagged.count(Et) ? Tagged.at(Et) : 0;
		const int Zn = FromZones.count(Et) ? FromZones.at(Et) : 0;
		if (Fr == 0 && Tg == 0 && Zn == 0) {
			continue;
		}
		OutTypeLabels.Add(FString(UTF8_TO_TCHAR(tactics::to_string(Et).c_str())));
		OutFreeFloating.Add(Fr);
		OutTaggedFloating.Add(Tg);
		OutFromZones.Add(Zn);
	}
}

int32 UTacticsMatchSubsystem::GetPendingEnergyZoneChoiceCount() const
{
	if (!Game || Game->turn_manager.current_phase != tactics::TurnPhase::Energy) {
		return 0;
	}
	const auto Cp = Game->turn_manager.current_player();
	if (!Cp || *Cp != ControlledPlayer) {
		return 0;
	}
	const auto It = Game->turn_manager.pending_energy_choices.find(*Cp);
	if (It == Game->turn_manager.pending_energy_choices.end()) {
		return 0;
	}
	return static_cast<int32>(It->second.size());
}

bool UTacticsMatchSubsystem::TryGetEnergyZoneChoiceUi(int32 Index1Based, FString& OutName, FString& OutProducesLine,
	FString& OutArtId) const
{
	if (!Game || Index1Based < 1) {
		return false;
	}
	if (Game->turn_manager.current_phase != tactics::TurnPhase::Energy) {
		return false;
	}
	const auto Cp = Game->turn_manager.current_player();
	if (!Cp || *Cp != ControlledPlayer) {
		return false;
	}
	const auto It = Game->turn_manager.pending_energy_choices.find(*Cp);
	if (It == Game->turn_manager.pending_energy_choices.end()) {
		return false;
	}
	const std::vector<tactics::EnergyZone>& Choices = It->second;
	if (Index1Based > static_cast<int>(Choices.size())) {
		return false;
	}
	const tactics::EnergyZone& Z = Choices[static_cast<size_t>(Index1Based - 1)];
	OutName = FString(UTF8_TO_TCHAR(Z.name.c_str()));
	OutArtId = Z.art_id.empty() ? FString{} : FString(UTF8_TO_TCHAR(Z.art_id.c_str()));
	FString Line;
	for (const auto& Pr : Z.energy_produced) {
		if (!Line.IsEmpty()) {
			Line += TEXT(" ");
		}
		Line += FString::Printf(TEXT("%s:%d"), UTF8_TO_TCHAR(tactics::to_string(Pr.first).c_str()), Pr.second);
	}
	OutProducesLine = Line;
	return true;
}

namespace {
FString HumanizeEffectKey(const std::string& Key)
{
	FString S = FString(UTF8_TO_TCHAR(Key.c_str())).Replace(TEXT("_"), TEXT(" "));
	if (!S.IsEmpty()) {
		S[0] = FChar::ToUpper(S[0]);
	}
	return S;
}

int32 TerritoryPayloadInt(const tactics::TerritoryEffect& E, const char* Key, const int32 Default = 0)
{
	const auto It = E.payload.find(Key);
	return It != E.payload.end() ? static_cast<int32>(It->second) : Default;
}

FString TerritoryPayloadString(const tactics::TerritoryEffect& E, const char* Key)
{
	const auto It = E.string_payload.find(Key);
	return It != E.string_payload.end() ? StdToF(It->second) : FString();
}

FString EnergyPip(const tactics::EnergyType Type)
{
	return FString::Printf(TEXT("{%c}"), EnergyBraceLetter(Type));
}

void EnsureSentence(FString& S)
{
	S.TrimStartAndEndInline();
	if (S.IsEmpty()) {
		return;
	}
	const TCHAR Last = S[S.Len() - 1];
	if (Last != TEXT('.') && Last != TEXT('!') && Last != TEXT('?')) {
		S += TEXT(".");
	}
}

void CapitalizeLeadingProse(FString& S)
{
	for (int32 i = 0; i < S.Len(); ++i) {
		if (S[i] == TEXT('{')) {
			const int32 Close = S.Find(TEXT("}"), ESearchCase::CaseSensitive, ESearchDir::FromStart, i + 1);
			if (Close == INDEX_NONE) {
				return;
			}
			i = Close;
			continue;
		}
		if (FChar::IsAlpha(S[i])) {
			S[i] = FChar::ToUpper(S[i]);
			return;
		}
		if (!FChar::IsWhitespace(S[i])) {
			return;
		}
	}
}

FString TargetNoun(const tactics::TerritoryEffect& E)
{
	switch (E.board_target_kind) {
		case tactics::BoardTargetKind::Ally:
			return TEXT("an allied unit");
		case tactics::BoardTargetKind::Enemy:
			return TEXT("an enemy unit");
		case tactics::BoardTargetKind::Own:
			return TEXT("one of your units");
		default:
			return TEXT("a unit");
	}
}

FString NeutralCostPips(const int32 Count)
{
	FString Out;
	for (int32 i = 0; i < Count; ++i) {
		Out += TEXT("{N}");
	}
	return Out;
}

FString DescribeTerritoryEffect(const tactics::TerritoryEffect& E)
{
	if (E.effect_key.empty()) {
		return {};
	}
	if (E.effect_key == "scan") {
		return FString::Printf(TEXT("{GL:scan|Scan} %d"), TerritoryPayloadInt(E, "amount", 1));
	}
	if (E.effect_key == "grant_permanent_stat_growth") {
		const int32 Atk = TerritoryPayloadInt(E, "attack", TerritoryPayloadInt(E, "amount", 1));
		const int32 Hp = TerritoryPayloadInt(E, "health", TerritoryPayloadInt(E, "amount", 1));
		return FString::Printf(TEXT("permanently grant %s +%d {MELEE} and +%d {LIFE}"), *TargetNoun(E), Atk, Hp);
	}
	if (E.effect_key == "grant_next_damage_bonus") {
		const int32 Amt = TerritoryPayloadInt(E, "amount", 1);
		return FString::Printf(
			TEXT("{KW:boost|Boost} %s with +%d damage on its next attack or ability"), *TargetNoun(E), Amt);
	}
	if (E.effect_key == "optional_discard_draw") {
		return TEXT("you may discard a card, then draw a card");
	}
	if (E.effect_key == "draw_focus_spell_cards") {
		const int32 Amount = TerritoryPayloadInt(E, "amount", 1);
		const int32 MaxCost = TerritoryPayloadInt(E, "max_total_cost", 0);
		const FString Draw = Amount == 1 ? FString(TEXT("draw a focus spell"))
										 : FString::Printf(TEXT("draw %d focus spells"), Amount);
		if (MaxCost > 0) {
			return FString::Printf(TEXT("%s costing %s or less"), *Draw, *NeutralCostPips(MaxCost));
		}
		return Draw;
	}
	if (E.effect_key == "spawn_card_unit_deploy_zone") {
		FString CardName = TerritoryPayloadString(E, "card_key");
		if (!CardName.IsEmpty()) {
			if (const tactics::CardDefinition* Def = tactics::try_get_card_definition_ptr(TCHAR_TO_UTF8(*CardName))) {
				if (!Def->name.empty()) {
					CardName = StdToF(Def->name);
				}
			}
		}
		if (CardName.IsEmpty()) {
			CardName = TEXT("unit");
		}
		return FString::Printf(TEXT("spawn a %s in your deploy zone"), *CardName);
	}
	if (E.effect_key == "grant_player_base_bonus_health") {
		return FString::Printf(TEXT("your base gains %d bonus {LIFE}"), TerritoryPayloadInt(E, "amount", 1));
	}
	if (E.effect_key == "grant_player_base_max_health") {
		return FString::Printf(
			TEXT("permanently increase your base's max {LIFE} by %d"), TerritoryPayloadInt(E, "amount", 1));
	}
	FString Fallback = HumanizeEffectKey(E.effect_key);
	Fallback.ToLowerInline();
	if (E.requires_target) {
		Fallback += TEXT(" (choose a target)");
	}
	return Fallback;
}

FString FinishTerritoryClause(FString Clause, const bool bCapitalize)
{
	if (bCapitalize) {
		CapitalizeLeadingProse(Clause);
	}
	EnsureSentence(Clause);
	return Clause;
}

FString ComposeLandAbilityStrip(const tactics::TerritoryAbility& A)
{
	const FString Speed = FString::Printf(TEXT("{%s}, "), UTF8_TO_TCHAR(A.speed_token()));
	const FString Cost = EnergyCostBraceTokens(A.cost);
	FString Name = StdToF(A.name);
	if (Name.IsEmpty()) {
		Name = TEXT("Use Land");
	}

	TArray<FString> Clauses;
	const FString Produced = EnergyCostBraceTokens(A.energy_produced);
	if (!Produced.IsEmpty()) {
		if (A.produces_flux) {
			Clauses.Add(FString::Printf(TEXT("Add %s {GL:flux_energy|flux energy}."), *Produced));
		} else {
			Clauses.Add(FString::Printf(TEXT("Add %s."), *Produced));
		}
	}
	if (!A.effect.effect_key.empty()) {
		Clauses.Add(FinishTerritoryClause(DescribeTerritoryEffect(A.effect), true));
	}
	if (A.sacrifice_self) {
		Clauses.Add(TEXT("Sacrifice this territory."));
	}

	FString Strip = Speed;
	if (!Cost.IsEmpty()) {
		Strip += Cost + TEXT(", ");
	}
	Strip += Name;
	if (Clauses.Num() > 0) {
		Strip += TEXT(": ") + FString::Join(Clauses, TEXT(" "));
	} else {
		Strip += TEXT(".");
	}
	return Strip;
}

/** Card-style rules for one territory (offered or placed). Same tokens, glossary markers, and
 *  ability strips as unit/spell cards so the detail panel formats and hovers identically. */
FString ComposeTerritoryRules(const tactics::EnergyZone& Z)
{
	TArray<FString> Parts;
	if (Z.is_basic && Z.color) {
		Parts.Add(FString::Printf(TEXT("{GL:basic_territory|Basic} %s territory."), *EnergyPip(*Z.color)));
	} else if (Z.color) {
		Parts.Add(FString::Printf(TEXT("%s territory."), *EnergyPip(*Z.color)));
	}
	if (Z.enters_depleted) {
		Parts.Add(TEXT("Enters {GL:depleted|depleted}."));
	}
	for (const tactics::GroundworkTrigger& G : Z.groundwork) {
		const FString Pip = EnergyPip(G.color);
		TArray<FString> Clauses;
		if (G.ignore_depleted) {
			Clauses.Add(TEXT("this land enters ready instead of {GL:depleted|depleted}"));
		}
		if (G.destroy_if_unmet) {
			Clauses.Add(FString::Printf(
				TEXT("if the previous territory you conquered was not a {GL:basic_territory|basic} %s territory, destroy this land"),
				*Pip));
		}
		if (G.effect) {
			Clauses.Add(DescribeTerritoryEffect(*G.effect));
		}
		if (Clauses.Num() == 0) {
			Clauses.Add(FString::Printf(
				TEXT("if the previous territory you conquered was a {GL:basic_territory|basic} %s territory"), *Pip));
		}
		FString Body = FString::Join(Clauses, TEXT("; "));
		Parts.Add(FString::Printf(
			TEXT("{GL:groundwork|Groundwork} %s: %s"), *Pip, *FinishTerritoryClause(MoveTemp(Body), true)));
	}
	for (const tactics::TerritoryEffect& E : Z.enter_effects) {
		Parts.Add(FString::Printf(
			TEXT("When conquered: %s"), *FinishTerritoryClause(DescribeTerritoryEffect(E), true)));
	}
	const FString Passive = EnergyCostBraceTokens(Z.energy_produced);
	if (!Passive.IsEmpty()) {
		Parts.Add(FString::Printf(TEXT("Produces %s each turn."), *Passive));
	}
	if (Z.land_abilities.size() > 1) {
		Parts.Add(TEXT("This land has 1 shared use per turn across its abilities."));
	} else if (Z.land_abilities.size() == 1) {
		Parts.Add(TEXT("This land has 1 use per turn."));
	}
	for (const tactics::TerritoryAbility& A : Z.land_abilities) {
		Parts.Add(ComposeLandAbilityStrip(A));
	}
	if (Parts.Num() == 0) {
		Parts.Add(TEXT("A territory with no special abilities."));
	}
	return FString::Join(Parts, TEXT(" "));
}
}  // namespace

bool UTacticsMatchSubsystem::TryGetEnergyZoneChoiceDetail(int32 Index1Based, FString& OutName, FString& OutRules,
	FString& OutArtId) const
{
	FString Produces;
	if (!TryGetEnergyZoneChoiceUi(Index1Based, OutName, Produces, OutArtId)) {
		return false;
	}
	// TryGetEnergyZoneChoiceUi already validated phase / current player / bounds, so this is safe.
	const auto Cp = Game->turn_manager.current_player();
	const tactics::EnergyZone& Z =
		Game->turn_manager.pending_energy_choices.at(*Cp)[static_cast<size_t>(Index1Based - 1)];
	OutRules = ComposeTerritoryRules(Z);
	return true;
}

bool UTacticsMatchSubsystem::TryGetPlacedTerritoryDetail(int32 TerritoryIndex, FString& OutName, FString& OutRules,
	FString& OutArtId) const
{
	if (!Game || TerritoryIndex < 0) {
		return false;
	}
	const auto It = Game->players_energy_zones.find(ControlledPlayer);
	if (It == Game->players_energy_zones.end() || TerritoryIndex >= static_cast<int32>(It->second.size())) {
		return false;
	}
	const tactics::EnergyZone& Z = It->second[static_cast<size_t>(TerritoryIndex)];
	OutName = StdToF(Z.name);
	OutArtId = Z.art_id.empty() ? FString{} : FString(UTF8_TO_TCHAR(Z.art_id.c_str()));
	OutRules = ComposeTerritoryRules(Z);
	return true;
}

int32 UTacticsMatchSubsystem::GetControlledTerritoryCount() const
{
	if (!Game) {
		return 0;
	}
	const auto It = Game->players_energy_zones.find(ControlledPlayer);
	return It == Game->players_energy_zones.end() ? 0 : static_cast<int32>(It->second.size());
}

bool UTacticsMatchSubsystem::TryGetTerritoryUi(int32 TerritoryIndex, FString& OutName, bool& bOutDepleted,
	bool& bOutUseAvailable, FString& OutArtId) const
{
	if (!Game || TerritoryIndex < 0) {
		return false;
	}
	const auto It = Game->players_energy_zones.find(ControlledPlayer);
	if (It == Game->players_energy_zones.end() || TerritoryIndex >= static_cast<int32>(It->second.size())) {
		return false;
	}
	const tactics::EnergyZone& Z = It->second[static_cast<size_t>(TerritoryIndex)];
	OutName = StdToF(Z.name);
	OutArtId = Z.art_id.empty() ? FString{} : FString(UTF8_TO_TCHAR(Z.art_id.c_str()));
	bOutDepleted = Z.depleted;
	bOutUseAvailable = Z.land_use_available > 0;
	return true;
}

int32 UTacticsMatchSubsystem::GetTerritoryLandAbilityCount(int32 TerritoryIndex) const
{
	if (!Game || TerritoryIndex < 0) {
		return 0;
	}
	const auto It = Game->players_energy_zones.find(ControlledPlayer);
	if (It == Game->players_energy_zones.end() || TerritoryIndex >= static_cast<int32>(It->second.size())) {
		return 0;
	}
	return static_cast<int32>(It->second[static_cast<size_t>(TerritoryIndex)].land_abilities.size());
}

bool UTacticsMatchSubsystem::IsControlledPlayerAwaitingTerritoryTarget() const
{
	return Game && Game->IsPendingTerritoryTargetForPlayer(ControlledPlayer);
}

FString UTacticsMatchSubsystem::GetTerritoryTargetPrompt() const
{
	if (!Game) {
		return FString();
	}
	const std::string Key = Game->pending_territory_target_effect_key(ControlledPlayer);
	if (Key.empty()) {
		return FString();
	}
	if (Key == "grant_permanent_stat_growth") {
		return TEXT("Territory: click a unit to grant +1/+1 (or Skip).");
	}
	if (Key == "grant_next_damage_bonus") {
		return TEXT("Territory: click a unit to grant a damage boost (or Skip).");
	}
	return FString::Printf(TEXT("Territory: click a target unit for %s (or Skip)."), *StdToF(Key));
}

bool UTacticsMatchSubsystem::TryGetLandAbilityUi(int32 TerritoryIndex, int32 AbilityIndex, FString& OutName,
	FString& OutSpeedToken, FString& OutCostLine, FString& OutEffectLine, bool& bOutUsableNow, bool& bOutNeedsTarget) const
{
	if (!Game || TerritoryIndex < 0 || AbilityIndex < 0) {
		return false;
	}
	const auto It = Game->players_energy_zones.find(ControlledPlayer);
	if (It == Game->players_energy_zones.end() || TerritoryIndex >= static_cast<int32>(It->second.size())) {
		return false;
	}
	const tactics::EnergyZone& Z = It->second[static_cast<size_t>(TerritoryIndex)];
	if (AbilityIndex >= static_cast<int32>(Z.land_abilities.size())) {
		return false;
	}
	const tactics::TerritoryAbility& Ab = Z.land_abilities[static_cast<size_t>(AbilityIndex)];
	OutName = Ab.name.empty() ? FString(TEXT("Use Land")) : StdToF(Ab.name);
	OutSpeedToken = FString::Printf(TEXT("{%s}"), UTF8_TO_TCHAR(Ab.speed_token()));

	OutCostLine = EnergyCostBraceTokens(Ab.cost);

	FString Eff = EnergyCostBraceTokens(Ab.energy_produced);
	if (!Eff.IsEmpty() && Ab.produces_flux) {
		Eff += TEXT(" flux");
	}
	if (!Ab.effect.effect_key.empty()) {
		FString EffectProse = DescribeTerritoryEffect(Ab.effect);
		CapitalizeLeadingProse(EffectProse);
		if (!Eff.IsEmpty()) {
			Eff += TEXT(", ");
		}
		Eff += EffectProse;
	}
	if (Ab.sacrifice_self) {
		if (!Eff.IsEmpty()) {
			Eff += TEXT(", ");
		}
		Eff += TEXT("sacrifice this territory");
	}
	OutEffectLine = Eff;

	const bool bAfford = Ab.cost.empty() || Game->turn_manager.can_afford(*Game, ControlledPlayer, Ab.cost);
	// Special (effect) abilities are channeled - only usable on the controller's own main phase. Energy
	// abilities are blazing (any phase the player holds a charge; resolve immediately). Mirrors use_land.
	bool bPhaseOk = true;
	if (Ab.is_special_ability()) {
		const auto Cp = Game->turn_manager.current_player();
		bPhaseOk = Cp && *Cp == ControlledPlayer
			&& (Game->turn_manager.current_phase == tactics::TurnPhase::Main
				|| Game->turn_manager.current_phase == tactics::TurnPhase::SecondMain);
	}
	bOutUsableNow = (Z.land_use_available > 0) && bAfford && bPhaseOk;
	bOutNeedsTarget = Ab.effect.requires_target;
	return true;
}

bool UTacticsMatchSubsystem::TryChooseEnergyZone(int32 Index1Based, FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	const auto Cp = Game->turn_manager.current_player();
	if (!Cp || *Cp != ControlledPlayer) {
		OutMessage = TEXT("Not your territory choice.");
		return false;
	}
	const auto It = Game->turn_manager.pending_energy_choices.find(*Cp);
	if (It == Game->turn_manager.pending_energy_choices.end() || It->second.empty()) {
		OutMessage = TEXT("No territory choices.");
		return false;
	}
	const int32 N = static_cast<int32>(It->second.size());
	if (Index1Based < 1 || Index1Based > N) {
		OutMessage = TEXT("Invalid territory index.");
		return false;
	}
	const tactics::ActionResult R = Game->choose_energy_zone(ControlledPlayer, Index1Based - 1);
	OutMessage = R.ok ? TEXT("Territory placed.") : StdToF(R.message);
	BroadcastRefresh();
	return R.ok;
}

bool UTacticsMatchSubsystem::GetCliCellTokens(int WorldX, int WorldY, FString& OutCol, FString& OutRow) const
{
	if (!Game) {
		return false;
	}
	const tactics::BoardCellBounds B = MergedBounds(*Game);
	if (!InBoundsWorld(B, WorldX, WorldY)) {
		return false;
	}
	const int Col1 = WorldX - B.min_x + 1;
	const int Row1 = WorldY - B.min_y + 1;
	OutCol = FString::FromInt(Col1);
	OutRow = FString::FromInt(Row1);
	return true;
}

