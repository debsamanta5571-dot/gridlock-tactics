#include "tactics/board/board_layout.hpp"

#include "tactics/board/aether.hpp"
#include "tactics/board/scanner.hpp"
#include "tactics/board/omni_energy_tile.hpp"
#include "tactics/board/tile_modifiers.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/entities/entity.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace tactics {
namespace {

struct MapTerrainCell {
    int wx;
    int wy;
    const char* name;
    float movement_cost;
    int damage_on_enter;
    bool is_void;
};

void apply_terrain_cell(GameBoard& board, const MapTerrainCell& c)
{
    auto sq = board.get_square(c.wx, c.wy);
    if (!sq) {
        return;
    }
    // Replace the terrain-layer modifier for this cell (overlay is left untouched).
    set_terrain_modifier(*sq, SquareModifier{.name=c.name, .layer=TileLayer::Terrain, .movement_cost=c.movement_cost, .damage_on_enter=c.damage_on_enter, .is_void=c.is_void});
}

// N2: layout entities get a proper spawn_sequence so ordering is deterministic on snapshot restore.
//     participates_in_passive_order = false makes the opt-out explicit rather than relying solely
//     on the entity_type string check in entity_participates_in_passive_action_order().
void place_map_obstacle(GameState& game, int wx, int wy, int& idx)
{
    auto obs = std::make_shared<Entity>();
    obs->entity_id = "map_obstacle_" + std::to_string(++idx);
    obs->entity_type = "obstacle";
    obs->line_of_sight_blocked = true;
    obs->participates_in_passive_order = false;
    obs->shape = {{0, 0}};
    normalize_entity_shape(*obs);
    if (game.board.place_entity(obs, wx, wy)) {
        game.note_entity_placed(obs);
    }
}

void place_map_low_cover(GameState& game, int wx, int wy, int& idx)
{
    auto cover = std::make_shared<Unit>();
    cover->entity_id = "map_low_cover_" + std::to_string(++idx);
    cover->entity_type = "low_cover";
    cover->unit_type = "Low Cover";
    cover->attack_type = AttackType::Utility;
    cover->base_health = 4;
    cover->current_health = 4;
    cover->movement = 0;
    cover->line_of_sight_blocked = false;
    cover->participates_in_passive_order = false;
    cover->shape = {{0, 0}};
    normalize_entity_shape(*cover);
    if (game.board.place_entity(cover, wx, wy)) {
        game.note_entity_placed(cover);
    }
}

}  // namespace

bool place_map_pickup(GameState& game, int wx, int wy,
    const std::string& effect_key,
    const std::map<std::string, int>& payload)
{
    const auto sq = game.board.get_square(wx, wy);
    if (!sq) {
        return false;
    }
    // Reject void or damaging terrain.
    for (const auto& mod : sq->modifiers) {
        if (mod.is_void || mod.damage_on_enter > 0) {
            return false;
        }
    }
    // Reject occupied cells.
    if (sq->occupied) {
        return false;
    }
    auto pickup = std::make_shared<Entity>();
    pickup->entity_type = "pickup";
    pickup->base_health = 1;
    pickup->current_health = 1;
    pickup->line_of_sight_blocked = false;
    pickup->participates_in_passive_order = false;
    pickup->pickup_effect_key = effect_key;
    pickup->pickup_payload = payload;
    pickup->shape = {{0, 0}};
    normalize_entity_shape(*pickup);
    game.assign_monotonic_entity_id(pickup, "pickup_" + std::to_string(wx) + "_" + std::to_string(wy));
    if (!game.board.place_entity(pickup, wx, wy)) {
        return false;
    }
    game.note_entity_placed(pickup);
    return true;
}

BoardLayoutSpec make_rect_board_layout(int width, int height)
{
    BoardLayoutSpec spec;
    spec.nominal_width = width;
    spec.nominal_height = height;
    spec.rects.push_back(BoardRectModule{"main", width, height, 0, 0});
    return spec;
}

BoardLayoutSpec make_default_map_layout()
{
    BoardLayoutSpec spec;
    spec.nominal_width = kStandardBoardWidth;
    spec.nominal_height = kStandardBoardHeight;
    // 8x8 play area in the middle (rows 2-9).
    spec.rects.push_back(BoardRectModule{"arena", kStandardBoardWidth, 8, 0, kPlayerBaseDepth});
    // 4x2 base pads on the bottom and top only (centered on the 8-wide board).
    const int base_x = player_base_anchor_x(kStandardBoardWidth);
    spec.rects.push_back(BoardRectModule{"base_pad_p1", kPlayerBaseWidth, kPlayerBaseDepth, base_x, 0});
    spec.rects.push_back(
        BoardRectModule{"base_pad_p2", kPlayerBaseWidth, kPlayerBaseDepth, base_x, kStandardBoardHeight - kPlayerBaseDepth});
    spec.base_zone_p1 = PlayerBaseZone{base_x, 0};
    spec.base_zone_p2 = PlayerBaseZone{base_x, kStandardBoardHeight - kPlayerBaseDepth};
    spec.deploy_zone_p1 = BoardRectZone{base_x, kPlayerBaseDepth, kPlayerBaseWidth, kPlayerBaseDepth};
    spec.deploy_zone_p2 =
        BoardRectZone{base_x, kStandardBoardHeight - kPlayerBaseDepth - kPlayerBaseDepth, kPlayerBaseWidth, kPlayerBaseDepth};
    spec.layout_id = kDefaultBoardLayoutId;
    return spec;
}

