#include "tactics/apps/master_cli_dispatch.hpp"

#include "tactics/apps/sandbox_match.hpp"
#include "tactics/board/board_display.hpp"
#include "tactics/board/board_layout.hpp"
#include "tactics/cards/card_runtime.hpp"
#include "tactics/cards/focus_spell.hpp"
#include "tactics/common/effect_keys.hpp"
#include "tactics/core.hpp"
#include "tactics/effects/effect_registry.hpp"
#include "tactics/effects/effect_traits.hpp"
#include "tactics/actions/actions.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

namespace tactics {

void master_cli_seed_zones_for_player(GameState& game, int player_id) {
	game.players_energy_zones[player_id] = {
		{"p" + std::to_string(player_id) + "_r", "RedOnly", std::string{}, {{EnergyType::Red, 1}}, false},
		{"p" + std::to_string(player_id) + "_rt", "RedTurquoise", std::string{},
			{{EnergyType::Red, 1}, {EnergyType::Turquoise, 1}}, false},
		{"p" + std::to_string(player_id) + "_n", "Neutral", std::string{}, {{EnergyType::Neutral, 1}}, false},
		{"p" + std::to_string(player_id) + "_o", "Omni", std::string{}, {{EnergyType::Omni, 1}}, false},
	};
}

namespace {

BoardCellBounds merged_cli_bounds(const GameState& game) {
    return cell_bounds_or_main_module(game.board_cell_bounds(), game.board_width(), game.board_height());
}

std::vector<std::string> split(const std::string& s) {
    std::istringstream iss(s);
    std::vector<std::string> out;
    std::string tok;
    while (iss >> tok) out.push_back(tok);
    return out;
}

struct ParsedSpellCastCliTargets {
    std::map<std::string, int> primary_targets;
    std::vector<std::map<std::string, int>> multicast_targets;
    std::string stack_target_id;
    size_t next_arg_index{2};
};

bool try_parse_spell_mode_arg(const CardDefinition& def, const std::vector<std::string>& p, size_t& in_out_start,
    int& out_mode, std::ostream& cli_out)
{
    out_mode = -1;
    if (!definition_spell_is_modal(def)) {
        return true;
    }
    const int mode_count = definition_spell_modal_mode_count(def);
    if (p.size() < in_out_start + 2 || p[in_out_start] != "mode") {
        cli_out << "Modal spell " << def.name << ": ... mode <0-" << (mode_count - 1) << "> [targets...]\n";
        return false;
    }
    try {
        out_mode = std::stoi(p[in_out_start + 1]);
    } catch (...) {
        cli_out << "Invalid mode index '" << p[in_out_start + 1] << "' - expected 0-" << (mode_count - 1) << "\n";
        return false;
    }
    if (out_mode < 0 || out_mode >= mode_count) {
        cli_out << "Mode index must be 0-" << (mode_count - 1) << " for " << def.name << "\n";
        return false;
    }
    in_out_start += 2;
    return true;
}

bool parse_spell_cast_cli_targets(const GameState& game, const CardDefinition& def, const std::vector<std::string>& p,
    const size_t start_index, ParsedSpellCastCliTargets& out, std::ostream& cli_out, const int mode_index = -1)
{
    out = {};
    out.next_arg_index = start_index;
    if (p.size() >= start_index + 2 && p[start_index] == "stack") {
        out.stack_target_id = p[start_index + 1];
        out.next_arg_index = start_index + 2;
        return true;
    }
    if (definition_spell_requires_player_seat_target(def)) {
        if (p.size() >= start_index + 2 && p[start_index] == "player") {
            try {
                const int seat = std::stoi(p[start_index + 1]);
                out.primary_targets[effect_keys::kTargetPlayerSeat] = seat;
                out.next_arg_index = start_index + 2;
                return true;
            } catch (...) {
                cli_out << "Invalid player seat '" << p[start_index + 1] << "' - expected an integer\n";
                return false;
            }
        }
        cli_out << "Spell " << def.name << " needs a target player: player <seat>\n";
        return false;
    }
    const SpellCardDefinition& spell = definition_spell(def);
    const std::string effect_key = mode_index >= 0 ? definition_spell_mode_effect_key(def, mode_index) : spell.effect_key;
    const int multicast = definition_multicast_amount(def);
    const bool per_copy = definition_spell_multicast_requires_per_copy_targets(def);
    const BoardCellBounds bb = merged_cli_bounds(game);
    if (per_copy && multicast > 1) {
        out.multicast_targets.reserve(static_cast<size_t>(multicast));
        size_t arg = start_index;
        std::set<std::string> seen;
        while (out.multicast_targets.size() < static_cast<size_t>(multicast) && p.size() >= arg + 2) {
            const auto tcell = parse_grid_cell_1based_world(bb, p[arg], p[arg + 1]);
            if (!tcell) {
                break;
            }
            const std::string sig = std::to_string(tcell->first) + "," + std::to_string(tcell->second);
            if (!seen.insert(sig).second) {
                cli_out << "Multicast targets must be different cells\n";
                return false;
            }
            out.multicast_targets.push_back({
                {effect_keys::kCellX, tcell->first},
                {effect_keys::kCellY, tcell->second},
            });
            arg += 2;
        }
        if (out.multicast_targets.empty()) {
            cli_out << "Multicast " << multicast << " requires at least one target cell (up to " << multicast
                    << " distinct targets: <col1> <row1> ...)\n";
            return false;
        }
        out.primary_targets = out.multicast_targets.front();
        out.next_arg_index = arg;
        return true;
    }
    if (effect_key_uses_push_direction_aim(effect_key)) {
        if (p.size() >= start_index + 4) {
            const auto tcell = parse_grid_cell_1based_world(bb, p[start_index], p[start_index + 1]);
            const auto acell = parse_grid_cell_1based_world(bb, p[start_index + 2], p[start_index + 3]);
            if (!tcell || !acell) {
                cli_out << "Invalid push spell cells (columns 1-" << bb.span_x() << ", rows 1-" << bb.span_y() << ")\n";
                return false;
            }
            out.primary_targets[effect_keys::kCellX] = tcell->first;
            out.primary_targets[effect_keys::kCellY] = tcell->second;
            out.primary_targets[effect_keys::kAimX] = acell->first;
            out.primary_targets[effect_keys::kAimY] = acell->second;
            out.next_arg_index = start_index + 4;
            return true;
        }
        cli_out << "Push spell " << def.name << " needs unit cell and direction cell: <col> <row> <aim_col> <aim_row>"
                << (spell.x_cost_energy_type.has_value() ? " <X>" : "") << "\n";
        return false;
    }
    if (p.size() >= start_index + 2) {
        const auto tcell = parse_grid_cell_1based_world(bb, p[start_index], p[start_index + 1]);
        if (!tcell) {
            cli_out << "Invalid spell target cell (columns 1-" << bb.span_x() << ", rows 1-" << bb.span_y() << ")\n";
            return false;
        }
        out.primary_targets[effect_keys::kCellX] = tcell->first;
        out.primary_targets[effect_keys::kCellY] = tcell->second;
        out.next_arg_index = start_index + 2;
        return true;
    }
    if (definition_spell_requires_stack_target(def)) {
        cli_out << "Spell " << def.name << " needs a stack target: ... stack <stack_id>\n";
        return false;
    }
    if (spell_requires_focus_caster(def)) {
        cli_out << "Focus spell " << def.name << " needs a target cell: <col> <row>"
                << (spell.x_cost_energy_type.has_value() ? " <X>" : "") << "\n";
        return false;
    }
    return true;
}

/** Parses cw / ccw / integer for quarter-turns CW (same semantics as former standalone rotate). */
bool parse_quarter_turns_cli(const std::string& arg, int& out_q, std::ostream& err) {
    if (arg == "ccw" || arg == "-1") {
        out_q = -1;
        return true;
    }
    if (arg == "cw" || arg == "1") {
        out_q = 1;
        return true;
    }
    try {
        out_q = std::stoi(arg);
        return true;
    } catch (...) {
        err << "Expected cw, ccw, or integer quarter-turns\n";
        return false;
    }
}

void seed_zones(GameState& game, int player_id) {
	master_cli_seed_zones_for_player(game, player_id);
}

const CardDefinition* cli_card_def(const GameState& game, int pid, CardInstanceId id)
{
    const auto deck_it = game.players_decks.find(pid);
    if (deck_it == game.players_decks.end()) {
        return nullptr;
    }
    const CardInstance* inst = deck_it->second.pool.try_get(id);
    if (!inst) {
        return nullptr;
    }
    return try_get_card_definition_ptr(inst->definition_id);
}

void print_reserves(GameState& game, int pid, std::ostream& out) {
    const auto deck_it = game.players_decks.find(pid);
    if (deck_it == game.players_decks.end()) {
        return;
    }
    const auto& reserves = deck_it->second.reserves;
    out << "P" << pid << " reserves (" << reserves.size() << "/" << kMaxReservesSize << ", owner-turn only):\n";
    for (size_t i = 0; i < reserves.size(); ++i) {
        const CardDefinition* def = cli_card_def(game, pid, reserves[i]);
        if (!def) {
            continue;
        }
        out << "  " << (i + 1) << ". " << def->name << " [" << def->type << "] cost:";
        for (const auto& [et, amt] : def->energy_cost) out << " " << to_string(et) << ":" << amt;
        out << "\n";
    }
}

void print_purgatory(GameState& game, int pid, std::ostream& out) {
    const auto deck_it = game.players_decks.find(pid);
    if (deck_it == game.players_decks.end()) {
        return;
    }
    const auto& pile = deck_it->second.purgatory;
    out << "P" << pid << " purgatory (" << pile.size() << ", exile-like - not drawable):\n";
    for (size_t i = 0; i < pile.size(); ++i) {
        const CardDefinition* def = cli_card_def(game, pid, pile[i]);
        if (!def) {
            continue;
        }
        out << "  " << (i + 1) << ". " << def->name << " [" << def->type << "]\n";
    }
}

void print_hand(GameState& game, int pid, std::ostream& out) {
    const auto* hand = game.players_hands.at(pid);
    const Deck& deck = game.players_decks.at(pid);
    out << "P" << pid << " hand:\n";
    for (size_t i = 0; i < hand->size(); ++i) {
        const CardInstanceId cid = (*hand)[i];
        const CardInstance* inst = deck.pool.try_get(cid);
        const CardDefinition* def = cli_card_def(game, pid, cid);
        if (!inst || !def) {
            continue;
        }
        out << "  " << (i + 1) << ". " << def->name << " [" << def->type << "] cost:";
        for (const auto& [et, amt] : def->energy_cost) out << " " << to_string(et) << ":" << amt;
        if (inst->stockpile_amount > 0) {
            out << " stockpile:" << inst->stockpile_remaining << "/" << inst->stockpile_amount;
            if (inst->stockpile_used_this_turn) out << " (used this turn)";
        }
        out << "\n";
    }
}

void print_card_pile(const char* label, const GameState& game, int pid, const std::vector<CardInstanceId>& pile, std::ostream& out) {
    out << "  " << label << " (" << pile.size() << "):\n";
    for (size_t i = 0; i < pile.size(); ++i) {
        const CardDefinition* def = cli_card_def(game, pid, pile[i]);
        if (!def) {
            continue;
        }
        out << "    " << (i + 1) << ". " << def->name << " [" << def->type << "]\n";
    }
}

void print_zones(GameState& game, int pid, std::ostream& out) {
    out << "P" << pid << " zones:\n";
    auto it = game.players_energy_zones.find(pid);
    if (it == game.players_energy_zones.end()) return;
    int i = 1;
    for (const auto& z : it->second) {
        out << "  " << i++ << ". " << z.name << " (" << (z.is_tapped ? "Tapped" : "Ready") << ") ->";
        for (const auto& [et, amt] : z.energy_produced) out << " " << to_string(et) << ":" << amt;
        out << "\n";
    }
}

void print_float(GameState& game, int pid, std::ostream& out) {
    out << "P" << pid << " floating:";
    for (const auto& [et, amt] : game.turn_manager.player_energy.at(pid)) out << " " << to_string(et) << ":" << amt;
    // Print any tagged pools that have non-zero amounts for this player.
    for (const auto& [tag, per_player] : game.turn_manager.player_tagged_float) {
        const auto pit = per_player.find(pid);
        if (pit == per_player.end()) continue;
        bool any = false;
        for (const auto& [et, amt] : pit->second) { if (amt > 0) { any = true; break; } }
        if (any) {
            out << " | " << tag << ":";
            for (const auto& [et, amt] : pit->second) if (amt > 0) out << " " << to_string(et) << ":" << amt;
        }
    }
    out << "\n";
}

void print_board(GameState& game, std::ostream& out) {
    out << "Entities:\n";
    for (const auto& [id, e] : game.board.all_entities_map) {
        if (!e->position) continue;
        const auto [cx, cy] = game_cell_to_display_1based(e->position->first, e->position->second);
        out << "  " << id << " owner:" << (e->owner ? std::to_string(*e->owner) : "none") << " pos:(" << cx << "," << cy << ")"
            << " hp:" << e->current_health << "\n";
    }
}

}  // namespace

void master_cli_seed_demo_terrain(GameState& game) { seed_standard_duel_map_features(game); }

void master_cli_seed_demo_obstacles(GameState& game) { seed_standard_duel_map_features(game); }

void master_cli_seed_demo_state(GameState& game) {
    // Standard matches use a blank board (bases only, no demo terrain/obstacles) and zero pre-placed
    // energy zones - players place one zone per turn from their zone deck during the Energy phase.
    // The permanent OBJECTIVE tiles (scanner / omni-energy / aether) are part of the standard duel
    // map, not demo terrain - seed them so headless matches (bot_match, CLI, net server) match the
    // Unreal real-match board (TacticsMatchSubsystem seeds the same tiles).
    ensure_standard_duel_permanent_map_tiles(game);
    for (int pid : game.turn_manager.players) {
        game.players_energy_zones[pid].clear();
    }
    game.refresh_passive_auras();
}

void master_cli_print_help(const GameState& game, std::ostream& out) {
    const BoardCellBounds bb = merged_cli_bounds(game);
    out << "Board: " << bb.span_x() << " x " << bb.span_y()
        << " cells (1-based column/row within merged bounds; matches PPM/plot)\n";
    if (game.game_id().find("footprint_test") != std::string::npos) {
        out << "Deck: footprint test (six unit cards: 1 / 2 / 3-line / 3-L / 2x2 / 5-line tiles), opening hand is those six in order.\n";
    }
    out << "Commands:\n"
        << "  as <seat>           (switch controlled seat - must exist in this match)\n"
        << "  hand\n"
        << "  reserves\n"
        << "  purgatory [seat]   (exile-like removed cards; default: your seat)\n"
        << "  zones\n"
        << "  float\n"
        << "  addfloat <type> <n>\n"
        << "  phase\n"
        << "  zonepick <idx>      (1-based index among offered zones)\n"
        << "  discard <handIdx>    (when ending turn with hand > " << kMaxHandSize << ")\n"
        << "  scan_discard <idx>   (discard peeked deck card during scan; 1-based)\n"
        << "  scan_finish          (keep remaining peeked cards; resume resolution)\n"
        << "  zoneskip\n"
        << "  deploy <handIdx> <x> <y>   (anchor cell; multi-tile units use `template_unit.shape` offsets from anchor)\n"
        << "  deploy_reserve <resIdx> <x> <y>   (from reserves; your turn only)\n"
        << "  select <x> <y>\n"
        << "  deselect              (clear selected unit; cancels pending move preview)\n"
        << "  move_preview <x> <y>  (or: move <x> <y>) - preview destination; no move point spent\n"
        << "  move_rotate [cw|ccw|n]  (default cw) - adjust footprint for pending move\n"
        << "  move_confirm          - commit preview (spends a move)\n"
        << "  move_cancel           - abandon preview (free)\n"
        << "  attack <x> <y>        (queues attack in Attack Declaration or Bonus Attack Declaration phase)\n"
        << "  end_main              (end Main/SecondMain Phase: commits spell batch if any, then advances)\n"
        << "  attack_undeclare <id> (remove a pending attack declaration by attacker entity id)\n"
        << "  attack_commit         (lock attack declarations; opens Defense or BonusDefense window)\n"
        << "  pass_defense          (pass/forfeit in the Spell, Defense, or BonusDefense reaction window; also: 'pass')\n"
        << "  cast <handIdx> [<col> <row> [X] | stack <stack_id>]   (X required for variable-cost spells)\n"
        << "  cast_reserve <resIdx> [<col> <row> [X] | stack <stack_id>]   (from reserves; your turn only)\n"
        << "  ability <key> [x y | stack <stack_id>]   (selected unit)\n"
        << "  teams                 (print seat → team id; default seat==team FFA)\n"
        << "  team <seat> <teamId>  (assign seat to a team; same teamId = allies)\n"
        << "  undo                  (reverse last undoable action this phase)\n"
        << "  match_setting <k> <v> (e.g. allow_deployment_undo 1)\n"
        << "  batch_cancel <id>     (cancel your queued batch item; card/energy/stockpile refund)\n"
        << "  pass\n"
        << "  end\n"
        << "  board\n"
        << "  plot                  -> native popup (master_cli only if hooked)\n"
        << "  sandbox_deck <faction>  (sandbox only) replace all players' hands with a faction deck\n"
        << "    factions: gallantry | ingenuity | mythology | core | all\n"
        << "  help\n"
        << "  quit\n";
}

void master_cli_write_board_ppm(const GameState& game, std::ostream& out, const std::string& path) {
    const BoardCellBounds bb = cell_bounds_or_main_module(game.board_cell_bounds(), game.board_width(), game.board_height());
    const int width = bb.span_x();
    const int height = bb.span_y();
    if (width < 1 || height < 1) {
        out << "Cannot snapshot board: no drawable cell bounds\n";
        return;
    }
    const int origin_x = bb.min_x;
    const int origin_y = bb.min_y;
    const int cell = 64;
    const int margin = 24;
    const int img_w = margin * 2 + width * cell;
    const int img_h = margin * 2 + height * cell;

    struct Pixel {
        unsigned char r;
        unsigned char g;
        unsigned char b;
    };
    std::vector<Pixel> img(static_cast<size_t>(img_w * img_h), {245, 245, 245});

    auto set_px = [&](int x, int y, Pixel c) {
        if (x < 0 || y < 0 || x >= img_w || y >= img_h) return;
        img[static_cast<size_t>(y * img_w + x)] = c;
    };

    auto fill_rect = [&](int x0, int y0, int x1, int y1, Pixel c) {
        for (int y = y0; y < y1; ++y) {
            for (int x = x0; x < x1; ++x) set_px(x, y, c);
        }
    };

    for (int gx = 0; gx <= width; ++gx) {
        int x = margin + gx * cell;
        for (int y = margin; y <= margin + height * cell; ++y) set_px(x, y, {30, 30, 30});
    }
    for (int gy = 0; gy <= height; ++gy) {
        int y = margin + gy * cell;
        for (int x = margin; x <= margin + width * cell; ++x) set_px(x, y, {30, 30, 30});
    }

    for (const auto& [id, e] : game.board.all_entities_map) {
        if (!e || !e->position) continue;
        const auto cells = !e->occupied_positions.empty() ? e->occupied_positions
                                                         : std::vector<std::pair<int, int>>{{e->position->first, e->position->second}};
        for (const auto& [bx, by] : cells) {
            const int lx = bx - origin_x;
            const int ly = by - origin_y;
            if (lx < 0 || ly < 0 || lx >= width || ly >= height) continue;
            const int x0 = cell_left_pixels(margin, cell, lx, kBoardEntityPixelInset);
            const int y0 = cell_top_pixels(margin, cell, height, ly, kBoardEntityPixelInset);
            const int x1 = x0 + cell_fill_extent_px(cell);
            const int y1 = y0 + cell_fill_extent_px(cell);

            Pixel c{44, 62, 80};
            if (e->owner) {
                const auto rgb = rgb_for_player_seat(*e->owner);
                c = {rgb.r, rgb.g, rgb.b};
            }
            fill_rect(x0, y0, x1, y1, c);
        }
    }

    std::error_code ec_path;
    const std::filesystem::path resolved = std::filesystem::absolute(path, ec_path);
    const std::string resolved_str = ec_path ? path : resolved.string();

    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file.is_open()) {
        out << "Failed to open snapshot for write: " << resolved_str << "\n";
        return;
    }
    file << "P6\n" << img_w << " " << img_h << "\n255\n";
    for (const auto& p : img) file.write(reinterpret_cast<const char*>(&p), 3);
    if (!file.good()) {
        out << "Snapshot write incomplete (disk full?): " << resolved_str << "\n";
        return;
    }
    out << "[snapshot] wrote board_snapshot.ppm -> " << resolved_str << "\n";
}

