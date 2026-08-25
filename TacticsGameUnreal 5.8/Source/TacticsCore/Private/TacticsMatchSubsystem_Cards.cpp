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
#include "tactics/entities/player_base.hpp"
#include "tactics/combat/directional_area.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/common/types.hpp"
#include "tactics/core/board_target_policy.hpp"  // entity_is_board_unit (IWYU: was transitively included)
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


void UTacticsMatchSubsystem::GetRulesTextGlossaryEntries(const FString& Rules, TArray<FTacticsCardGlossaryEntry>& OutEntries) const
{
	OutEntries.Reset();
	if (Rules.IsEmpty()) {
		return;
	}
	std::vector<tactics::CardGlossaryEntry> Collected;
	tactics::collect_glossary_from_rules_text(TCHAR_TO_UTF8(*Rules), Collected, bShowAdvancedCardText);
	CopyGlossaryToUe(Collected, OutEntries);
}

void UTacticsMatchSubsystem::GetHandCardGlossaryEntries(int32 Index1Based, TArray<FTacticsCardGlossaryEntry>& OutEntries) const
{
	OutEntries.Reset();
	if (!Game || Index1Based < 1) {
		return;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	FillCardGlossaryFromDefinition(Def, OutEntries, bShowAdvancedCardText);
}

void UTacticsMatchSubsystem::GetReservesCardGlossaryEntries(int32 Index1Based, TArray<FTacticsCardGlossaryEntry>& OutEntries) const
{
	OutEntries.Reset();
	if (!Game || Index1Based < 1) {
		return;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	FillCardGlossaryFromDefinition(Def, OutEntries, bShowAdvancedCardText);
}

void UTacticsMatchSubsystem::GetSelectedUnitCardGlossaryEntries(TArray<FTacticsCardGlossaryEntry>& OutEntries) const
{
	OutEntries.Reset();
	if (!Game || !Selected) {
		return;
	}
	if (tactics::entity_is_base(*Selected)) {
		std::vector<tactics::CardGlossaryEntry> Collected;
		tactics::collect_player_base_innate_glossary_entry(Collected, bShowAdvancedCardText);
		const std::string Rules = tactics::format_player_base_card_rules(bShowAdvancedCardText);
		tactics::collect_glossary_from_rules_text(Rules, Collected, bShowAdvancedCardText);
		for (const tactics::CardGlossaryEntry& Entry : Collected) {
			if (Entry.name.empty() || Entry.body.empty()) {
				continue;
			}
			FTacticsCardGlossaryEntry Row;
			Row.Key = UTF8_TO_TCHAR(Entry.dedupe_key.c_str());
			Row.Name = UTF8_TO_TCHAR(Entry.name.c_str());
			Row.Body = UTF8_TO_TCHAR(Entry.body.c_str());
			OutEntries.Add(std::move(Row));
		}
		return;
	}
	if (Selected->source_card_id.empty()) {
		return;
	}
	const tactics::CardInstance* Inst = FindCardInstanceForEntity(*Game, *Selected);
	const tactics::CardDefinition* Def = nullptr;
	if (Inst) {
		Def = tactics::try_get_card_definition_ptr(Inst->definition_id);
	}
	if (!Def) {
		Def = tactics::try_get_card_definition_ptr(Selected->source_card_id);
	}
	FillCardGlossaryFromDefinition(Def, OutEntries, bShowAdvancedCardText);
	std::vector<tactics::CardGlossaryEntry> GainedKeywords;
	tactics::collect_entity_gained_keyword_glossary_entries(*Selected, Def, GainedKeywords, bShowAdvancedCardText);
	for (const tactics::CardGlossaryEntry& Entry : GainedKeywords) {
		if (Entry.name.empty() || Entry.body.empty()) {
			continue;
		}
		const FString DedupeKey = UTF8_TO_TCHAR(Entry.dedupe_key.c_str());
		if (OutEntries.ContainsByPredicate([&](const FTacticsCardGlossaryEntry& Row) {
				return Row.Key.Equals(DedupeKey, ESearchCase::IgnoreCase);
			})) {
			continue;
		}
		FTacticsCardGlossaryEntry Row;
		Row.Key = DedupeKey;
		Row.Name = UTF8_TO_TCHAR(Entry.name.c_str());
		Row.Body = UTF8_TO_TCHAR(Entry.body.c_str());
		OutEntries.Add(std::move(Row));
	}
	OutEntries.RemoveAll([](const FTacticsCardGlossaryEntry& Entry) {
		return Entry.Name.Equals(TEXT("Stockpile"), ESearchCase::IgnoreCase);
	});
}

void UTacticsMatchSubsystem::GetSelectedUnitActiveEffectEntries(TArray<FTacticsActiveEffectEntry>& OutEntries) const
{
	OutEntries.Reset();
	if (!Selected) {
		return;
	}
	std::vector<tactics::CardGlossaryEntry> Collected;
	tactics::collect_entity_active_glossary_entries(*Selected, Collected, bShowAdvancedCardText);
	OutEntries.Reserve(static_cast<int32>(Collected.size()));
	for (const tactics::CardGlossaryEntry& Entry : Collected) {
		if (Entry.name.empty() || Entry.body.empty()) {
			continue;
		}
		FTacticsActiveEffectEntry Row;
		Row.Key = UTF8_TO_TCHAR(Entry.dedupe_key.c_str());
		Row.Name = UTF8_TO_TCHAR(Entry.name.c_str());
		Row.Body = UTF8_TO_TCHAR(Entry.body.c_str());
		Row.bNegative = Entry.is_negative;
		Row.bPositive = Entry.is_positive;
		OutEntries.Add(std::move(Row));
	}
}

int32 UTacticsMatchSubsystem::GetControlledHandCount() const
{
	if (!Game) {
		return 0;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return 0;
	}
	return static_cast<int32>(It->second->size());
}

int32 UTacticsMatchSubsystem::GetControlledReservesCount() const
{
	if (!Game) {
		return 0;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return 0;
	}
	return static_cast<int32>(It->second.reserves.size());
}

bool UTacticsMatchSubsystem::TryGetReservesCardUi(int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutCostLine,
	FString& OutRulesLine) const
{
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardInstanceId InstId = Reserves[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, InstId);
	if (!Def) {
		return false;
	}
	return fill_card_ui_strings(*Def, MatchCardInst(*Game, ViewPid, InstId), OutName, OutTypeTag, OutCostLine, OutRulesLine, bShowAdvancedCardText);
}

int32 UTacticsMatchSubsystem::GetReservesCardTotalCost(int32 Index1Based) const
{
	if (!Game || Index1Based < 1) {
		return -1;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return -1;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return -1;
	}
	const tactics::CardInstanceId InstId = Reserves[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, InstId);
	if (!Def) {
		return -1;
	}
	int32 Total = 0;
	for (const auto& Pr : Def->energy_cost) {
		Total += Pr.second;
	}
	return Total;
}

bool UTacticsMatchSubsystem::TryGetReservesCardArtId(int32 Index1Based, FString& OutArtId) const
{
	OutArtId.Reset();
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || Def->art_id.empty()) {
		return false;
	}
	OutArtId = UTF8_TO_TCHAR(Def->art_id.c_str());
	return true;
}

bool UTacticsMatchSubsystem::TryGetReservesSpellRequiresFocusCaster(int32 Index1Based, bool& bOutRequiresFocus) const
{
	bOutRequiresFocus = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutRequiresFocus = tactics::spell_requires_focus_caster(*Def);
	return true;
}

bool UTacticsMatchSubsystem::TryGetReservesSpellUsesDirectionalAim(int32 Index1Based, bool& bOutUsesDirectionalAim) const
{
	bOutUsesDirectionalAim = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutUsesDirectionalAim = tactics::effect_uses_directional_aim(tactics::definition_spell(*Def).effect_key);
	return true;
}

bool UTacticsMatchSubsystem::TryGetReservesSpellUsesPushDirectionAim(int32 Index1Based, bool& bOutUsesPushDirectionAim) const
{
	bOutUsesPushDirectionAim = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutUsesPushDirectionAim = tactics::effect_key_uses_push_direction_aim(tactics::definition_spell(*Def).effect_key);
	return true;
}

bool UTacticsMatchSubsystem::TryGetReservesSpellRequiresStackTarget(int32 Index1Based, bool& bOutRequiresStackTarget) const
{
	bOutRequiresStackTarget = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutRequiresStackTarget = tactics::definition_spell_requires_stack_target(*Def);
	return true;
}

bool UTacticsMatchSubsystem::TryGetReservesSpellRequiresPlayerSeatTarget(int32 Index1Based, bool& bOutRequiresPlayerSeatTarget) const
{
	bOutRequiresPlayerSeatTarget = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutRequiresPlayerSeatTarget = tactics::definition_spell_requires_player_seat_target(*Def);
	return true;
}

bool UTacticsMatchSubsystem::TryGetReservesSpellRequiresBoardCell(int32 Index1Based, bool& bOutRequiresCell) const
{
	bOutRequiresCell = true;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutRequiresCell = tactics::definition_spell_requires_mandatory_board_cell(*Def);
	return true;
}

int32 UTacticsMatchSubsystem::GetPlayerDiscardCount(const int32 PlayerId) const
{
	if (!Game || PlayerId <= 0) {
		return 0;
	}
	const auto It = Game->players_decks.find(PlayerId);
	if (It == Game->players_decks.end()) {
		return 0;
	}
	return static_cast<int32>(It->second.discard.size());
}

bool UTacticsMatchSubsystem::TryGetPlayerDiscardCardUi(const int32 PlayerId, const int32 Index1Based, FString& OutName, FString& OutTypeTag,
	FString& OutRulesLine) const
{
	if (!Game || PlayerId <= 0 || Index1Based < 1) {
		return false;
	}
	const auto It = Game->players_decks.find(PlayerId);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& pile = It->second.discard;
	if (Index1Based > static_cast<int32>(pile.size())) {
		return false;
	}
	const tactics::CardInstanceId InstId = pile[static_cast<size_t>(pile.size() - static_cast<size_t>(Index1Based))];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, PlayerId, InstId);
	if (!Def) {
		return false;
	}
	FString CostUnused;
	return fill_card_ui_strings(*Def, MatchCardInst(*Game, PlayerId, InstId), OutName, OutTypeTag, CostUnused, OutRulesLine, bShowAdvancedCardText);
}

