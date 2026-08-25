#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tactics {

/** One authoritative CLI commit on the host (monotonic `seq`). */
struct MatchCommandEntry {
    uint64_t seq{0};
    int seat{0};
    std::string line_utf8;
};

inline constexpr int kNetworkCheckpointCommandInterval = 64;
/** Max retained authority CLI rows (also serialized in full snapshots).
 *  Upper bound ~6–15 MiB journal JSON at typical line sizes; keep headroom under 32 MiB WS frames. */
inline constexpr std::size_t kMaxCommandJournalEntries = 65536;

/** Append to in-memory journal; returns assigned sequence (starts at 1). */
uint64_t append_match_command_journal(std::vector<MatchCommandEntry>& journal, uint64_t& next_seq, int seat, std::string line_utf8);

/** Drop oldest entries when over cap (keeps monotonic seq). */
void trim_command_journal(std::vector<MatchCommandEntry>& journal);

/** `{ "t":"cmd", "v":4, "seq", "seat", "line" }` - host→client rebroadcast; unsigned (clients trust the host connection). */
std::string wrap_match_command_for_network_utf8(uint64_t seq, int seat, const std::string& line_utf8);

inline constexpr std::size_t kMaxNetworkSnapshotUtf8Bytes = 24ull * 1024 * 1024;

bool parse_match_command_wire_utf8(const std::string& wire_utf8, uint64_t& out_seq, int& out_seat, std::string& out_line_utf8,
                                   std::string& err);

bool peek_wire_message_type_utf8(const std::string& wire_utf8, std::string& out_type, std::string& err);

/** `{ "t":"snap_delta", "v":3, "base_seq", "snap_seq", "delta" }` */
std::string wrap_match_snapshot_delta_for_network_utf8(uint64_t base_seq, uint64_t snap_seq, const std::string& delta_json_utf8);
bool parse_match_snapshot_delta_wire_utf8(const std::string& wire_utf8, uint64_t& out_base_seq, uint64_t& out_snap_seq,
                                          std::string& out_delta_json_utf8, std::string& err);

}  // namespace tactics