BoardLayoutSpec make_standard_duel_layout(int width, int height)
{
    if (width == kStandardBoardWidth && height == kStandardBoardHeight) {
        return make_default_map_layout();
    }
    return make_rect_board_layout(width, height);
}

BoardLayoutSpec make_2v2_map_layout()
{
    BoardLayoutSpec spec;
    spec.nominal_width = k2v2BoardWidth;
    spec.nominal_height = k2v2BoardHeight;

    // Full-width arena spanning the rows between the top and bottom base pads (rows 2-9).
    const int arena_h = k2v2BoardHeight - 2 * kPlayerBaseDepth;
    spec.rects.push_back(BoardRectModule{"arena", k2v2BoardWidth, arena_h, 0, kPlayerBaseDepth});

    // Two 4x2 base pads on each edge, spaced apart (a gap between the same-side bases).
    const int left_x = 1;    // cols 1-4
    const int right_x = 7;   // cols 7-10
    const int bottom_y = k2v2BoardHeight - kPlayerBaseDepth;  // rows 10-11
    spec.rects.push_back(BoardRectModule{"base_pad_p1", kPlayerBaseWidth, kPlayerBaseDepth, left_x, 0});
    spec.rects.push_back(BoardRectModule{"base_pad_p3", kPlayerBaseWidth, kPlayerBaseDepth, right_x, 0});
    spec.rects.push_back(BoardRectModule{"base_pad_p2", kPlayerBaseWidth, kPlayerBaseDepth, left_x, bottom_y});
    spec.rects.push_back(BoardRectModule{"base_pad_p4", kPlayerBaseWidth, kPlayerBaseDepth, right_x, bottom_y});

    // Team A (top): seats 1 & 3. Team B (bottom): seats 2 & 4. (Seats 1/2 stay opposite teams.)
    spec.base_zone_p1 = PlayerBaseZone{left_x, 0};
    spec.base_zone_p3 = PlayerBaseZone{right_x, 0};
    spec.base_zone_p2 = PlayerBaseZone{left_x, bottom_y};
    spec.base_zone_p4 = PlayerBaseZone{right_x, bottom_y};

    // One-row deploy strip in front of each base, leaving rows 3-4 as a neutral objective strip.
    const int top_deploy_y = kPlayerBaseDepth;                    // row 2
    const int bottom_deploy_y = k2v2BoardHeight - kPlayerBaseDepth - 1;  // row 5
    spec.deploy_zone_p1 = BoardRectZone{left_x, top_deploy_y, kPlayerBaseWidth, 1};
    spec.deploy_zone_p3 = BoardRectZone{right_x, top_deploy_y, kPlayerBaseWidth, 1};
    spec.deploy_zone_p2 = BoardRectZone{left_x, bottom_deploy_y, kPlayerBaseWidth, 1};
    spec.deploy_zone_p4 = BoardRectZone{right_x, bottom_deploy_y, kPlayerBaseWidth, 1};

    spec.layout_id = k2v2BoardLayoutId;
    return spec;
}

BoardLayoutSpec make_sandbox_map_layout() { return make_default_map_layout(); }

bool apply_board_layout(GameBoard& board, const BoardLayoutSpec& spec)
{
    for (const BoardRectModule& rect : spec.rects) {
        if (!board.add_modular_grid(rect.id, rect.width, rect.height, rect.offset_x, rect.offset_y)) {
            return false;
        }
    }
    for (const BoardCellModule& cells : spec.cell_modules) {
        if (!board.add_cell_module(cells.id, cells.cells)) {
            return false;
        }
    }
    return true;
}

void ensure_standard_duel_permanent_map_tiles(GameState& game)
{
    if (game.board_width() != kStandardBoardWidth || game.board_height() != kStandardBoardHeight) {
        return;
    }
    if (game.board_layout().layout_id != kDefaultBoardLayoutId) {
        return;
    }
    if (game.aether_clusters_.empty()) {
        seed_standard_duel_aether_tiles(game);
    }
    if (game.scanner_clusters_.empty()) {
        seed_standard_duel_scanner_tiles(game);
    }
    if (game.omni_energy_clusters_.empty()) {
        seed_standard_duel_omni_energy_tiles(game);
    }
}