int32 UTacticsMatchSubsystem::GetControlledDiscardCount() const
{
	return GetPlayerDiscardCount(ControlledPlayer);
}

bool UTacticsMatchSubsystem::TryGetDiscardCardUi(int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutRulesLine) const
{
	return TryGetPlayerDiscardCardUi(ControlledPlayer, Index1Based, OutName, OutTypeTag, OutRulesLine);
}

int32 UTacticsMatchSubsystem::GetPlayerPurgatoryCount(const int32 PlayerId) const
{
	if (!Game || PlayerId <= 0) {
		return 0;
	}
	const auto It = Game->players_decks.find(PlayerId);
	if (It == Game->players_decks.end()) {
		return 0;
	}
	return static_cast<int32>(It->second.purgatory.size());
}

bool UTacticsMatchSubsystem::TryGetPlayerPurgatoryCardUi(const int32 PlayerId, const int32 Index1Based, FString& OutName, FString& OutTypeTag,
	FString& OutRulesLine) const
{
	if (!Game || PlayerId <= 0 || Index1Based < 1) {
		return false;
	}
	const auto It = Game->players_decks.find(PlayerId);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& pile = It->second.purgatory;
	if (Index1Based > static_cast<int32>(pile.size())) {
		return false;
	}
	const tactics::CardInstanceId InstId = pile[static_cast<size_t>(pile.size() - static_cast<size_t>(Index1Based))];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, PlayerId, InstId);
	if (!Def) {
		return false;
	}
	FString CostUnused;
	return fill_card_ui_strings(*Def, MatchCardInst(*Game, PlayerId, InstId), OutName, OutTypeTag, CostUnused, OutRulesLine, bShowAdvancedCardText);
}

int32 UTacticsMatchSubsystem::GetControlledPurgatoryCount() const
{
	return GetPlayerPurgatoryCount(ControlledPlayer);
}

bool UTacticsMatchSubsystem::TryGetPurgatoryCardUi(int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutRulesLine) const
{
	return TryGetPlayerPurgatoryCardUi(ControlledPlayer, Index1Based, OutName, OutTypeTag, OutRulesLine);
}

bool UTacticsMatchSubsystem::TryGetHandCardUi(int32 Index1Based, FString& OutName, FString& OutTypeTag, FString& OutCostLine, FString& OutRulesLine) const
{
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardInstanceId InstId = (*Hand)[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, InstId);
	if (!Def) {
		return false;
	}
	return fill_card_ui_strings(*Def, MatchCardInst(*Game, ViewPid, InstId), OutName, OutTypeTag, OutCostLine, OutRulesLine, bShowAdvancedCardText);
}

