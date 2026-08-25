#include "tactics/core.hpp"
#include "tactics/cards/card_runtime.hpp"

#include <iostream>
#include <string>

int main() {
    tactics::GameState game("local_cpp", 8, 8);
    for (int i = 1; i <= tactics::kDefaultDemoSeatCount; ++i) {
        game.add_player(i, "Player " + std::to_string(i));
    }
    game.start_game();

    auto r = game.choose_energy_zone(1, 0);
    std::cout << "choose_energy_zone: " << (r.ok ? "ok" : "fail") << " - " << r.message << "\n";

    auto* hand = game.players_hands.at(1);
    tactics::CardInstanceId deploy_id;
    for (const tactics::CardInstanceId cid : *hand) {
        const tactics::CardInstance& inst = game.players_decks.at(1).pool.at(cid);
        if (const tactics::CardDefinition* def = tactics::try_get_card_definition_ptr(inst.definition_id)) {
            if (tactics::definition_is_unit(*def)) {
                deploy_id = cid;
                break;
            }
        }
    }

    if (deploy_id.is_valid()) {
        tactics::DeployAction deploy(deploy_id, 1, {0, 0});
        auto dr = game.perform_action(1, deploy);
        std::cout << "deploy: " << (dr.ok ? "ok" : "fail") << " - " << dr.message << "\n";
    } else {
        std::cout << "deploy: fail - no unit card in hand\n";
    }

    return 0;
}
