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


// ── Combat Visualization ──────────────────────────────────────────────────────


bool UTacticsMatchSubsystem::IsCombatVisualizationPaused() const
{
	return Game && Game->is_combat_visualization_paused();
}

bool UTacticsMatchSubsystem::HasPendingAttacksInQueue() const
{
	return Game && Game->has_pending_attacks_in_queue();
}

bool UTacticsMatchSubsystem::IsDefenseReactionPhase() const
{
	if (!Game) {
		return false;
	}
	const auto P = Game->turn_manager.current_phase;
	return P == tactics::TurnPhase::Defense || P == tactics::TurnPhase::BonusDefense;
}

void UTacticsMatchSubsystem::AutoPassDefenseWindowUntilClosed(FString& OutLog)
{
	OutLog.Reset();
	if (!Game) {
		return;
	}
	const int32 MaxPasses = FMath::Max(4, GetMatchPlayerCount() * 4);
	for (int32 I = 0; I < MaxPasses && IsDefenseReactionPhase(); ++I) {
		const int32 Pri = GetReactionWindowPriorityPlayerId();
		if (Pri <= 0) {
			break;
		}
		FString PassOut;
		ExecMasterCliLineAsPlayer(Pri, TEXT("pass"), PassOut, false);
		if (!PassOut.IsEmpty()) {
			if (!OutLog.IsEmpty()) {
				OutLog += TEXT("\n");
			}
			OutLog += PassOut;
		}
		if (IsCombatVisualizationPaused()) {
			break;
		}
	}
}

bool UTacticsMatchSubsystem::CaptureCombatVizPauseEncounter(FCombatEncounter& OutEncounter) const
{
	if (!Game) {
		return false;
	}
	tactics::GameState::AttackDeclaration Decl;
	if (!Game->try_get_pending_combat_visualization_attack(Decl)) {
		return false;
	}
	const auto AttIt = Game->board.all_entities_map.find(Decl.attacker_id);
	if (AttIt == Game->board.all_entities_map.end() || !AttIt->second) {
		return false;
	}
	const auto DefPtr = Game->board.entity_at(Decl.target_x, Decl.target_y);
	if (!DefPtr) {
		return false;
	}
	OutEncounter.AttackerBefore = SnapshotEntity(*Game, *AttIt->second);
	OutEncounter.DefenderBefore = SnapshotEntity(*Game, *DefPtr);
	OutEncounter.AttackerAfter = OutEncounter.AttackerBefore;
	OutEncounter.DefenderAfter = OutEncounter.DefenderBefore;
	OutEncounter.AttackDamage = 0;
	OutEncounter.CounterDamage = 0;
	// This is the PRE-ROLL matchup (before the dice are resolved).  OutEncounter is a reused member
	// (ActiveCombatEncounter) that the previous fight's resolved view left as bIsPostResolution=true,
	// so it MUST be cleared here - otherwise the matchup inherits the stale flag and plays the
	// attack→counter lunge with zero damage instead of showing a static preview.
	OutEncounter.bIsPostResolution = false;
	PopulateBattlefieldUnits(*Game, OutEncounter);
	return true;
}

bool UTacticsMatchSubsystem::ResumeCombatVisualization(FString& OutMessage)
{
	if (!Game) {
		OutMessage = TEXT("No match.");
		return false;
	}
	std::ostringstream Oss;
	const bool Quit = tactics::dispatch_master_cli_line(*Game, ControlledPlayer, Selected, "combat_viz_resume", Oss, {}, nullptr);
	OutMessage = StdToF(Oss.str());
	BroadcastRefresh();
	return !Quit;
}