bool UTacticsMatchSubsystem::TryGetDetailCardAbilityMetadataStrip(int32 Index1Based, bool bFromReserves,
	const FString& AbilityBlockName, int32 AbilityBlockIndex, FString& OutMetadataStrip) const
{
	OutMetadataStrip.Reset();
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const tactics::CardDefinition* def = nullptr;
	if (bFromReserves) {
		const auto it = Game->players_decks.find(ViewPid);
		if (it == Game->players_decks.end()) {
			return false;
		}
		const std::vector<tactics::CardInstanceId>& reserves = it->second.reserves;
		if (Index1Based > static_cast<int32>(reserves.size())) {
			return false;
		}
		def = MatchCardDef(*Game, ViewPid, reserves[static_cast<size_t>(Index1Based - 1)]);
	} else {
		const auto it = Game->players_hands.find(ViewPid);
		if (it == Game->players_hands.end() || it->second == nullptr) {
			return false;
		}
		if (Index1Based > static_cast<int32>(it->second->size())) {
			return false;
		}
		def = MatchCardDef(*Game, ViewPid, (*it->second)[static_cast<size_t>(Index1Based - 1)]);
	}
	if (!def || !def->unit) {
		return false;
	}
	const std::vector<tactics::AbilitySpec> specs = BuildOrderedCardAbilitySpecs(*def);
	if (specs.empty()) {
		return false;
	}
	const tactics::AbilitySpec* matched = ResolveCardAbilitySpecForDetailBlock(specs, AbilityBlockName, AbilityBlockIndex);
	if (!matched) {
		return false;
	}
	OutMetadataStrip = AbilityMetadataStripFromSpec(*matched);
	return !OutMetadataStrip.IsEmpty();
}

bool UTacticsMatchSubsystem::TryGetDetailCardAbilityUses(int32 Index1Based, bool bFromReserves, const FString& AbilityBlockName,
	int32 AbilityBlockIndex, int32& OutUsesRemaining, int32& OutUsesMax) const
{
	OutUsesRemaining = 0;
	OutUsesMax = 0;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const tactics::CardDefinition* def = nullptr;
	if (bFromReserves) {
		const auto it = Game->players_decks.find(ViewPid);
		if (it == Game->players_decks.end()) {
			return false;
		}
		const std::vector<tactics::CardInstanceId>& reserves = it->second.reserves;
		if (Index1Based > static_cast<int32>(reserves.size())) {
			return false;
		}
		def = MatchCardDef(*Game, ViewPid, reserves[static_cast<size_t>(Index1Based - 1)]);
	} else {
		const auto it = Game->players_hands.find(ViewPid);
		if (it == Game->players_hands.end() || it->second == nullptr) {
			return false;
		}
		if (Index1Based > static_cast<int32>(it->second->size())) {
			return false;
		}
		def = MatchCardDef(*Game, ViewPid, (*it->second)[static_cast<size_t>(Index1Based - 1)]);
	}
	if (!def || !def->unit) {
		return false;
	}
	const std::vector<tactics::AbilitySpec> specs = BuildOrderedCardAbilitySpecs(*def);
	if (specs.empty()) {
		return false;
	}
	const tactics::AbilitySpec* matched = ResolveCardAbilitySpecForDetailBlock(specs, AbilityBlockName, AbilityBlockIndex);
	if (!matched) {
		return false;
	}
	OutUsesMax = static_cast<int32>(tactics::ability_effective_uses_per_turn(*matched));
	if (OutUsesMax <= 0) {
		return false;
	}
	OutUsesRemaining = OutUsesMax;
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandCardStatTokens(int32 Index1Based, FString& OutStats) const
{
	OutStats.Reset();
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def) {
		return false;
	}
	OutStats = build_unit_stat_tokens(*Def, bShowAdvancedCardText);
	return !OutStats.IsEmpty();
}

bool UTacticsMatchSubsystem::TryGetReservesCardStatTokens(int32 Index1Based, FString& OutStats) const
{
	OutStats.Reset();
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def) {
		return false;
	}
	OutStats = build_unit_stat_tokens(*Def, bShowAdvancedCardText);
	return !OutStats.IsEmpty();
}

int32 UTacticsMatchSubsystem::GetHandCardTotalCost(int32 Index1Based) const
{
	if (!Game || Index1Based < 1) {
		return -1;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return -1;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int32>(Hand->size())) {
		return -1;
	}
	const tactics::CardInstanceId InstId = (*Hand)[static_cast<size_t>(Index1Based - 1)];
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, InstId);
	if (!Def) {
		return -1;
	}
	return tactics::definition_total_energy_cost(*Def);
}

bool UTacticsMatchSubsystem::TryGetHandCardArtId(int32 Index1Based, FString& OutArtId) const
{
	OutArtId.Reset();
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || Def->art_id.empty()) {
		return false;
	}
	OutArtId = UTF8_TO_TCHAR(Def->art_id.c_str());
	return true;
}

bool UTacticsMatchSubsystem::TryGetSelectedUnitCardStatTokens(FString& OutStats) const
{
	OutStats.Reset();
	if (!Selected) {
		return false;
	}
	if (tactics::entity_is_base(*Selected)) {
		const tactics::Unit& U = *Selected;
		const int32 EffectiveHealth = tactics::entity_effective_base_health(*Selected);
		const tactics::DamageRange RangedRange = tactics::unit_effective_ranged_damage_range(U);
		const tactics::PassiveStatGrant Temp = tactics::temporary_stat_grants_for_entity(*Selected);
		const int32 RangeDisplay = U.ranged_range + Temp.bonus_ranged_range;
		TArray<FString> Parts;
		Parts.Add(FString::Printf(TEXT("{LIFE} %d/%d"), Selected->current_health, EffectiveHealth));
		if (RangedRange.max > 0 || RangedRange.min > 0) {
			FString RangedGroup;
			if (RangedRange.min == RangedRange.max) {
				RangedGroup = FString::Printf(TEXT("{RANGED} %d"), RangedRange.max);
			} else {
				RangedGroup = FString::Printf(TEXT("{RANGED} %d-%d"), RangedRange.min, RangedRange.max);
			}
			if (RangeDisplay > 0) {
				RangedGroup += FString::Printf(TEXT(" {RANGE} %d"), RangeDisplay);
			}
			Parts.Add(RangedGroup);
		}
		OutStats = FString::Join(Parts, TEXT(" \u00b7 "));
		return !OutStats.IsEmpty();
	}
	if (!Game || Selected->source_card_id.empty()) {
		return false;
	}
	const tactics::CardInstance* Inst = FindCardInstanceForEntity(*Game, *Selected);
	const tactics::CardDefinition* Def = nullptr;
	if (Inst) {
		Def = tactics::try_get_card_definition_ptr(Inst->definition_id);
	}
	if (!Def) {
		Def = tactics::try_get_card_definition_ptr(Selected->source_card_id);
	}
	if (!Def) {
		return false;
	}
	const int32 LiveCrit = bShowAdvancedCardText ? Selected->crit_chance_percent : -1;
	const int32 LiveCurrentHealth = Selected->current_health;
	const int32 LiveMaxHealth = tactics::entity_effective_base_health(*Selected);
	const int32 LiveArmor = tactics::armor_value(*Selected);
	FUnitStatLiveOverrides Live;
	if (const std::shared_ptr<tactics::Unit> LiveUnit = std::dynamic_pointer_cast<tactics::Unit>(Selected)) {
		const tactics::DamageRange Melee = tactics::unit_effective_melee_damage_range(*LiveUnit);
		const tactics::DamageRange Ranged = tactics::unit_effective_ranged_damage_range(*LiveUnit);
		Live.MeleeMin = Melee.min;
		Live.MeleeMax = Melee.max;
		Live.RangedMin = Ranged.min;
		Live.RangedMax = Ranged.max;
		Live.Movement = tactics::unit_effective_movement(*LiveUnit);
		const tactics::PassiveStatGrant Temp = tactics::temporary_stat_grants_for_entity(*LiveUnit);
		Live.RangedRange = LiveUnit->ranged_range + Temp.bonus_ranged_range;
	}
	OutStats = build_unit_stat_tokens(*Def, bShowAdvancedCardText, LiveCrit, LiveCurrentHealth, LiveMaxHealth, LiveArmor, Live);
	return !OutStats.IsEmpty();
}