bool dispatch_master_cli_line(GameState& game, int& controlled, std::shared_ptr<Unit>& selected, const std::string& line, std::ostream& out,
                              const MasterCliPlotSink& plot, std::optional<bool>* out_move_performed_ok) {
    if (line.empty()) return false;
    auto p = split(line);
    auto cmd = p[0];

    if (cmd == "quit") return true;
    if (cmd == "help") {
        master_cli_print_help(game, out);
        return false;
    }
    if (game.is_combat_visualization_paused() && cmd != "combat_viz_resume") {
        out << "Match paused for combat visualization. Run combat_viz_resume to continue.\n";
        return false;
    }
    if (cmd == "combat_viz_resume") {
        auto r = game.resume_combat_visualization();
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "as" && p.size() >= 2) {
        const int seat = std::stoi(p[1]);
        if (game.players_decks.find(seat) == game.players_decks.end()) {
            out << "No seat P" << seat << " in this match.\n";
            return false;
        }
        controlled = seat;
        out << "Now controlling P" << controlled << "\n";
        return false;
    }
    if (cmd == "teams") {
        for (int pl : game.turn_manager.players) {
            out << "P" << pl << " -> team " << game.team_of_seat(pl) << "\n";
        }
        return false;
    }
    if (cmd == "team" && p.size() >= 3) {
        const int seat = std::stoi(p[1]);
        const int tid = std::stoi(p[2]);
        if (game.players_decks.find(seat) == game.players_decks.end()) {
            out << "No seat P" << seat << " in this match.\n";
            return false;
        }
        game.set_seat_team(seat, tid);
        out << "P" << seat << " assigned to team " << tid << "\n";
        return false;
    }
    if (cmd == "hand") {
        print_hand(game, controlled, out);
        return false;
    }
    if (cmd == "reserves") {
        print_reserves(game, controlled, out);
        return false;
    }
    if (cmd == "purgatory") {
        int seat = controlled;
        if (p.size() >= 2) {
            const auto seat_opt = parse_cli_index_1based(game.players_decks.size(), p[1]);
            if (!seat_opt) {
                out << "Invalid seat (use 1-" << game.players_decks.size() << ")\n";
                return false;
            }
            seat = *seat_opt;
        }
        print_purgatory(game, seat, out);
        return false;
    }
    if (cmd == "zones") {
        print_zones(game, controlled, out);
        return false;
    }
    if (cmd == "float") {
        print_float(game, controlled, out);
        return false;
    }
    if (cmd == "addfloat" && p.size() >= 3) {
        auto et = energy_type_from_string(p[1]);
        int amt = std::stoi(p[2]);
        if (!et) {
            out << "Unknown energy type\n";
            return false;
        }
        game.turn_manager.player_energy[controlled][*et] += amt;
        print_float(game, controlled, out);
        return false;
    }
    if (cmd == "phase") {
        out << "Phase: " << turn_phase_to_string(game.turn_manager.current_phase) << "\n";
        return false;
    }
    if (cmd == "zonepick" && p.size() >= 2) {
        auto cp = game.turn_manager.current_player();
        if (!cp || *cp != controlled) {
            out << "Not your turn\n";
            return false;
        }
        auto& choices = game.turn_manager.pending_energy_choices[*cp];
        const int n = static_cast<int>(choices.size());
        if (n < 1) {
            out << "No zone choices available\n";
            return false;
        }
        const auto idx_opt = parse_cli_index_1based(n, p[1]);
        if (!idx_opt) {
            out << "Invalid zone index (use 1-" << n << ")\n";
            return false;
        }
        auto r = game.choose_energy_zone(controlled, *idx_opt);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "zoneskip") {
        auto r = game.skip_energy_zone(controlled);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    // use_land <territory#> [ability#] [x y]  - activate a placed territory's "use land" ability
    // (1-based territory/ability indices; optional target cell for effects that need one).
    if (cmd == "use_land" && p.size() >= 2) {
        const auto zit = game.players_energy_zones.find(controlled);
        const int zn = (zit == game.players_energy_zones.end()) ? 0 : static_cast<int>(zit->second.size());
        const auto tzone = parse_cli_index_1based(zn, p[1]);
        if (!tzone) {
            out << "Invalid territory index (use 1-" << zn << ")\n";
            return false;
        }
        int ability_idx = 0;
        if (p.size() >= 3) {
            const auto ta = parse_cli_index_1based(64, p[2]);
            if (!ta) {
                out << "Invalid ability index\n";
                return false;
            }
            ability_idx = *ta;
        }
        std::map<std::string, int> targets;
        if (p.size() >= 5) {
            const BoardCellBounds bb = merged_cli_bounds(game);
            if (const auto tcell = parse_grid_cell_1based_world(bb, p[3], p[4])) {
                targets[effect_keys::kCellX] = tcell->first;
                targets[effect_keys::kCellY] = tcell->second;
            }
        }
        auto r = game.use_land(controlled, *tzone, ability_idx, targets);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    // land_target <x> <y>  - resolve a pending territory enter/groundwork effect on a target unit.
    if (cmd == "land_target" && p.size() >= 3) {
        const BoardCellBounds bb = merged_cli_bounds(game);
        const auto tcell = parse_grid_cell_1based_world(bb, p[1], p[2]);
        if (!tcell) {
            out << "Invalid target cell\n";
            return false;
        }
        std::map<std::string, int> targets{{effect_keys::kCellX, tcell->first}, {effect_keys::kCellY, tcell->second}};
        auto r = game.resolve_territory_target(controlled, targets);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "land_target_skip") {
        auto r = game.skip_territory_target(controlled);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        return false;
    }
    if (cmd == "territory_loot_discard" && p.size() >= 2) {
        const auto idx_opt = parse_cli_index_1based(99, p[1]);
        if (!idx_opt) {
            out << "Invalid hand index\n";
            return false;
        }
        auto r = game.territory_loot_discard_at(controlled, *idx_opt);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "territory_loot_skip") {
        auto r = game.territory_loot_skip(controlled);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        return false;
    }
    if (cmd == "deploy" && p.size() >= 4) {
        auto* hand = game.players_hands.at(controlled);
        if (hand->empty()) {
            out << "Hand is empty\n";
            return false;
        }
        const auto idx_opt = parse_cli_index_1based(static_cast<int>(hand->size()), p[1]);
        const BoardCellBounds bb = merged_cli_bounds(game);
        const auto cell_opt = parse_grid_cell_1based_world(bb, p[2], p[3]);
        if (!idx_opt) {
            out << "Invalid hand index (use 1-" << hand->size() << ")\n";
            return false;
        }
        if (!cell_opt) {
            out << "Invalid cell (columns 1-" << bb.span_x() << ", rows 1-" << bb.span_y() << ")\n";
            return false;
        }
        const int idx = *idx_opt;
        const auto [x, y] = *cell_opt;
        const CardInstanceId cid = (*hand)[static_cast<size_t>(idx)];
        const CardDefinition* def = cli_card_def(game, controlled, cid);
        if (!def || !definition_is_unit(*def)) {
            out << "Selected card is not deployable\n";
            return false;
        }
        DeployAction a(cid, controlled, {x, y});
        auto r = game.perform_action(controlled, a);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "deploy_reserve" && p.size() >= 4) {
        auto& reserves = game.players_decks.at(controlled).reserves;
        if (reserves.empty()) {
            out << "Reserves are empty\n";
            return false;
        }
        const auto idx_opt = parse_cli_index_1based(static_cast<int>(reserves.size()), p[1]);
        const BoardCellBounds bb = merged_cli_bounds(game);
        const auto cell_opt = parse_grid_cell_1based_world(bb, p[2], p[3]);
        if (!idx_opt) {
            out << "Invalid reserves index (use 1-" << reserves.size() << ")\n";
            return false;
        }
        if (!cell_opt) {
            out << "Invalid cell (columns 1-" << bb.span_x() << ", rows 1-" << bb.span_y() << ")\n";
            return false;
        }
        const int idx = *idx_opt;
        const auto [x, y] = *cell_opt;
        const CardInstanceId cid = reserves[static_cast<size_t>(idx)];
        const CardDefinition* def = cli_card_def(game, controlled, cid);
        if (!def || !definition_is_unit(*def)) {
            out << "Selected reserves card is not deployable\n";
            return false;
        }
        DeployAction a(cid, controlled, {x, y}, CardPlayZone::Reserves);
        auto r = game.perform_action(controlled, a);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "select" && p.size() >= 3) {
        const BoardCellBounds bb = merged_cli_bounds(game);
        const auto cell_opt = parse_grid_cell_1based_world(bb, p[1], p[2]);
        if (!cell_opt) {
            out << "Invalid cell (columns 1-" << bb.span_x() << ", rows 1-" << bb.span_y() << ")\n";
            return false;
        }
        const auto [x, y] = *cell_opt;
        auto e = game.board.entity_at(x, y);
        auto u = std::dynamic_pointer_cast<Unit>(e);
        if (!u || !u->owner || *u->owner != controlled) {
            out << "No controllable unit/building there\n";
            return false;
        }
        game.reconcile_pending_move_for_unit_selection(controlled, u->entity_id);
        selected = u;
        out << "Selected " << selected->entity_id << "\n";
        return false;
    }
    if (cmd == "deselect") {
        game.clear_pending_move_for(controlled);
        selected.reset();
        out << "Selection cleared.\n";
        return false;
    }
    if (cmd == "rotate") {
        out << "Use move_rotate while you have a pending move preview (after move_preview / move).\n";
        return false;
    }
    if (cmd == "move_rotate") {
        int q_rot = 1;
        if (p.size() >= 2) {
            if (!parse_quarter_turns_cli(p[1], q_rot, out)) {
                return false;
            }
        }
        MovePendingRotateAction a(controlled, q_rot);
        auto r = game.perform_action(controlled, a);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "move_confirm") {
        MoveConfirmAction a(controlled);
        auto r = game.perform_action(controlled, a);
        if (out_move_performed_ok) {
            *out_move_performed_ok = r.ok;
        }
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "move_cancel") {
        MoveCancelAction a(controlled);
        auto r = game.perform_action(controlled, a);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if ((cmd == "move_preview" || cmd == "move") && p.size() >= 3) {
        if (!selected) {
            out << "Select a unit first\n";
            return false;
        }
        if (p.size() >= 4) {
            out << "Rotation is no longer combined with move. Use move_preview then move_rotate, then move_confirm.\n";
            return false;
        }
        const BoardCellBounds bb = merged_cli_bounds(game);
        const auto cell_opt = parse_grid_cell_1based_world(bb, p[1], p[2]);
        if (!cell_opt) {
            out << "Invalid cell (columns 1-" << bb.span_x() << ", rows 1-" << bb.span_y() << ")\n";
            return false;
        }
        const auto [x, y] = *cell_opt;
        MovePreviewAction a(selected, controlled, {x, y});
        auto r = game.perform_action(controlled, a);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "attack" && p.size() >= 3) {
        if (!selected) {
            out << "Select a unit first\n";
            return false;
        }
        const BoardCellBounds bb = merged_cli_bounds(game);
        const auto cell_opt = parse_grid_cell_1based_world(bb, p[1], p[2]);
        if (!cell_opt) {
            out << "Invalid cell (columns 1-" << bb.span_x() << ", rows 1-" << bb.span_y() << ")\n";
            return false;
        }
        const auto [x, y] = *cell_opt;
        bool ranged = false;
        if (selected->position) {
            int dx = std::abs(selected->position->first - x);
            int dy = std::abs(selected->position->second - y);
            ranged = std::max(dx, dy) > 1;
        }
        AttackAction a(selected, controlled, {x, y}, ranged);
        auto r = game.perform_action(controlled, a);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    // End Main Phase: commits spell batch (if any) and enters Attack Declaration.
    if (cmd == "end_main") {
        auto r = game.end_main_phase(controlled);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    // Undeclare an attack: attack_undeclare <entity_id>
    if (cmd == "attack_undeclare" && p.size() >= 2) {
        auto r = game.undeclare_attack(controlled, p[1]);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        return false;
    }
    // Lock attack declarations and open the Defense window.
    if (cmd == "attack_commit") {
        auto r = game.commit_attack_declaration(controlled);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    // Pass / forfeit in either the Spell reaction window or the Defense window.
    if (cmd == "pass_defense") {
        const auto phase = game.turn_manager.current_phase;
        ActionResult r;
        if (phase == TurnPhase::SpellWindow || phase == TurnPhase::SecondSpellWindow) {
            r = game.pass_spell_window(controlled);
        } else {
            r = game.pass_defense_window(controlled);
        }
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "defend") {
        if (!selected) {
            out << "Select a unit first\n";
            return false;
        }
        DefendAction a(selected, controlled);
        auto r = game.perform_action(controlled, a);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "dash") {
        if (!selected) {
            out << "Select a unit first\n";
            return false;
        }
        DashAction a(selected, controlled);
        auto r = game.perform_action(controlled, a);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "recover") {
        if (!selected) {
            out << "Select a unit first\n";
            return false;
        }
        RecoverAction a(selected, controlled);
        auto r = game.perform_action(controlled, a);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "cast" && p.size() >= 2) {
        auto* hand = game.players_hands.at(controlled);
        if (hand->empty()) {
            out << "Hand is empty\n";
            return false;
        }
        const auto idx_opt = parse_cli_index_1based(static_cast<int>(hand->size()), p[1]);
        if (!idx_opt) {
            out << "Invalid hand index (use 1-" << hand->size() << ")\n";
            return false;
        }
        const int idx = *idx_opt;
        const CardInstanceId cid = (*hand)[static_cast<size_t>(idx)];
        const CardDefinition* def = cli_card_def(game, controlled, cid);
        if (!def || !definition_is_spell(*def)) {
            out << "Selected card is not a spell\n";
            return false;
        }
        const SpellCardDefinition& spell = definition_spell(*def);
        size_t target_start = 2;
        int mode_index = -1;
        if (!try_parse_spell_mode_arg(*def, p, target_start, mode_index, out)) {
            return false;
        }
        ParsedSpellCastCliTargets parsed;
        if (!parse_spell_cast_cli_targets(game, *def, p, target_start, parsed, out, mode_index)) {
            if (definition_spell_multicast_requires_per_copy_targets(*def)) {
                out << "Multicast cast: cast " << (idx + 1)
                    << " <col1> <row1> [... up to " << definition_multicast_amount(*def) << " distinct targets]\n";
            } else if (definition_spell_requires_stack_target(*def)) {
                out << "Spell " << def->name << " needs a stack target: cast " << (idx + 1) << " stack <stack_id>\n";
            } else if (definition_spell_requires_player_seat_target(*def)) {
                out << "Spell " << def->name << " needs a target player: cast " << (idx + 1) << " player <seat>\n";
            } else if (spell_requires_focus_caster(*def)) {
                out << "Focus spell " << def->name << " needs a target cell: cast " << (idx + 1) << " <col> <row>"
                    << (spell.x_cost_energy_type.has_value() ? " <X>" : "") << " (select your casting unit first)\n";
            }
            return false;
        }
        std::shared_ptr<Entity> focus_caster;
        if (spell_requires_focus_caster(*def)) {
            if (!selected) {
                out << "Select a friendly unit to cast this focus spell from\n";
                return false;
            }
            focus_caster = std::static_pointer_cast<Entity>(selected);
        } else if (tactics::spell_requires_forced_damage_spell_focus_caster(game, controlled, *def)) {
            if (!selected || !cast_uses_forced_damage_spell_focus_caster(game, *def, selected)) {
                out << "Select a friendly unit with Insatiable Focus to cast this damaging spell\n";
                return false;
            }
            focus_caster = std::static_pointer_cast<Entity>(selected);
        }
        CastSpellAction a(cid, controlled, parsed.primary_targets, parsed.stack_target_id, focus_caster);
        if (mode_index >= 0) {
            a.set_mode_index(mode_index);
        }
        if (!parsed.multicast_targets.empty()) {
            a.set_multicast_targets(parsed.multicast_targets);
        }
        // X-cost spells: parse optional x_amount.  Syntax: cast <idx> [<col> <row>] <X>
        if (spell.x_cost_energy_type.has_value()) {
            const size_t x_pos = parsed.next_arg_index;
            if (p.size() > x_pos) {
                try {
                    a.set_x_amount(std::stoi(p[x_pos]));
                } catch (...) {
                    out << "Invalid X amount '" << p[x_pos] << "' - expected an integer\n";
                    return false;
                }
            } else if (spell.x_cost_min > 0) {
                out << def->name << " requires choosing an X (minimum " << spell.x_cost_min
                    << "): cast " << (idx + 1) << " <targets...> <X>\n";
                return false;
            }
        }
        auto r = game.perform_action(controlled, a);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "cast_reserve" && p.size() >= 2) {
        auto& reserves = game.players_decks.at(controlled).reserves;
        if (reserves.empty()) {
            out << "Reserves are empty\n";
            return false;
        }
        const auto idx_opt = parse_cli_index_1based(static_cast<int>(reserves.size()), p[1]);
        if (!idx_opt) {
            out << "Invalid reserves index (use 1-" << reserves.size() << ")\n";
            return false;
        }
        const int idx = *idx_opt;
        const CardInstanceId cid = reserves[static_cast<size_t>(idx)];
        const CardDefinition* def = cli_card_def(game, controlled, cid);
        if (!def || !definition_is_spell(*def)) {
            out << "Selected reserves card is not a spell\n";
            return false;
        }
        const SpellCardDefinition& spell_r = definition_spell(*def);
        size_t target_start = 2;
        int mode_index = -1;
        if (!try_parse_spell_mode_arg(*def, p, target_start, mode_index, out)) {
            return false;
        }
        ParsedSpellCastCliTargets parsed;
        if (!parse_spell_cast_cli_targets(game, *def, p, target_start, parsed, out, mode_index)) {
            if (definition_spell_multicast_requires_per_copy_targets(*def)) {
                out << "Multicast cast_reserve: cast_reserve " << (idx + 1)
                    << " <col1> <row1> [... up to " << definition_multicast_amount(*def) << " distinct targets]\n";
            } else if (definition_spell_requires_stack_target(*def)) {
                out << "Spell " << def->name << " needs a stack target: cast_reserve " << (idx + 1) << " stack <stack_id>\n";
            } else if (definition_spell_requires_player_seat_target(*def)) {
                out << "Spell " << def->name << " needs a target player: cast_reserve " << (idx + 1) << " player <seat>\n";
            } else if (spell_requires_focus_caster(*def)) {
                out << "Focus spell " << def->name << " needs a target cell: cast_reserve " << (idx + 1) << " <col> <row>"
                    << (spell_r.x_cost_energy_type.has_value() ? " <X>" : "") << "\n";
            }
            return false;
        }
        std::shared_ptr<Entity> focus_caster;
        if (spell_requires_focus_caster(*def)) {
            if (!selected) {
                out << "Select a friendly unit to cast this focus spell from\n";
                return false;
            }
            focus_caster = std::static_pointer_cast<Entity>(selected);
        } else if (tactics::spell_requires_forced_damage_spell_focus_caster(game, controlled, *def)) {
            if (!selected || !cast_uses_forced_damage_spell_focus_caster(game, *def, selected)) {
                out << "Select a friendly unit with Insatiable Focus to cast this damaging spell\n";
                return false;
            }
            focus_caster = std::static_pointer_cast<Entity>(selected);
        }
        CastSpellAction a(cid, controlled, parsed.primary_targets, parsed.stack_target_id, focus_caster, CardPlayZone::Reserves);
        if (mode_index >= 0) {
            a.set_mode_index(mode_index);
        }
        if (!parsed.multicast_targets.empty()) {
            a.set_multicast_targets(parsed.multicast_targets);
        }
        if (spell_r.x_cost_energy_type.has_value()) {
            const size_t x_pos = parsed.next_arg_index;
            if (p.size() > x_pos) {
                try {
                    a.set_x_amount(std::stoi(p[x_pos]));
                } catch (...) {
                    out << "Invalid X amount '" << p[x_pos] << "' - expected an integer\n";
                    return false;
                }
            } else if (spell_r.x_cost_min > 0) {
                out << def->name << " requires choosing an X (minimum " << spell_r.x_cost_min
                    << "): cast_reserve " << (idx + 1) << " <targets...> <X>\n";
                return false;
            }
        }
        auto r = game.perform_action(controlled, a);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "ability" && p.size() >= 2) {
        if (!selected) {
            out << "Select a unit first\n";
            return false;
        }
        const std::string& key = p[1];
        std::optional<AbilitySpec> spec;
        for (const auto& ab : selected->activated_abilities) {
            if (ab.key == key) {
                spec = ab;
                break;
            }
        }
        if (!spec) {
            out << "No ability '" << key << "' on selected unit\n";
            return false;
        }
        std::map<std::string, int> targets{};
        std::string stack_target_id;
        int ability_x_amount = 0;
        if (p.size() >= 4 && p[2] == "stack") {
            stack_target_id = p[3];
            if (spec->x_cost_energy_type.has_value()) {
                if (p.size() < 5) {
                    out << "Ability " << key << " requires X: ability " << key << " stack <stack_id> <X>\n";
                    return false;
                }
                try {
                    ability_x_amount = std::stoi(p[4]);
                } catch (...) {
                    out << "Invalid X amount '" << p[4] << "' - expected an integer\n";
                    return false;
                }
            }
        } else if (ability_requires_board_target(*spec) || effect_key_targets_empty_cell(spec->effect_key)) {
            if (p.size() < 4) {
                out << "Ability " << key << " needs a target cell: ability " << key << " <x> <y>\n";
                return false;
            }
            const BoardCellBounds bb = merged_cli_bounds(game);
            const auto tcell = parse_grid_cell_1based_world(bb, p[2], p[3]);
            if (!tcell) {
                out << "Invalid ability target cell (columns 1-" << bb.span_x() << ", rows 1-" << bb.span_y() << ")\n";
                return false;
            }
            targets[effect_keys::kCellX] = tcell->first;
            targets[effect_keys::kCellY] = tcell->second;
        } else if (target_definition_for_effect_key(spec->effect_key).domain == TargetDomain::StackItem) {
            out << "Ability " << key << " needs a stack target: ability " << key << " stack <stack_id>\n";
            return false;
        }
        ActivateAbilityAction a(std::static_pointer_cast<Entity>(selected), controlled, key, targets, stack_target_id);
        if (spec->x_cost_energy_type.has_value()) {
            a.set_x_amount(ability_x_amount);
        }
        auto r = game.perform_action(controlled, a);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "batch_cancel" && p.size() >= 2) {
        auto r = game.cancel_queued_batch_item_for_player(controlled, p[1]);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "undo") {
        auto r = game.undo_last_action(controlled);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "match_setting" && p.size() >= 3) {
        const std::string key = p[1];
        const bool on = p[2] == "1" || p[2] == "true" || p[2] == "on";
        if (key == "allow_deployment_undo") {
            game.set_allow_deployment_undo(on);
            out << "allow_deployment_undo = " << (on ? "1" : "0") << "\n";
        } else {
            out << "Failed: unknown match setting '" << key << "'\n";
        }
        return false;
    }
    if (cmd == "pass") {
        auto r = game.pass_priority(controlled);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "discard" && p.size() >= 2) {
        if (!game.IsAwaitingHandDiscard()) {
            out << "Not in discard step\n";
            return false;
        }
        if (!game.IsPendingDiscardForPlayer(controlled)) {
            out << "You are not choosing discards\n";
            return false;
        }
        auto* hand = game.players_hands.at(controlled);
        const auto idx_opt = parse_cli_index_1based(static_cast<int>(hand->size()), p[1]);
        if (!idx_opt) {
            out << "Invalid hand index (use 1-" << hand->size() << ")\n";
            return false;
        }
        const int idx_1based = *idx_opt + 1;
        auto r = game.discard_hand_card_at(controlled, idx_1based);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "scan_discard" && p.size() >= 2) {
        if (!game.IsAwaitingScan()) {
            out << "Not in scan step\n";
            return false;
        }
        if (!game.IsPendingScanForPlayer(controlled)) {
            out << "You are not choosing scan discards\n";
            return false;
        }
        const auto* peeked = game.pending_scan_peeked_for(controlled);
        if (!peeked) {
            out << "No peeked cards\n";
            return false;
        }
        const auto idx_opt = parse_cli_index_1based(static_cast<int>(peeked->size()), p[1]);
        if (!idx_opt) {
            out << "Invalid scan index (use 1-" << peeked->size() << ")\n";
            return false;
        }
        const int idx_1based = *idx_opt + 1;
        auto r = game.scan_discard_at(controlled, idx_1based);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "scan_finish") {
        if (!game.IsAwaitingScan()) {
            out << "Not in scan step\n";
            return false;
        }
        if (!game.IsPendingScanForPlayer(controlled)) {
            out << "You are not choosing scan discards\n";
            return false;
        }
        auto r = game.scan_finish(controlled);
        out << (r.ok ? "" : "Failed: ") << r.message << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "board") {
        print_board(game, out);
        master_cli_write_board_ppm(game, out);
        return false;
    }
    if (cmd == "plot") {
        if (plot.plot_board) plot.plot_board(game);
        else out << "plot: not available in this host (use tactics_master_cli on Windows for native window).\n";
        return false;
    }
    // place_pickup <x> <y> [effect_key [payload_key payload_value ...]]
    // Example: place_pickup 3 5 draw_cards amount 2
    if (cmd == "place_pickup" && p.size() >= 3) {
        const int wx = std::stoi(p[1]);
        const int wy = std::stoi(p[2]);
        std::string effect_key;
        std::map<std::string, int> payload;
        if (p.size() >= 4) {
            effect_key = p[3];
            for (size_t i = 4; i + 1 < p.size(); i += 2) {
                payload[p[i]] = std::stoi(p[i + 1]);
            }
        }
        const bool ok = place_map_pickup(game, wx, wy, effect_key, payload);
        out << (ok ? "Pickup placed at (" + p[1] + "," + p[2] + ")"
                   : "Failed: could not place pickup (cell void/damaging/occupied or out of bounds)")
            << "\n";
        master_cli_write_board_ppm(game, out);
        return false;
    }

    if (cmd == "sandbox_deck") {
        if (!game_id_is_sandbox(game.game_id())) {
            out << "sandbox_deck is only available in sandbox matches.\n";
            return false;
        }
        const std::string faction = (p.size() >= 2) ? p[1] : "all";
        std::string err;
        if (!apply_sandbox_faction_deck_to_all_players(game, faction, &err)) {
            out << (err.empty() ? "Failed to apply sandbox faction deck." : err) << "\n";
            if (faction == "all" || faction.empty()) {
                out << "Valid factions: gallantry, ingenuity, mythology, core, all\n";
            }
            return false;
        }
        const auto deck_it = game.players_decks.find(controlled);
        const std::size_t hand_count = (deck_it != game.players_decks.end()) ? deck_it->second.hand.size() : 0;
        out << "All players' hands replaced with faction '" << faction << "' ("
            << hand_count << " cards each for P" << controlled << ").\n";
        return false;
    }

    out << "Unknown command. Type 'help'.\n";
    return false;
}

}  // namespace tactics