void UTacticsMatchSubsystem::FillCombatEncountersAfter(TArray<FCombatEncounter>& InOutEncounters) const
{
	if (!Game) {
		return;
	}
	for (FCombatEncounter& Enc : InOutEncounters) {
		const std::string AttId = TCHAR_TO_UTF8(*Enc.AttackerBefore.EntityId);
		const std::string DefId = TCHAR_TO_UTF8(*Enc.DefenderBefore.EntityId);

		// Attacker after
		const auto AttIt = Game->board.all_entities_map.find(AttId);
		if (AttIt != Game->board.all_entities_map.end() && AttIt->second) {
			Enc.AttackerAfter = SnapshotEntity(*Game, *AttIt->second);
		} else {
			// Unit was killed and removed from board - copy before state with hp=0
			Enc.AttackerAfter = Enc.AttackerBefore;
			Enc.AttackerAfter.Hp = 0;
			Enc.AttackerAfter.bAlive = false;
		}

		// Defender after
		const auto DefIt = Game->board.all_entities_map.find(DefId);
		if (DefIt != Game->board.all_entities_map.end() && DefIt->second) {
			Enc.DefenderAfter = SnapshotEntity(*Game, *DefIt->second);
		} else {
			Enc.DefenderAfter = Enc.DefenderBefore;
			Enc.DefenderAfter.Hp = 0;
			Enc.DefenderAfter.bAlive = false;
		}

		tactics::GameState::CombatVizEncounterResult VizResult{};
		if (Game->try_get_last_combat_viz_encounter_result(VizResult)) {
			Enc.AttackDamage    = FMath::Max(0, VizResult.attack_damage);
			Enc.CounterDamage   = FMath::Max(0, VizResult.counter_damage);
			Enc.bAttackWasCrit  = VizResult.attack_was_crit;
			Enc.bCounterWasCrit = VizResult.counter_was_crit;
		} else {
			Enc.AttackDamage    = FMath::Max(0, Enc.DefenderBefore.Hp - Enc.DefenderAfter.Hp);
			Enc.CounterDamage   = FMath::Max(0, Enc.AttackerBefore.Hp - Enc.AttackerAfter.Hp);
			Enc.bAttackWasCrit  = false;
			Enc.bCounterWasCrit = false;
		}
		// Mark as the resolved pass so the visualization can distinguish it from
		// the pre-roll static display without relying on damage values being non-zero.
		Enc.bIsPostResolution = true;
		// Re-populate the battlefield snapshot from the post-combat board state so
		// dead units are removed and survivor positions reflect any movement.
		PopulateBattlefieldUnits(*Game, Enc);
	}
}

bool UTacticsMatchSubsystem::HasPendingPassiveAttackVizEvents() const
{
	return Game && Game->has_pending_passive_attack_viz_events();
}

bool UTacticsMatchSubsystem::TryConsumePassiveAttackVizEvent(FCombatEncounter& OutEncounter)
{
	if (!Game) {
		return false;
	}
	tactics::GameState::PassiveAttackVizEvent Evt;
	if (!Game->try_consume_passive_attack_viz_event(Evt)) {
		return false;
	}

	// Build the encounter from the event.  The damage is already applied (post-hoc viz).
	const auto SrcIt = Game->board.all_entities_map.find(Evt.source_entity_id);
	if (SrcIt == Game->board.all_entities_map.end() || !SrcIt->second) {
		return false;
	}
	const auto TgtIt = Game->board.all_entities_map.find(Evt.target_entity_id);

	OutEncounter = FCombatEncounter{};
	OutEncounter.AttackerBefore = SnapshotEntity(*Game, *SrcIt->second);
	OutEncounter.AttackerAfter  = OutEncounter.AttackerBefore;  // attacker unchanged
	OutEncounter.CounterDamage  = 0;                            // buildings don't counter

	if (TgtIt != Game->board.all_entities_map.end() && TgtIt->second) {
		OutEncounter.DefenderBefore = SnapshotEntity(*Game, *TgtIt->second);
		// Reconstruct the before-HP using the logged damage delta.
		OutEncounter.DefenderBefore.Hp += Evt.damage_to_primary;
		OutEncounter.DefenderAfter  = SnapshotEntity(*Game, *TgtIt->second);
	} else {
		// Target was killed - snapshot what we know; mark as dead.
		OutEncounter.DefenderBefore = FCombatUnitSnapshot{};
		OutEncounter.DefenderBefore.EntityId = FString(UTF8_TO_TCHAR(Evt.target_entity_id.c_str()));
		OutEncounter.DefenderBefore.Hp = Evt.damage_to_primary;  // best guess for before-HP
		OutEncounter.DefenderAfter  = OutEncounter.DefenderBefore;
		OutEncounter.DefenderAfter.Hp = 0;
		OutEncounter.DefenderAfter.bAlive = false;
	}
	OutEncounter.AttackDamage    = Evt.damage_to_primary;
	OutEncounter.bIsPostResolution = true;
	PopulateBattlefieldUnits(*Game, OutEncounter);
	return true;
}