bool UTacticsMatchSubsystem::TryGetSelectedUnitActionIconStacks(TArray<FString>& OutMoveArtIds, TArray<FString>& OutAttackArtIds,
	TArray<FString>& OutReactionArtIds) const
{
	OutMoveArtIds.Reset();
	OutAttackArtIds.Reset();
	OutReactionArtIds.Reset();
	const bool bIsBase = tactics::entity_is_base(*Selected);
	if (!Selected || (!tactics::entity_is_board_unit(*Selected) && !bIsBase)) {
		return false;
	}
	static const FString MoveReady(TEXT("ui/actions/movement_ready"));
	static const FString MoveUsed(TEXT("ui/actions/movement_used"));
	static const FString AttackReady(TEXT("ui/actions/attack_ready"));
	static const FString AttackUsed(TEXT("ui/actions/attack_used"));
	static const FString ReactionReady(TEXT("ui/actions/reaction_ready"));
	static const FString ReactionUsed(TEXT("ui/actions/reaction_used"));

	if (!bIsBase) {
		const int32 MovesRemaining = FMath::Max(0, Selected->moves_remaining_this_turn);
		int32 MoveTotal = MovesRemaining;
		if (Selected->has_moved_this_turn) {
			const tactics::PassiveStatGrant Temp = temporary_stat_grants_for_entity(*Selected);
			int32 BaselineBudget = Selected->bonus_moves + Temp.bonus_moves;
			if (tactics::entity_can_move(*Selected) && !tactics::deployment_fatigue_blocks_move(*Selected)) {
				BaselineBudget += 1;
			}
			MoveTotal = FMath::Max(MovesRemaining, BaselineBudget);
		} else if (tactics::entity_can_move(*Selected)) {
			MoveTotal = FMath::Max(MovesRemaining, 1);
		}
		const int32 MovesSpent = FMath::Max(0, MoveTotal - MovesRemaining);
		for (int32 i = 0; i < MovesRemaining; ++i) {
			OutMoveArtIds.Add(MoveReady);
		}
		for (int32 i = 0; i < MovesSpent; ++i) {
			OutMoveArtIds.Add(MoveUsed);
		}

		constexpr int32 kMaxReactionsPerTurn = 3;
		const int32 ReactionsRemaining = FMath::Clamp(Selected->reactions_remaining_this_turn, 0, kMaxReactionsPerTurn);
		const int32 ReactionsSpent = kMaxReactionsPerTurn - ReactionsRemaining;
		for (int32 i = 0; i < ReactionsRemaining; ++i) {
			OutReactionArtIds.Add(ReactionReady);
		}
		for (int32 i = 0; i < ReactionsSpent; ++i) {
			OutReactionArtIds.Add(ReactionUsed);
		}
	}

	const int32 AttacksRemaining = FMath::Max(0, Selected->attacks_remaining_this_turn)
		+ FMath::Max(0, Selected->bonus_attacks_remaining_this_turn);
	int32 AttackTotal = AttacksRemaining;
	if (Selected->has_attacked_this_turn) {
		const tactics::PassiveStatGrant Temp = temporary_stat_grants_for_entity(*Selected);
		const int32 BaselineBudget = 1 + Selected->bonus_attacks + Temp.bonus_attacks;
		AttackTotal = FMath::Max(AttacksRemaining, BaselineBudget);
	} else {
		AttackTotal = FMath::Max(AttacksRemaining, 1);
	}
	const int32 AttacksSpent = FMath::Max(0, AttackTotal - AttacksRemaining);
	for (int32 i = 0; i < AttacksRemaining; ++i) {
		OutAttackArtIds.Add(AttackReady);
	}
	for (int32 i = 0; i < AttacksSpent; ++i) {
		OutAttackArtIds.Add(AttackUsed);
	}
	return true;
}


bool UTacticsMatchSubsystem::TryGetSelectedUnitCardRulesBase(FString& OutRulesLine) const
{
	OutRulesLine.Reset();
	if (!Game || !Selected) {
		return false;
	}
	if (tactics::entity_is_base(*Selected)) {
		OutRulesLine = UTF8_TO_TCHAR(tactics::format_player_base_card_rules(bShowAdvancedCardText).c_str());
		return true;
	}
	const tactics::CardInstance* Inst = nullptr;
	const tactics::CardDefinition* Def = SelectedUnitCardDefinition(*Game, *Selected, Inst);
	if (!Def) {
		return false;
	}
	FString NameUnused, TypeUnused, CostUnused;
	return fill_card_ui_strings(*Def, Inst, NameUnused, TypeUnused, CostUnused, OutRulesLine, bShowAdvancedCardText, false);
}

FString UTacticsMatchSubsystem::GetSelectedUnitGainedKeywordsStrip() const
{
	if (!Game || !Selected || tactics::entity_is_base(*Selected)) {
		return FString();
	}
	const tactics::CardInstance* Inst = nullptr;
	const tactics::CardDefinition* Def = SelectedUnitCardDefinition(*Game, *Selected, Inst);
	const std::string Strip = tactics::format_entity_gained_keywords_rules_strip(*Selected, Def);
	return Strip.empty() ? FString() : UTF8_TO_TCHAR(Strip.c_str());
}

void UTacticsMatchSubsystem::GetSelectedUnitRuntimePassiveStrips(TArray<FTacticsRuntimePassiveStrip>& OutStrips) const
{
	OutStrips.Reset();
	if (!Game || !Selected) {
		return;
	}
	const tactics::CardInstance* Inst = nullptr;
	const tactics::CardDefinition* Def = SelectedUnitCardDefinition(*Game, *Selected, Inst);
	if (!Def) {
		return;
	}
	const std::vector<std::string> CardPassiveIds = CollectPassiveIdsForCatalogStrips(*Def);
	for (const tactics::PassiveAbilitySpec& Passive : Selected->passive_abilities) {
		if (Passive.key.empty()) {
			continue;
		}
		if (std::find(CardPassiveIds.begin(), CardPassiveIds.end(), Passive.key) != CardPassiveIds.end()) {
			continue;
		}
		const std::string ChosenRules = CardTextForDisplay(Passive.normal_rules_text, Passive.rules_text, bShowAdvancedCardText);
		FTacticsRuntimePassiveStrip Row;
		Row.Name = UTF8_TO_TCHAR(Passive.name.empty() ? Passive.key.c_str() : Passive.name.c_str());
		Row.RulesText = UTF8_TO_TCHAR(ChosenRules.c_str());
		OutStrips.Add(MoveTemp(Row));
	}
}

