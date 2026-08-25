#pragma once

#include "tactics/cards/card_catalog.hpp"

#include <string>

namespace tactics {

using ProjectContentFileReader = CardCatalogFileReader;

struct ProjectContentLoadOptions {
    bool load_project_cards{true};
    bool load_ability_catalog{true};
    bool load_passive_catalog{true};
    bool load_status_catalog{true};
    bool load_glossary_copy{true};
    bool load_starter_deck{true};
    bool load_test_deck{true};
};

/** Loads built-in catalogs plus optional project JSON shards (cards, abilities, passives, decks). */
bool load_all_project_content(const ProjectContentFileReader& read_file, std::string& err_out,
    const ProjectContentLoadOptions& options = {});

}  // namespace tactics