void seed_2v2_map_objectives(GameState& game, bool scanner, bool omni, bool aether)
{
    if (game.board_layout().layout_id != std::string(k2v2BoardLayoutId)) {
        return;
    }
    // Objectives at the map's mid-height (rows 5-6): scanners on the LEFT edge, omni on the RIGHT edge,
    // aether dead center. Home seats 1 (team A) and 2 (team B) give each team one scanner / omni to
    // contest, equidistant from both teams' top/bottom bases.
    if (scanner) {
        seed_scanner_cluster(game, ScannerClusterSpec{"teamA_scanner", 1, {{0, 5}}});
        seed_scanner_cluster(game, ScannerClusterSpec{"teamB_scanner", 2, {{0, 6}}});
    }
    if (aether) {
        seed_aether_cluster(game, AetherClusterSpec{"center_aether", {{5, 5}, {6, 5}, {5, 6}, {6, 6}}});
    }
    if (omni) {
        seed_omni_energy_cluster(game, OmniEnergyClusterSpec{"teamA_omni", 1, {{11, 5}}});
        seed_omni_energy_cluster(game, OmniEnergyClusterSpec{"teamB_omni", 2, {{11, 6}}});
    }
}

void seed_standard_duel_objectives(GameState& game, bool scanner, bool omni, bool aether)
{
    if (game.board_width() != kStandardBoardWidth || game.board_height() != kStandardBoardHeight) {
        return;
    }
    if (game.board_layout().layout_id != kDefaultBoardLayoutId) {
        return;
    }
    if (aether && game.aether_clusters_.empty()) {
        seed_standard_duel_aether_tiles(game);
    }
    if (scanner && game.scanner_clusters_.empty()) {
        seed_standard_duel_scanner_tiles(game);
    }
    if (omni && game.omni_energy_clusters_.empty()) {
        seed_standard_duel_omni_energy_tiles(game);
    }
}

void seed_standard_duel_map_features(GameState& game)
{
    if (game.board_width() != kStandardBoardWidth || game.board_height() != kStandardBoardHeight) {
        return;
    }
    if (game.board_layout().layout_id != kDefaultBoardLayoutId) {
        return;
    }

    // Arena center (rows 4-8): roads, rough edges, hazard strip, void pockets - avoids 4x2 deploy strips (rows 2-3, 8-9).
    const std::vector<MapTerrainCell> terrain = {
        {1, 4, "road", 0.5f, 0, false},
        {2, 4, "road", 0.5f, 0, false},
        {5, 4, "road", 0.5f, 0, false},
        {6, 4, "road", 0.5f, 0, false},
        {3, 4, "road", 0.5f, 0, false},
        {4, 4, "road", 0.5f, 0, false},
        {1, 5, "road", 0.5f, 0, false},
        {6, 5, "road", 0.5f, 0, false},
        {1, 6, "road", 0.5f, 0, false},
        {6, 6, "road", 0.5f, 0, false},
        {0, 5, "rough", 2.0f, 0, false},
        {7, 5, "rough", 2.0f, 0, false},
        {0, 6, "rough", 2.0f, 0, false},
        {7, 6, "rough", 2.0f, 0, false},
        {0, 7, "rough", 2.0f, 0, false},
        {7, 7, "rough", 2.0f, 0, false},
        {5, 8, "hazard", 1.0f, 2, false},
        {6, 8, "hazard", 1.0f, 2, false},
        {0, 8, "void", 1.0f, 0, true},
        {7, 8, "void", 1.0f, 0, true},
    };
    for (const MapTerrainCell& cell : terrain) {
        apply_terrain_cell(game.board, cell);
    }

    seed_standard_duel_aether_tiles(game);
    seed_standard_duel_scanner_tiles(game);
    seed_standard_duel_omni_energy_tiles(game);

    const std::vector<std::pair<int, int>> obstacle_cells = {
        {2, 5},
        {5, 5},
        {6, 7},
    };
    const std::vector<std::pair<int, int>> low_cover_cells = {
        {1, 7},
        {1, 5},
        {6, 5},
    };
    int obstacle_idx = 0;
    for (const auto& [wx, wy] : obstacle_cells) {
        if (game.board.entity_at(wx, wy)) {
            continue;
        }
        place_map_obstacle(game, wx, wy, obstacle_idx);
    }
    int low_cover_idx = 0;
    for (const auto& [wx, wy] : low_cover_cells) {
        if (game.board.entity_at(wx, wy)) {
            continue;
        }
        place_map_low_cover(game, wx, wy, low_cover_idx);
    }
}

}  // namespace tactics