bool UTacticsMatchSubsystem::TryGetSelectedUnitCardUi(FString& OutName, FString& OutTypeTag, FString& OutCostLine, FString& OutRulesLine,
	FString& OutArtId) const
{
	OutArtId.Reset();
	if (!Game || !Selected) {
		return false;
	}
	const tactics::Entity& E = *Selected;
	if (tactics::entity_is_base(E)) {
		OutName = FString::Printf(TEXT("Player %d Base"), E.owner.value_or(0));
		OutTypeTag = TEXT("base");
		OutCostLine.Reset();
		OutRulesLine = UTF8_TO_TCHAR(tactics::format_player_base_card_rules(bShowAdvancedCardText).c_str());
		OutArtId = ResolveArtIdForEntity(*Game, E);
		return true;
	}
	const tactics::CardInstance* Inst = nullptr;
	const tactics::CardDefinition* Def = SelectedUnitCardDefinition(*Game, E, Inst);
	if (!Def) {
		OutName = UTF8_TO_TCHAR(E.entity_id.c_str());
		OutTypeTag = UTF8_TO_TCHAR(E.entity_type.c_str());
		OutCostLine.Reset();
		OutRulesLine = FormatSelectedUnitStats();
		OutArtId = ResolveArtIdForEntity(*Game, E);
		return true;
	}
	if (!fill_card_ui_strings(*Def, Inst, OutName, OutTypeTag, OutCostLine, OutRulesLine, bShowAdvancedCardText, false)) {
		return false;
	}
	OutArtId = ResolveArtIdForEntity(*Game, E);
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandSpellRequiresFocusCaster(int32 Index1Based, bool& bOutRequiresFocus) const
{
	bOutRequiresFocus = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutRequiresFocus = tactics::spell_requires_focus_caster(*Def);
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandSpellRequiresForcedFocusCaster(int32 Index1Based, bool& bOutRequires) const
{
	bOutRequires = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutRequires = tactics::spell_requires_forced_damage_spell_focus_caster(*Game, ControlledPlayer, *Def);
	return true;
}

bool UTacticsMatchSubsystem::TryGetReservesSpellRequiresForcedFocusCaster(int32 Index1Based, bool& bOutRequires) const
{
	bOutRequires = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const auto DeckIt = Game->players_decks.find(ControlledPlayer);
	if (DeckIt == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = DeckIt->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutRequires = tactics::spell_requires_forced_damage_spell_focus_caster(*Game, ControlledPlayer, *Def);
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandSpellUsesDirectionalAim(int32 Index1Based, bool& bOutUsesDirectionalAim) const
{
	bOutUsesDirectionalAim = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutUsesDirectionalAim = tactics::effect_uses_directional_aim(tactics::definition_spell(*Def).effect_key);
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandSpellUsesPushDirectionAim(int32 Index1Based, bool& bOutUsesPushDirectionAim) const
{
	bOutUsesPushDirectionAim = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutUsesPushDirectionAim = tactics::effect_key_uses_push_direction_aim(tactics::definition_spell(*Def).effect_key);
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandSpellRequiresBoardCell(int32 Index1Based, bool& bOutRequiresCell) const
{
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutRequiresCell = tactics::definition_spell_requires_mandatory_board_cell(*Def);
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandSpellIsModal(int32 Index1Based, bool& bOutIsModal) const
{
	bOutIsModal = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutIsModal = tactics::definition_spell_is_modal(*Def);
	return true;
}

int32 UTacticsMatchSubsystem::GetHandSpellModalModeCount(int32 Index1Based) const
{
	if (!Game || Index1Based < 1) {
		return 0;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return 0;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return 0;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return 0;
	}
	return tactics::definition_spell_modal_mode_count(*Def);
}

bool UTacticsMatchSubsystem::TryGetHandSpellModalModeUi(int32 Index1Based, int32 ModeIndex0, FString& OutLabel, FString& OutRules) const
{
	OutLabel.Reset();
	OutRules.Reset();
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	const tactics::SpellMode* Mode = Def ? tactics::try_definition_spell_mode(*Def, ModeIndex0) : nullptr;
	if (!Mode) {
		return false;
	}
	OutLabel = UTF8_TO_TCHAR(Mode->label.c_str());
	OutRules = UTF8_TO_TCHAR(Mode->rules_text.c_str());
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandSpellModeRequiresBoardCell(int32 Index1Based, int32 ModeIndex0, bool& bOutRequiresCell) const
{
	bOutRequiresCell = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutRequiresCell = tactics::definition_spell_mode_requires_board_cell(*Def, ModeIndex0);
	return true;
}

bool UTacticsMatchSubsystem::TryGetReservesSpellIsModal(int32 Index1Based, bool& bOutIsModal) const
{
	bOutIsModal = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutIsModal = tactics::definition_spell_is_modal(*Def);
	return true;
}

int32 UTacticsMatchSubsystem::GetReservesSpellModalModeCount(int32 Index1Based) const
{
	if (!Game || Index1Based < 1) {
		return 0;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return 0;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return 0;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return 0;
	}
	return tactics::definition_spell_modal_mode_count(*Def);
}

bool UTacticsMatchSubsystem::TryGetReservesSpellModalModeUi(int32 Index1Based, int32 ModeIndex0, FString& OutLabel, FString& OutRules) const
{
	OutLabel.Reset();
	OutRules.Reset();
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	const tactics::SpellMode* Mode = Def ? tactics::try_definition_spell_mode(*Def, ModeIndex0) : nullptr;
	if (!Mode) {
		return false;
	}
	OutLabel = UTF8_TO_TCHAR(Mode->label.c_str());
	OutRules = UTF8_TO_TCHAR(Mode->rules_text.c_str());
	return true;
}

bool UTacticsMatchSubsystem::TryGetReservesSpellModeRequiresBoardCell(int32 Index1Based, int32 ModeIndex0, bool& bOutRequiresCell) const
{
	bOutRequiresCell = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutRequiresCell = tactics::definition_spell_mode_requires_board_cell(*Def, ModeIndex0);
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandSpellSpeedTag(int32 Index1Based, FString& OutSpeedTag) const
{
	OutSpeedTag.Reset();
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	const tactics::EffectSpeed CastSpeed = tactics::effective_spell_cast_speed(*Game, ControlledPlayer, *Def);
	switch (CastSpeed) {
		case tactics::EffectSpeed::Reflex:
			OutSpeedTag = TEXT("reflex");
			break;
		case tactics::EffectSpeed::Blazing:
			OutSpeedTag = TEXT("blazing");
			break;
		case tactics::EffectSpeed::Channeled:
		default:
			OutSpeedTag = TEXT("channeled");
			break;
	}
	return true;
}

bool UTacticsMatchSubsystem::TryGetReservesSpellSpeedTag(int32 Index1Based, FString& OutSpeedTag) const
{
	OutSpeedTag.Reset();
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	const tactics::EffectSpeed CastSpeed = tactics::effective_spell_cast_speed(*Game, ControlledPlayer, *Def);
	switch (CastSpeed) {
		case tactics::EffectSpeed::Reflex:
			OutSpeedTag = TEXT("reflex");
			break;
		case tactics::EffectSpeed::Blazing:
			OutSpeedTag = TEXT("blazing");
			break;
		case tactics::EffectSpeed::Channeled:
		default:
			OutSpeedTag = TEXT("channeled");
			break;
	}
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandSpellRequiresStackTarget(int32 Index1Based, bool& bOutRequiresStackTarget) const
{
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutRequiresStackTarget = tactics::definition_spell_requires_stack_target(*Def);
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandSpellRequiresPlayerSeatTarget(int32 Index1Based, bool& bOutRequiresPlayerSeatTarget) const
{
	bOutRequiresPlayerSeatTarget = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutRequiresPlayerSeatTarget = tactics::definition_spell_requires_player_seat_target(*Def);
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandSpellMulticastInfo(int32 Index1Based, int32& OutAmount, bool& bOutPerCopyTargets) const
{
	OutAmount = 1;
	bOutPerCopyTargets = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	OutAmount = tactics::definition_multicast_amount(*Def);
	bOutPerCopyTargets = tactics::definition_spell_multicast_requires_per_copy_targets(*Def);
	return true;
}

bool UTacticsMatchSubsystem::TryGetHandSpellXCostInfo(int32 Index1Based, bool& bOutHasXCost, FString& OutEnergyType, int32& OutMinX,
	int32& OutMaxAffordableX) const
{
	bOutHasXCost = false;
	OutEnergyType.Reset();
	OutMinX = 0;
	OutMaxAffordableX = 0;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutHasXCost = tactics::definition_spell_has_x_cost(*Def);
	if (!bOutHasXCost) {
		return true;
	}
	const tactics::SpellCardDefinition& Spell = tactics::definition_spell(*Def);
	OutEnergyType = FString(UTF8_TO_TCHAR(tactics::to_string(*Spell.x_cost_energy_type).c_str()));
	OutMinX = tactics::definition_spell_x_cost_min(*Def);
	OutMaxAffordableX = tactics::max_affordable_spell_x_amount(*Game, ControlledPlayer, *Def);
	return true;
}

bool UTacticsMatchSubsystem::TryGetReservesSpellXCostInfo(int32 Index1Based, bool& bOutHasXCost, FString& OutEnergyType, int32& OutMinX,
	int32& OutMaxAffordableX) const
{
	bOutHasXCost = false;
	OutEnergyType.Reset();
	OutMinX = 0;
	OutMaxAffordableX = 0;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const auto DeckIt = Game->players_decks.find(ControlledPlayer);
	if (DeckIt == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = DeckIt->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ControlledPlayer, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	bOutHasXCost = tactics::definition_spell_has_x_cost(*Def);
	if (!bOutHasXCost) {
		return true;
	}
	const tactics::SpellCardDefinition& Spell = tactics::definition_spell(*Def);
	OutEnergyType = FString(UTF8_TO_TCHAR(tactics::to_string(*Spell.x_cost_energy_type).c_str()));
	OutMinX = tactics::definition_spell_x_cost_min(*Def);
	OutMaxAffordableX = tactics::max_affordable_spell_x_amount(*Game, ControlledPlayer, *Def);
	return true;
}

bool UTacticsMatchSubsystem::TryGetSelectedAbilityXCostInfo(const FString& AbilityKey, bool& bOutHasXCost, FString& OutEnergyType,
	int32& OutMinX, int32& OutMaxAffordableX) const
{
	bOutHasXCost = false;
	OutEnergyType.Reset();
	OutMinX = 0;
	OutMaxAffordableX = 0;
	if (!Game || !Selected || AbilityKey.IsEmpty()) {
		return false;
	}
	const std::string KeyUtf8 = TCHAR_TO_UTF8(*AbilityKey);
	for (const tactics::AbilitySpec& Ab : Selected->activated_abilities) {
		if (Ab.key != KeyUtf8) {
			continue;
		}
		bOutHasXCost = Ab.x_cost_energy_type.has_value();
		if (!bOutHasXCost) {
			return true;
		}
		OutEnergyType = FString(UTF8_TO_TCHAR(tactics::to_string(*Ab.x_cost_energy_type).c_str()));
		OutMinX = std::max(0, Ab.x_cost_min);
		OutMaxAffordableX = tactics::max_affordable_ability_x_amount(*Game, ControlledPlayer, Ab);
		return true;
	}
	return false;
}

bool UTacticsMatchSubsystem::TryGetStackItemBatchedSpellTotalCost(const FString& ItemId, int32& OutTotalCost) const
{
	OutTotalCost = 0;
	if (!Game || ItemId.IsEmpty()) {
		return false;
	}
	const std::string IdUtf8 = TCHAR_TO_UTF8(*ItemId);
	const tactics::StackItem* Item = Game->find_batched_item(IdUtf8);
	if (!Item) {
		for (const tactics::StackItem& StackItem : Game->stack_manager.stack) {
			if (StackItem.item_id == IdUtf8) {
				Item = &StackItem;
				break;
			}
		}
	}
	if (!Item || Item->batched_spell_total_cost <= 0) {
		return false;
	}
	OutTotalCost = Item->batched_spell_total_cost;
	return true;
}

bool UTacticsMatchSubsystem::TryGetReservesSpellMulticastInfo(int32 Index1Based, int32& OutAmount, bool& bOutPerCopyTargets) const
{
	OutAmount = 1;
	bOutPerCopyTargets = false;
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	OutAmount = tactics::definition_multicast_amount(*Def);
	bOutPerCopyTargets = tactics::definition_spell_multicast_requires_per_copy_targets(*Def);
	return true;
}

bool UTacticsMatchSubsystem::CanHandSpellTargetStackSourceType(int32 Index1Based, const FString& SourceType) const
{
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_hands.find(ViewPid);
	if (It == Game->players_hands.end() || It->second == nullptr) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>* Hand = It->second;
	if (Index1Based > static_cast<int32>(Hand->size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, (*Hand)[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	const tactics::TargetDefinition Target = tactics::target_definition_for_effect_key(tactics::definition_spell(*Def).effect_key);
	if (Target.domain != tactics::TargetDomain::StackItem) {
		return false;
	}
	if (Target.allowed_stack_source_types.empty()) {
		return true;
	}
	const std::string Source = TCHAR_TO_UTF8(*SourceType);
	return std::find(Target.allowed_stack_source_types.begin(), Target.allowed_stack_source_types.end(), Source) != Target.allowed_stack_source_types.end();
}

bool UTacticsMatchSubsystem::CanReservesSpellTargetStackSourceType(int32 Index1Based, const FString& SourceType) const
{
	if (!Game || Index1Based < 1) {
		return false;
	}
	const int32 ViewPid = GetHandViewPlayerId();
	const auto It = Game->players_decks.find(ViewPid);
	if (It == Game->players_decks.end()) {
		return false;
	}
	const std::vector<tactics::CardInstanceId>& Reserves = It->second.reserves;
	if (Index1Based > static_cast<int32>(Reserves.size())) {
		return false;
	}
	const tactics::CardDefinition* Def = MatchCardDef(*Game, ViewPid, Reserves[static_cast<size_t>(Index1Based - 1)]);
	if (!Def || !tactics::definition_is_spell(*Def)) {
		return false;
	}
	const tactics::TargetDefinition Target = tactics::target_definition_for_effect_key(tactics::definition_spell(*Def).effect_key);
	if (Target.domain != tactics::TargetDomain::StackItem) {
		return false;
	}
	if (Target.allowed_stack_source_types.empty()) {
		return true;
	}
	const std::string Source = TCHAR_TO_UTF8(*SourceType);
	return std::find(Target.allowed_stack_source_types.begin(), Target.allowed_stack_source_types.end(), Source) != Target.allowed_stack_source_types.end();
}

bool UTacticsMatchSubsystem::TryGetSelectedAbilityRequiresStackTarget(const FString& AbilityKey, bool& bOutRequiresStackTarget) const
{
	bOutRequiresStackTarget = false;
	if (!Selected) {
		return false;
	}
	const std::string Key = TCHAR_TO_UTF8(*AbilityKey);
	for (const tactics::AbilitySpec& Ability : Selected->activated_abilities) {
		if (Ability.key == Key) {
			bOutRequiresStackTarget = tactics::target_definition_for_effect_key(Ability.effect_key).domain == tactics::TargetDomain::StackItem;
			return true;
		}
	}
	return false;
}

bool UTacticsMatchSubsystem::TryGetSelectedAbilityTargetsEmptyCell(const FString& AbilityKey, bool& bOutTargetsEmptyCell) const
{
	bOutTargetsEmptyCell = false;
	if (!Selected) {
		return false;
	}
	const std::string Key = TCHAR_TO_UTF8(*AbilityKey);
	for (const tactics::AbilitySpec& Ability : Selected->activated_abilities) {
		if (Ability.key == Key) {
			bOutTargetsEmptyCell = tactics::effect_key_targets_empty_cell(Ability.effect_key);
			return true;
		}
	}
	return false;
}

bool UTacticsMatchSubsystem::CanSelectedAbilityTargetStackSourceType(const FString& AbilityKey, const FString& SourceType) const
{
	if (!Selected) {
		return false;
	}
	const std::string Key = TCHAR_TO_UTF8(*AbilityKey);
	const std::string Source = TCHAR_TO_UTF8(*SourceType);
	for (const tactics::AbilitySpec& Ability : Selected->activated_abilities) {
		if (Ability.key != Key) {
			continue;
		}
		const tactics::TargetDefinition Target = tactics::target_definition_for_effect_key(Ability.effect_key);
		if (Target.domain != tactics::TargetDomain::StackItem) {
			return false;
		}
		return Target.allowed_stack_source_types.empty()
			|| std::find(Target.allowed_stack_source_types.begin(), Target.allowed_stack_source_types.end(), Source)
				!= Target.allowed_stack_source_types.end();
	}
	return false;
}

void UTacticsMatchSubsystem::GetSpellStackUiLines(TArray<FString>& OutTopFirstLines) const
{
	OutTopFirstLines.Reset();
	TArray<FTacticsStackItemUi> Items;
	GetSpellStackUiItems(Items);
	if (Items.IsEmpty()) {
		OutTopFirstLines.Add(TEXT("(batch queue empty)"));
		return;
	}
	for (const FTacticsStackItemUi& Item : Items) {
		OutTopFirstLines.Add(FString::Printf(TEXT("%s ? P%d ? %s ? %s [%s]"),
			*Item.StackId,
			Item.ControllerPlayerId,
			*Item.SourceName,
			*Item.EffectKey,
			*Item.Speed));
	}
}

void UTacticsMatchSubsystem::GetSpellStackUiItems(TArray<FTacticsStackItemUi>& OutTopFirstItems) const
{
	OutTopFirstItems.Reset();
	if (!Game) {
		return;
	}
	const std::vector<tactics::StackItem>& St = Game->stack_manager.stack;
	for (auto It = St.rbegin(); It != St.rend(); ++It) {
		const tactics::StackItem& Item = *It;
		const TCHAR* Spd = TEXT("channeled");
		switch (Item.speed) {
			case tactics::EffectSpeed::Reflex:
				Spd = TEXT("reflex");
				break;
			case tactics::EffectSpeed::Blazing:
				Spd = TEXT("blazing");
				break;
			case tactics::EffectSpeed::Channeled:
			default:
				break;
		}
		FTacticsStackItemUi Ui;
		Ui.StackId = UTF8_TO_TCHAR(Item.item_id.c_str());
		Ui.SourceType = UTF8_TO_TCHAR(Item.source_type.c_str());
		Ui.SourceName = UTF8_TO_TCHAR(Item.source_name.c_str());
		Ui.EffectKey = UTF8_TO_TCHAR(Item.effect_key.c_str());
		Ui.Speed = Spd;
		Ui.ControllerPlayerId = Item.controller_id;
		if (!Item.target_stack_item_id.empty()) {
			Ui.TargetLine = FString::Printf(TEXT("Targets %s"), UTF8_TO_TCHAR(Item.target_stack_item_id.c_str()));
		} else if (!Item.target_entity_id.empty()) {
			Ui.TargetLine = FString::Printf(TEXT("Targets %s"), UTF8_TO_TCHAR(Item.target_entity_id.c_str()));
		} else if (Item.targets.contains(tactics::effect_keys::kCellX) && Item.targets.contains(tactics::effect_keys::kCellY)) {
			Ui.TargetLine = FString::Printf(
				TEXT("Targets cell %d,%d"), Item.targets.at(tactics::effect_keys::kCellX), Item.targets.at(tactics::effect_keys::kCellY));
		} else {
			Ui.TargetLine = TEXT("No target");
		}
		OutTopFirstItems.Add(MoveTemp(Ui));
	}
}

void UTacticsMatchSubsystem::GetActionQueueUiEntries(TArray<FTacticsActionQueueEntryUi>& OutEntries) const
{
	OutEntries.Reset();
	if (!Game) return;

	const auto Phase = Game->turn_manager.current_phase;

	auto StackItemToEntry = [&](const tactics::StackItem& Item, const FString& Kind, int32 GroupIndex) -> FTacticsActionQueueEntryUi
	{
		FTacticsActionQueueEntryUi E;
		E.Kind = Kind;
		E.Label = Item.source_name.empty()
			? UTF8_TO_TCHAR(Item.effect_key.c_str())
			: UTF8_TO_TCHAR(Item.source_name.c_str());
		if (GroupIndex > 0) {
			E.Label = FString::Printf(TEXT("[G%d] %s"), GroupIndex, *E.Label);
		}
		if (!Item.target_entity_id.empty()) {
			E.Detail = FString::Printf(TEXT("→ %s"), UTF8_TO_TCHAR(Item.target_entity_id.c_str()));
		} else if (Item.targets.contains(tactics::effect_keys::kCellX) && Item.targets.contains(tactics::effect_keys::kCellY)) {
			E.Detail = FString::Printf(TEXT("→ cell %d,%d"),
				Item.targets.at(tactics::effect_keys::kCellX), Item.targets.at(tactics::effect_keys::kCellY));
		}
		E.ControllerPlayerId = Item.controller_id;
		E.ItemId = UTF8_TO_TCHAR(Item.item_id.c_str());
		E.SourceType = UTF8_TO_TCHAR(Item.source_type.c_str());
		if (!E.ItemId.IsEmpty()) {
			if (!E.Detail.IsEmpty()) {
				E.Detail += TEXT("  ");
			}
			E.Detail += E.ItemId;
		}
		return E;
	};

	auto AddAttackEntry = [&](const tactics::GameState::AttackDeclaration& Decl, int32 GroupIndex) -> FTacticsActionQueueEntryUi
	{
		FTacticsActionQueueEntryUi E;
		E.Kind = TEXT("attack");
		const auto It = Game->board.all_entities_map.find(Decl.attacker_id);
		FString AttackerName = UTF8_TO_TCHAR(Decl.attacker_id.c_str());
		if (It != Game->board.all_entities_map.end() && It->second) {
			const auto* AsUnit = dynamic_cast<const tactics::Unit*>(It->second.get());
			if (AsUnit && !AsUnit->unit_type.empty()) {
				AttackerName = UTF8_TO_TCHAR(AsUnit->unit_type.c_str());
			}
		}
		E.Label = GroupIndex > 0
			? FString::Printf(TEXT("[G%d] Attack: %s"), GroupIndex, *AttackerName)
			: FString::Printf(TEXT("Attack: %s"), *AttackerName);
		E.Detail = FString::Printf(TEXT("→ %d,%d%s"),
			Decl.target_x, Decl.target_y, Decl.ranged ? TEXT(" (ranged)") : TEXT(""));
		return E;
	};

	const auto& Queue = Game->phase_action_queue();
	const auto& Boundaries = Game->phase_action_group_boundaries();

	// Sealed groups as [begin, end) slices in queue order (G1 = oldest batch, Gn = newest).
	std::vector<std::pair<size_t, size_t>> Groups;
	{
		size_t Start = 0;
		for (const size_t Boundary : Boundaries) {
			if (Boundary > Start && Boundary <= Queue.size()) {
				Groups.emplace_back(Start, Boundary);
				Start = Boundary;
			}
		}
		if (Start < Queue.size()) {
			Groups.emplace_back(Start, Queue.size());
		}
	}

	auto EmitEntryAt = [&](size_t I, int32 GroupIndex, bool bSpellWindow, bool bDefenseWindow) {
		const auto& Entry = Queue[I];
		if (Entry.is_attack) {
			if (!bDefenseWindow) {
				return;
			}
			FTacticsActionQueueEntryUi Row = AddAttackEntry(Entry.attack, GroupIndex);
			Row.QueueIndex = static_cast<int32>(I);
			OutEntries.Add(MoveTemp(Row));
		} else {
			FString Kind = TEXT("spell");
			if (bSpellWindow && GroupIndex > 1) {
				Kind = TEXT("reaction");
			} else if (bDefenseWindow && GroupIndex > 1) {
				Kind = TEXT("reaction");
			}
			FTacticsActionQueueEntryUi Row = StackItemToEntry(Entry.spell_item, Kind, GroupIndex);
			Row.QueueIndex = static_cast<int32>(I);
			OutEntries.Add(MoveTemp(Row));
		}
	};

	if (Phase == tactics::TurnPhase::AttackDeclaration || Phase == tactics::TurnPhase::BonusAttackDeclaration) {
		const size_t Begin = Groups.empty() ? 0 : Groups.back().first;
		const size_t End = Queue.size();
		const int32 GroupIndex = Groups.empty() ? 1 : static_cast<int32>(Groups.size());
		for (size_t I = Begin; I < End; ++I) {
			const auto& Entry = Queue[I];
			if (Entry.is_attack) {
				FTacticsActionQueueEntryUi Row = AddAttackEntry(Entry.attack, GroupIndex);
				Row.QueueIndex = static_cast<int32>(I);
				OutEntries.Add(MoveTemp(Row));
			} else {
				FTacticsActionQueueEntryUi Row = StackItemToEntry(Entry.spell_item, TEXT("spell"), GroupIndex);
				Row.QueueIndex = static_cast<int32>(I);
				OutEntries.Add(MoveTemp(Row));
			}
		}
		return;
	}

	const bool bSpellWindow = Phase == tactics::TurnPhase::SpellWindow || Phase == tactics::TurnPhase::SecondSpellWindow;
	const bool bDefenseWindow = Phase == tactics::TurnPhase::Defense || Phase == tactics::TurnPhase::BonusDefense;
	const bool bMainPhase = Phase == tactics::TurnPhase::Main || Phase == tactics::TurnPhase::SecondMain;
	if (!bMainPhase && !bSpellWindow && !bDefenseWindow) {
		return;
	}

	// Top-to-bottom: newest batch on top (resolves first), oldest batch at bottom; FIFO within each batch.
	if (Groups.empty()) {
		for (size_t I = 0; I < Queue.size(); ++I) {
			EmitEntryAt(I, 1, bSpellWindow, bDefenseWindow);
		}
		return;
	}

	for (int32 G = static_cast<int32>(Groups.size()) - 1; G >= 0; --G) {
		const auto [Begin, End] = Groups[static_cast<size_t>(G)];
		const int32 GroupIndex = G + 1;
		for (size_t I = Begin; I < End; ++I) {
			EmitEntryAt(I, GroupIndex, bSpellWindow, bDefenseWindow);
		}
	}
}

