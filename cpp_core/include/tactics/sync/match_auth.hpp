#pragma once

#include <cstdint>
#include <string>

namespace tactics {

/** Random per-connection nonce (32 hex chars) the host issues in its welcome frame.
 *  Binding the nonce into the cli digest kills cross-session replay of captured frames. */
std::string generate_auth_nonce_hex();

/** HMAC-SHA256 digest for optional room auth on `cli` frames (not full TLS).
 *  Payload binds the host-issued nonce, seat, and a client-monotonic counter (`ctr`)
 *  so a captured (line, sig) pair cannot be replayed on this or a later connection.
 *  The host must reject frames whose `ctr` is not strictly greater than the last
 *  accepted value for that peer. */
std::string compute_cli_auth_digest_utf8(const std::string& room_secret, const std::string& auth_nonce, int seat, uint64_t ctr,
    const std::string& line_utf8);

/** Constant-time verification of a `cli` frame digest. Empty secret → digest must be empty. */
bool verify_cli_auth_digest_utf8(const std::string& room_secret, const std::string& auth_nonce, int seat, uint64_t ctr,
    const std::string& line_utf8, const std::string& digest);

}  // namespace tactics
