#include "tactics/sync/match_sync.hpp"

#include "tactics/core/game_state.hpp"
#include "tactics/sync/match_auth.hpp"
#include "tactics/sync/wire_version.hpp"

#include <nlohmann/json.hpp>

namespace tactics {

uint64_t append_match_command_journal(std::vector<MatchCommandEntry>& journal, uint64_t& next_seq, const int seat, std::string line_utf8)
{
    const uint64_t seq = next_seq > 0 ? next_seq : 1;
    journal.push_back(MatchCommandEntry{seq, seat, std::move(line_utf8)});
    next_seq = seq + 1;
    trim_command_journal(journal);
    return seq;
}

void trim_command_journal(std::vector<MatchCommandEntry>& journal)
{
    if (journal.size() <= kMaxCommandJournalEntries) {
        return;
    }
    const std::size_t drop = journal.size() - kMaxCommandJournalEntries;
    journal.erase(journal.begin(), journal.begin() + static_cast<std::ptrdiff_t>(drop));
}

std::string wrap_match_command_for_network_utf8(const uint64_t seq, const int seat, const std::string& line_utf8)
{
    nlohmann::json j = nlohmann::json::object();
    j["t"] = "cmd";
    j["v"] = kNetworkWireVersion;
    j["seq"] = seq;
    j["seat"] = seat;
    j["line"] = line_utf8;
    return j.dump();
}

bool parse_match_command_wire_utf8(const std::string& wire_utf8, uint64_t& out_seq, int& out_seat, std::string& out_line_utf8,
                                   std::string& err)
{
    try {
        const nlohmann::json root = nlohmann::json::parse(wire_utf8);
        if (!root.contains("t") || root.at("t").get<std::string>() != "cmd") {
            err = "not a cmd frame";
            return false;
        }
        if (!wire_version_ok(root, err)) {
            return false;
        }
        out_seq = root.at("seq").get<uint64_t>();
        out_seat = root.at("seat").get<int>();
        out_line_utf8 = root.at("line").get<std::string>();
        return true;
    } catch (const std::exception& ex) {
        err = ex.what();
        return false;
    }
}

bool peek_wire_message_type_utf8(const std::string& wire_utf8, std::string& out_type, std::string& err)
{
    try {
        const nlohmann::json root = nlohmann::json::parse(wire_utf8);
        if (!root.contains("t") || !root["t"].is_string()) {
            err = "missing message type";
            return false;
        }
        out_type = root.at("t").get<std::string>();
        return true;
    } catch (const std::exception& ex) {
        err = ex.what();
        return false;
    }
}

std::string wrap_match_snapshot_delta_for_network_utf8(
    const uint64_t base_seq, const uint64_t snap_seq, const std::string& delta_json_utf8)
{
    nlohmann::json j = nlohmann::json::object();
    j["t"] = "snap_delta";
    j["v"] = kNetworkWireVersion;
    j["base_seq"] = base_seq;
    j["snap_seq"] = snap_seq;
    try {
        j["delta"] = nlohmann::json::parse(delta_json_utf8);
    } catch (const std::exception&) {
        return {};
    }
    return j.dump();
}

bool parse_match_snapshot_delta_wire_utf8(const std::string& wire_utf8, uint64_t& out_base_seq, uint64_t& out_snap_seq,
                                          std::string& out_delta_json_utf8, std::string& err)
{
    try {
        const nlohmann::json root = nlohmann::json::parse(wire_utf8);
        if (!root.contains("t") || root.at("t").get<std::string>() != "snap_delta") {
            err = "not a snap_delta frame";
            return false;
        }
        if (!wire_version_ok(root, err)) {
            return false;
        }
        out_base_seq = root.at("base_seq").get<uint64_t>();
        out_snap_seq = root.at("snap_seq").get<uint64_t>();
        out_delta_json_utf8 = root.at("delta").dump();
        return true;
    } catch (const std::exception& ex) {
        err = ex.what();
        return false;
    }
}

}  // namespace tactics
