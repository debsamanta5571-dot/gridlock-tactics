#include "tactics/content/project_content.hpp"

#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/passive_catalog.hpp"
#include "tactics/content/glossary_copy.hpp"
#include "tactics/effects/status_effect_catalog.hpp"

namespace tactics {
namespace {

constexpr const char kAbilityCatalogPath[] = "TacticsData/ability_catalog.json";
constexpr const char kPassiveCatalogPath[] = "TacticsData/passive_catalog.json";
constexpr const char kStatusCatalogPath[] = "TacticsData/status_effects.json";
constexpr const char kGlossaryCopyPath[] = "TacticsData/glossary_copy.json";
constexpr const char kStarterDeckPath[] = "TacticsData/decks/starter_deck.json";
constexpr const char kTestDeckPath[] = "TacticsData/decks/test_deck.json";

bool load_optional_json(const ProjectContentFileReader& read_file, const char* path,
    const auto& loader, std::string& err_out)
{
    std::string utf8;
    std::string read_err;
    if (!read_file(path, utf8, read_err)) {
        return true;
    }
    return loader(utf8, err_out);
}

}  // namespace

bool load_all_project_content(const ProjectContentFileReader& read_file, std::string& err_out,
    const ProjectContentLoadOptions& options)
{
    // Load ability and passive catalogs BEFORE card shards so that validate_ability_ids /
    // validate_passive_ids (called per-card during shard parsing) see the full project catalog
    // and not just the compiled-in builtins. Cards that reference abilities defined only in
    // ability_catalog.json (e.g. Lost Kingdom cards) would otherwise fail validation.
    if (options.load_ability_catalog) {
        if (!load_optional_json(read_file, kAbilityCatalogPath,
                [](const std::string& utf8, std::string& err) {
                    return load_ability_catalog_from_json_utf8(utf8, err);
                },
                err_out)) {
            return false;
        }
    }

    if (options.load_passive_catalog) {
        if (!load_optional_json(read_file, kPassiveCatalogPath,
                [](const std::string& utf8, std::string& err) {
                    return load_passive_catalog_from_json_utf8(utf8, err);
                },
                err_out)) {
            return false;
        }
    }

    if (options.load_project_cards) {
        if (!load_project_card_catalogs(read_file, err_out)) {
            return false;
        }
    }

    if (options.load_status_catalog) {
        if (!load_optional_json(read_file, kStatusCatalogPath,
                [](const std::string& utf8, std::string& err) {
                    return load_status_effect_catalog_from_json_utf8(utf8, err);
                },
                err_out)) {
            return false;
        }
    }

    if (options.load_glossary_copy) {
        if (!load_optional_json(read_file, kGlossaryCopyPath,
                [](const std::string& utf8, std::string& err) {
                    return load_glossary_copy_from_json_utf8(utf8, err);
                },
                err_out)) {
            return false;
        }
    }

    if (options.load_starter_deck) {
        if (!load_optional_json(read_file, kStarterDeckPath,
                [](const std::string& utf8, std::string& err) {
                    return load_starter_deck_list_from_json_utf8(utf8, err);
                },
                err_out)) {
            return false;
        }
    }

    if (options.load_test_deck) {
        if (!load_optional_json(read_file, kTestDeckPath,
                [](const std::string& utf8, std::string& err) {
                    return load_test_deck_list_from_json_utf8(utf8, err);
                },
                err_out)) {
            return false;
        }
    }

    return true;
}

}  // namespace tactics