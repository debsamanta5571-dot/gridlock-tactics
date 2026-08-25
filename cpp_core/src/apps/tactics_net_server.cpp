/**
 * Standalone tactics WebSocket host (same wire format as `UTacticsWebSocketSubsystem` in Unreal).
 * Unreal clients: connect as WebSocket client, e.g. ws://127.0.0.1:8788/ (auto seat) or ws://127.0.0.1:8788/?seat=2
 * Headless: authority is always seat P1 on the server; WebSocket clients are assigned P2+ (omit ?seat= for auto-pick).
 *
 * Build: from cpp_core/build, `cmake ..` then build target `tactics_net_server`.
 * Run: tactics_net_server [--port N] [--content <path-to-TacticsGameUnreal/Content>] [--public]
 *   Default bind: 127.0.0.1 (use --public for 0.0.0.0). Optional: --token <secret> on cli frames.
 *   TLS flags (requires TACTICS_NET_USE_OPENSSL build): --tls-cert <pem> --tls-key <pem>.
 */

#include "tactics/apps/master_cli_dispatch.hpp"
#include "tactics/cards/ability_catalog.hpp"
#include "tactics/cards/card_catalog.hpp"
#include "tactics/cards/passive_catalog.hpp"
#include "tactics/content/project_content.hpp"
#include "tactics/core.hpp"
#include "tactics/core/game_state.hpp"
#include "tactics/effects/status_effect_catalog.hpp"
#include "tactics/sync/match_sync.hpp"
#include "tactics/sync/match_auth.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(TACTICS_NET_USE_OPENSSL)
#include <openssl/err.h>
#include <openssl/ssl.h>
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "Ws2_32.lib")
#pragma comment(lib, "Advapi32.lib")
#include <windows.h>
#include <wincrypt.h>
#else
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>
#include <openssl/sha.h>
#endif

namespace {

constexpr int kWireProtocolVersion = tactics::kNetworkWireVersion;
constexpr uint64_t kMaxWsTextPayloadBytes = 32ull * 1024 * 1024;
/** Hard cap on buffered unsent bytes per peer - a client that stalls past this is dropped
 *  (it can reconnect and resync) instead of growing the host's memory unboundedly. */
constexpr size_t kMaxPeerTxBufferBytes = 128ull * 1024 * 1024;

std::atomic<bool> g_run{true};

#ifndef _WIN32
void on_signal(int) { g_run = false; }
#endif

#ifdef _WIN32
using sock_t = SOCKET;
constexpr sock_t k_invalid_socket = INVALID_SOCKET;
inline int sock_errno() { return WSAGetLastError(); }
inline bool sock_err_would_block(const int e) { return e == WSAEWOULDBLOCK; }
inline void sock_close(sock_t s) {
	if (s != k_invalid_socket) {
		closesocket(s);
	}
}
inline bool set_nonblocking(sock_t s) {
	u_long mode = 1;
	return ioctlsocket(s, FIONBIO, &mode) == 0;
}
inline bool set_blocking(sock_t s) {
	u_long mode = 0;
	return ioctlsocket(s, FIONBIO, &mode) == 0;
}
bool sha1_digest(const uint8_t* data, size_t len, uint8_t out20[20])
{
	HCRYPTPROV prov = 0;
	HCRYPTHASH hash = 0;
	if (!CryptAcquireContext(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
		return false;
	}
	if (!CryptCreateHash(prov, CALG_SHA1, 0, 0, &hash)) {
		CryptReleaseContext(prov, 0);
		return false;
	}
	if (!CryptHashData(hash, data, static_cast<DWORD>(len), 0)) {
		CryptDestroyHash(hash);
		CryptReleaseContext(prov, 0);
		return false;
	}
	DWORD hash_len = 20;
	if (!CryptGetHashParam(hash, HP_HASHVAL, out20, &hash_len, 0)) {
		CryptDestroyHash(hash);
		CryptReleaseContext(prov, 0);
		return false;
	}
	CryptDestroyHash(hash);
	CryptReleaseContext(prov, 0);
	return true;
}
#else
using sock_t = int;
constexpr sock_t k_invalid_socket = -1;
inline int sock_errno() { return errno; }
inline bool sock_err_would_block(const int e) { return e == EWOULDBLOCK || e == EAGAIN; }
inline void sock_close(sock_t s) {
	if (s >= 0) {
		close(s);
	}
}
inline bool set_nonblocking(sock_t s)
{
	int flags = fcntl(s, F_GETFL, 0);
	if (flags < 0) {
		return false;
	}
	return fcntl(s, F_SETFL, flags | O_NONBLOCK) == 0;
}
inline bool set_blocking(sock_t s)
{
	int flags = fcntl(s, F_GETFL, 0);
	if (flags < 0) {
		return false;
	}
	return fcntl(s, F_SETFL, flags & ~O_NONBLOCK) == 0;
}
bool sha1_digest(const uint8_t* data, size_t len, uint8_t out20[20])
{
	SHA_CTX ctx;
	if (SHA1_Init(&ctx) != 1) {
		return false;
	}
	if (SHA1_Update(&ctx, data, len) != 1) {
		return false;
	}
	return SHA1_Final(out20, &ctx) == 1;
}
#endif

struct Peer {
	sock_t sock{k_invalid_socket};
#if defined(TACTICS_NET_USE_OPENSSL)
	SSL* ssl{nullptr};
#endif
	enum class Phase { HttpHandshake, WebSocketReady } phase{Phase::HttpHandshake};
	std::vector<uint8_t> handshake_accum;
	std::vector<uint8_t> rx_scratch;
	std::vector<uint8_t> utf8_fragment_assembly;
	/** Unsent outgoing bytes (frames already encoded). Flushed opportunistically and on POLLOUT
	 *  so a slow client causes backpressure here instead of a partially written frame. */
	std::vector<uint8_t> tx_buffer;
	int seat_id{0};
	/** Random per-connection nonce issued in the welcome frame; bound into cli auth digests. */
	std::string auth_nonce;
	/** Highest accepted cli `ctr` - later frames must be strictly greater (replay protection). */
	uint64_t last_cli_ctr{0};
};

void close_peer(Peer& peer)
{
#if defined(TACTICS_NET_USE_OPENSSL)
	if (peer.ssl) {
		SSL_shutdown(peer.ssl);
		SSL_free(peer.ssl);
		peer.ssl = nullptr;
	}
#endif
	sock_close(peer.sock);
	peer.sock = k_invalid_socket;
	peer.phase = Peer::Phase::HttpHandshake;
	peer.tx_buffer.clear();
}

/** Send as much buffered output as the socket accepts. Would-block keeps the remainder for the
 *  next POLLOUT; any other error is fatal for this peer (returns false → caller disconnects). */
bool flush_peer_tx(Peer& peer)
{
	while (!peer.tx_buffer.empty()) {
		const uint8_t* data = peer.tx_buffer.data();
		const size_t len = peer.tx_buffer.size();
		int n = 0;
#if defined(TACTICS_NET_USE_OPENSSL)
		if (peer.ssl) {
			n = SSL_write(peer.ssl, data, static_cast<int>(std::min<size_t>(len, 1u << 20)));
			if (n <= 0) {
				const int ssl_err = SSL_get_error(peer.ssl, n);
				if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
					return true;
				}
				return false;
			}
			peer.tx_buffer.erase(peer.tx_buffer.begin(), peer.tx_buffer.begin() + n);
			continue;
		}
#endif
#ifdef _WIN32
		n = send(peer.sock, reinterpret_cast<const char*>(data), static_cast<int>(std::min<size_t>(len, 1u << 20)), 0);
#else
		n = static_cast<int>(::send(peer.sock, data, len, 0));
#endif
		if (n <= 0) {
			if (n < 0 && sock_err_would_block(sock_errno())) {
				return true;
			}
			return false;
		}
		peer.tx_buffer.erase(peer.tx_buffer.begin(), peer.tx_buffer.begin() + n);
	}
	return true;
}

/** Queue raw bytes and try to flush. False = peer is broken or hopelessly backlogged → disconnect. */
bool queue_peer_bytes(Peer& peer, const uint8_t* data, const size_t len)
{
	if (peer.sock == k_invalid_socket) {
		return false;
	}
	if (peer.tx_buffer.size() + len > kMaxPeerTxBufferBytes) {
		return false;
	}
	peer.tx_buffer.insert(peer.tx_buffer.end(), data, data + len);
	return flush_peer_tx(peer);
}

/** Read once. >0 = bytes read; 0 = nothing available right now; -1 = closed/error. */
int recv_some(Peer& peer, uint8_t* out, const size_t cap)
{
#if defined(TACTICS_NET_USE_OPENSSL)
	if (peer.ssl) {
		const int n = SSL_read(peer.ssl, out, static_cast<int>(cap));
		if (n > 0) {
			return n;
		}
		const int ssl_err = SSL_get_error(peer.ssl, n);
		// A split TLS record legitimately yields WANT_READ - it is not a disconnect.
		if (ssl_err == SSL_ERROR_WANT_READ || ssl_err == SSL_ERROR_WANT_WRITE) {
			return 0;
		}
		return -1;
	}
#endif
#ifdef _WIN32
	const int n = recv(peer.sock, reinterpret_cast<char*>(out), static_cast<int>(cap), 0);
#else
	const int n = static_cast<int>(::recv(peer.sock, out, cap, 0));
#endif
	if (n > 0) {
		return n;
	}
	if (n < 0 && sock_err_would_block(sock_errno())) {
		return 0;
	}
	return -1;
}

std::string base64_encode(const uint8_t* data, size_t len)
{
	static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve((len + 2) / 3 * 4);
	for (size_t i = 0; i < len; i += 3) {
		const uint32_t b0 = data[i];
		const uint32_t b1 = i + 1 < len ? data[i + 1] : 0u;
		const uint32_t b2 = i + 2 < len ? data[i + 2] : 0u;
		const uint32_t triple = (b0 << 16) | (b1 << 8) | b2;
		const int rem = static_cast<int>(len - i);
		out.push_back(tbl[(triple >> 18) & 63]);
		out.push_back(tbl[(triple >> 12) & 63]);
		out.push_back(rem > 1 ? tbl[(triple >> 6) & 63] : '=');
		out.push_back(rem > 2 ? tbl[triple & 63] : '=');
	}
	return out;
}

std::string make_sec_websocket_accept(const std::string& client_key_raw)
{
	std::string client_key = client_key_raw;
	while (!client_key.empty() && std::isspace(static_cast<unsigned char>(client_key.front()))) {
		client_key.erase(client_key.begin());
	}
	while (!client_key.empty() && std::isspace(static_cast<unsigned char>(client_key.back()))) {
		client_key.pop_back();
	}
	const std::string concat = client_key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
	uint8_t digest[20]{};
	if (!sha1_digest(reinterpret_cast<const uint8_t*>(concat.data()), concat.size(), digest)) {
		return {};
	}
	return base64_encode(digest, 20);
}

bool parse_http_header_value(const std::string& block, const char* key, std::string& out)
{
	const std::string prefix = std::string(key) + ": ";
	for (size_t line_start = 0; line_start < block.size();) {
		const size_t line_end = block.find('\n', line_start);
		const std::string line = line_end == std::string::npos ? block.substr(line_start) : block.substr(line_start, line_end - line_start);
		size_t p = 0;
		bool match = true;
		if (line.size() < prefix.size()) {
			match = false;
		} else {
			for (; p < prefix.size(); ++p) {
				if (std::tolower(static_cast<unsigned char>(line[p])) != std::tolower(static_cast<unsigned char>(prefix[p]))) {
					match = false;
					break;
				}
			}
		}
		if (match && line.size() >= prefix.size()) {
			out = line.substr(prefix.size());
			while (!out.empty() && (out.back() == '\r' || std::isspace(static_cast<unsigned char>(out.back())))) {
				out.pop_back();
			}
			while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front()))) {
				out.erase(out.begin());
			}
			return true;
		}
		if (line_end == std::string::npos) {
			break;
		}
		line_start = line_end + 1;
	}
	return false;
}

/** Parses `?seat=` / `&seat=` from the HTTP request line / headers. Returns `default_when_absent` if missing.
 *  `seat=0` means auto (lowest free seat). Otherwise clamped to 1..32. */
int parse_seat_from_http_block(const std::string& block, int default_when_absent)
{
	size_t pos = std::string::npos;
	for (size_t i = 0; i + 6 <= block.size(); ++i) {
		const unsigned char c = static_cast<unsigned char>(block[i]);
		if (c != '?' && c != '&') {
			continue;
		}
		if (block.compare(i + 1, 5, "seat=") == 0) {
			pos = i + 6;
			break;
		}
	}
	if (pos == std::string::npos) {
		return default_when_absent;
	}
	while (pos < block.size() && std::isspace(static_cast<unsigned char>(block[pos]))) {
		++pos;
	}
	std::string digits;
	for (; pos < block.size() && std::isdigit(static_cast<unsigned char>(block[pos])); ++pos) {
		digits.push_back(block[pos]);
	}
	if (digits.empty()) {
		return default_when_absent;
	}
	const int v = std::stoi(digits);
	if (v == 0) {
		return 0;
	}
	return std::clamp(v, 1, 32);
}

/** Encode a server→client WS frame (unmasked) and queue it. False → disconnect the peer. */
bool send_ws_frame(Peer& peer, const uint8_t opcode, const uint8_t* payload, const size_t payload_len)
{
	if (payload_len > kMaxWsTextPayloadBytes) {
		return true;
	}
	std::vector<uint8_t> frame;
	frame.reserve(14 + payload_len);
	frame.push_back(static_cast<uint8_t>(0x80 | opcode));
	if (payload_len < 126) {
		frame.push_back(static_cast<uint8_t>(payload_len));
	} else if (payload_len < 65536) {
		frame.push_back(126);
		frame.push_back(static_cast<uint8_t>((payload_len >> 8) & 0xff));
		frame.push_back(static_cast<uint8_t>(payload_len & 0xff));
	} else {
		frame.push_back(127);
		const uint64_t len = payload_len;
		for (int b = 7; b >= 0; --b) {
			frame.push_back(static_cast<uint8_t>((len >> (b * 8)) & 0xff));
		}
	}
	if (payload_len > 0) {
		frame.insert(frame.end(), payload, payload + payload_len);
	}
	return queue_peer_bytes(peer, frame.data(), frame.size());
}

bool send_ws_pong(Peer& peer, const std::vector<uint8_t>& payload)
{
	return send_ws_frame(peer, 0x0A, payload.data(), payload.size());
}

bool send_ws_text(Peer& peer, const std::string& utf8)
{
	return send_ws_frame(peer, 0x01, reinterpret_cast<const uint8_t*>(utf8.data()), utf8.size());
}

std::string json_cli_ack(const std::string& msg)
{
	nlohmann::json j;
	j["t"] = "cli_ack";
	j["v"] = kWireProtocolVersion;
	j["msg"] = msg.size() > 4096 ? msg.substr(0, 4096) + "…" : msg;
	return j.dump();
}

std::string json_welcome(int seat, int player_count, bool content_catalog_loaded, const std::string& ability_catalog_fingerprint,
	const std::string& card_catalog_fingerprint, const std::string& auth_nonce)
{
	nlohmann::json j;
	j["t"] = "welcome";
	j["v"] = kWireProtocolVersion;
	j["seat"] = seat;
	j["player_count"] = player_count;
	j["authority"] = "headless_p1_server";
	j["content_catalog_loaded"] = content_catalog_loaded;
	j["ability_catalog_fingerprint"] = ability_catalog_fingerprint;
	j["card_catalog_fingerprint"] = card_catalog_fingerprint;
	if (!auth_nonce.empty()) {
		j["auth_nonce"] = auth_nonce;
	}
	return j.dump();
}

int get_match_player_count(const tactics::GameState& g)
{
	return static_cast<int>(g.turn_manager.players.size());
}

void skip_energy_until_main_or_cap(tactics::GameState& game)
{
	for (int i = 0; i < 32 && game.turn_manager.current_phase == tactics::TurnPhase::Energy; ++i) {
		const std::optional<int> cp = game.turn_manager.current_player();
		if (!cp) {
			break;
		}
		const tactics::ActionResult r = game.skip_energy_zone(*cp);
		if (!r.ok) {
			break;
		}
	}
}

void load_text_file_if_present(const std::string& path, const std::function<void(const std::string&)>& on_text)
{
	if (path.empty()) {
		return;
	}
	std::ifstream in(path, std::ios::binary);
	if (!in) {
		return;
	}
	std::ostringstream oss;
	oss << in.rdbuf();
	on_text(oss.str());
}

bool content_catalog_files_present(const std::string& content_dir)
{
	if (content_dir.empty()) {
		return false;
	}
	std::string content_prefix = content_dir;
	if (content_prefix.back() != '/' && content_prefix.back() != '\\') {
		content_prefix += "/";
	}
	const std::string ability_path = content_prefix + "TacticsData/ability_catalog.json";
	std::ifstream in(ability_path, std::ios::binary);
	return static_cast<bool>(in);
}

void reset_match_to_player_count(std::unique_ptr<tactics::GameState>& game_ptr, int player_count, const std::string& content_dir)
{
	tactics::ensure_builtin_ability_catalog_loaded();
	tactics::ensure_builtin_passive_catalog_loaded();
	std::string content_prefix = content_dir;
	if (!content_prefix.empty() && content_prefix.back() != '/' && content_prefix.back() != '\\') {
		content_prefix += "/";
	}
	if (!content_dir.empty() && !content_catalog_files_present(content_dir)) {
		std::cerr << "[tactics_net_server] WARN: --content must be TacticsGameUnreal/Content (missing "
		          << content_prefix << "TacticsData/ability_catalog.json); using built-in catalogs.\n";
	}
	{
		const auto read_file = [&](const std::string& rel, std::string& out, std::string& err) -> bool {
			(void)err;
			std::ifstream in(content_prefix + rel, std::ios::binary);
			if (!in) {
				return false;
			}
			std::ostringstream oss;
			oss << in.rdbuf();
			out = oss.str();
			return true;
		};
		std::string err;
		if (!tactics::load_all_project_content(read_file, err)) {
			std::cerr << "[tactics_net_server] project content: " << err << "\n";
		}
	}

	const int n = std::clamp(player_count, 0, 32);
	game_ptr = std::make_unique<tactics::GameState>("unreal_gui");
	for (int i = 1; i <= n; ++i) {
		game_ptr->add_player(i, "P" + std::to_string(i));
	}
	if (n > 0) {
		tactics::master_cli_seed_demo_state(*game_ptr);
		game_ptr->start_game();
	}
}

int register_network_client_seat(tactics::GameState& game, int requested_seat, const std::vector<int>& seats_taken_by_other_remotes)
{
	auto seat_taken = [&](int s) {
		return std::find(seats_taken_by_other_remotes.begin(), seats_taken_by_other_remotes.end(), s) != seats_taken_by_other_remotes.end();
	};
	// Align with Unreal: never grow the match. Cap at configured player count; return 0 when full.
	// Seat 1 is server authority - remotes are P2+.
	const int cur = get_match_player_count(game);
	if (cur < 2) {
		return 0;
	}
	auto seat_unavailable = [&](int s) { return s < 2 || s > cur || seat_taken(s); };
	int chosen = 2;
	if (requested_seat > 1) {
		chosen = std::clamp(requested_seat, 2, cur);
		if (seat_unavailable(chosen)) {
			chosen = 2;
			while (chosen <= cur && seat_unavailable(chosen)) {
				++chosen;
			}
			if (chosen > cur) {
				return 0;
			}
		}
	} else {
		while (chosen <= cur && seat_unavailable(chosen)) {
			++chosen;
		}
		if (chosen > cur) {
			return 0;
		}
	}
	return chosen;
}

struct MatchHost {
	std::unique_ptr<tactics::GameState> game;
	std::unordered_map<int, std::shared_ptr<tactics::Unit>> remote_selection;
	bool content_catalog_loaded{false};
	std::string room_token;
	std::optional<uint64_t> last_broadcast_snap_seq;
	/** Kept as a parsed DOM so per-broadcast deltas don't re-parse the previous snapshot. */
	nlohmann::json last_broadcast_snapshot_dom;

	std::string export_wire() const
	{
		if (!game) {
			return {};
		}
		const uint64_t snap_seq = game->network_snap_seq();
		const std::string inner = game->build_match_snapshot_utf8();
		return tactics::wrap_match_snapshot_for_network_utf8(inner, snap_seq);
	}

	bool exec_remote_tcp_seat_cli_line(int seat, const std::string& line_utf8, std::string& out_message)
	{
		if (!game) {
			out_message = "No match.";
			return false;
		}
		int controlled = seat;
		std::shared_ptr<tactics::Unit>& sel = remote_selection[seat];
		std::ostringstream oss;
		const bool quit = tactics::dispatch_master_cli_line(*game, controlled, sel, line_utf8, oss, {});
		out_message = oss.str();
		return quit;
	}

	void clear_remote_seat_selection(int seat) { remote_selection.erase(seat); }

	void clear_all_remote_selection() { remote_selection.clear(); }
};

void broadcast_snap(const std::vector<std::unique_ptr<Peer>>& peers, MatchHost& host, const bool bump_seq)
{
	if (bump_seq && host.game) {
		host.game->bump_network_snap_seq();
	}
	std::string wire = host.export_wire();
	if (wire.empty()) {
		std::cerr << "[tactics_net_server] snapshot export empty\n";
		return;
	}
	std::string inner;
	std::optional<uint64_t> snap_seq;
	std::string err;
	if (tactics::unwrap_snap_wire_utf8_for_replace(wire, inner, snap_seq, err) && snap_seq.has_value()) {
		try {
			nlohmann::json next = nlohmann::json::parse(inner);
			// Send a delta whenever every ready peer shares the previous broadcast as a base  - 
			// checkpoint bumps included (a full snapshot every 64 commands wasted bandwidth).
			if (host.last_broadcast_snap_seq.has_value() && !host.last_broadcast_snapshot_dom.is_null()
			    && *host.last_broadcast_snap_seq < *snap_seq) {
				const std::string delta = nlohmann::json::diff(host.last_broadcast_snapshot_dom, next).dump();
				const std::string delta_wire =
					tactics::wrap_match_snapshot_delta_for_network_utf8(*host.last_broadcast_snap_seq, *snap_seq, delta);
				if (!delta_wire.empty() && delta_wire.size() < wire.size()) {
					wire = delta_wire;
				}
			}
			host.last_broadcast_snap_seq = *snap_seq;
			host.last_broadcast_snapshot_dom = std::move(next);
		} catch (const std::exception&) {
			// Keep full snapshot wire; drop the stale delta base.
			host.last_broadcast_snap_seq.reset();
			host.last_broadcast_snapshot_dom = nlohmann::json();
		}
	}
	for (const auto& p : peers) {
		if (p && p->sock != k_invalid_socket && p->phase == Peer::Phase::WebSocketReady) {
			if (!send_ws_text(*p, wire)) {
				close_peer(*p);
			}
		}
	}
}

void broadcast_cmd(const std::vector<std::unique_ptr<Peer>>& peers, const int seat, const std::string& line_utf8, const uint64_t cmd_seq)
{
	if (line_utf8.empty() || cmd_seq == 0) {
		return;
	}
	const std::string wire = tactics::wrap_match_command_for_network_utf8(cmd_seq, seat, line_utf8);
	for (const auto& p : peers) {
		if (p && p->sock != k_invalid_socket && p->phase == Peer::Phase::WebSocketReady) {
			if (!send_ws_text(*p, wire)) {
				close_peer(*p);
			}
		}
	}
}

void send_snap_to_peer(const MatchHost& host, Peer& peer)
{
	if (peer.sock == k_invalid_socket || peer.phase != Peer::Phase::WebSocketReady) {
		return;
	}
	const std::string wire = host.export_wire();
	if (!wire.empty() && !send_ws_text(peer, wire)) {
		close_peer(peer);
	}
}

bool complete_ws_handshake(MatchHost& host, Peer& peer, const std::string& http_block, std::vector<std::unique_ptr<Peer>>& all_peers,
	size_t peer_index)
{
	std::string key;
	if (!parse_http_header_value(http_block, "Sec-WebSocket-Key", key)) {
		return false;
	}
	const std::string accept = make_sec_websocket_accept(key);
	if (accept.empty()) {
		return false;
	}

	const int requested_seat = parse_seat_from_http_block(http_block, 0);
	std::vector<int> taken;
	for (size_t i = 0; i < all_peers.size(); ++i) {
		if (i == peer_index || !all_peers[i]) {
			continue;
		}
		if (all_peers[i]->seat_id >= 1) {
			taken.push_back(all_peers[i]->seat_id);
		}
	}
	peer.seat_id = register_network_client_seat(*host.game, requested_seat, taken);
	if (peer.seat_id < 1) {
		const std::string busy = "HTTP/1.1 503 Service Unavailable\r\nConnection: close\r\nContent-Length: 0\r\n\r\n";
		queue_peer_bytes(peer, reinterpret_cast<const uint8_t*>(busy.data()), busy.size());
		std::cerr << "[tactics_net_server] refusing join - match is full\n";
		return false;
	}

	const std::string response = std::string("HTTP/1.1 101 Switching Protocols\r\n") + "Upgrade: websocket\r\n" + "Connection: Upgrade\r\n" + "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
	if (!queue_peer_bytes(peer, reinterpret_cast<const uint8_t*>(response.data()), response.size())) {
		return false;
	}
	peer.phase = Peer::Phase::WebSocketReady;
	peer.auth_nonce = tactics::generate_auth_nonce_hex();
	peer.last_cli_ctr = 0;

	for (int i = static_cast<int>(all_peers.size()) - 1; i >= 0; --i) {
		if (static_cast<size_t>(i) == peer_index || !all_peers[static_cast<size_t>(i)]) {
			continue;
		}
		Peer& other = *all_peers[static_cast<size_t>(i)];
		if (other.phase == Peer::Phase::WebSocketReady && other.seat_id == peer.seat_id && other.sock != k_invalid_socket) {
			std::cerr << "[tactics_net_server] replacing prior connection for seat P" << peer.seat_id << "\n";
			close_peer(other);
			host.clear_remote_seat_selection(other.seat_id);
		}
	}

	host.clear_remote_seat_selection(peer.seat_id);
	const int player_count = host.game ? get_match_player_count(*host.game) : 0;
	if (!send_ws_text(peer,
			json_welcome(peer.seat_id, player_count, host.content_catalog_loaded, tactics::ability_catalog_fingerprint_utf8(),
				tactics::card_catalog_fingerprint_utf8(), host.room_token.empty() ? std::string{} : peer.auth_nonce))) {
		return false;
	}
	host.last_broadcast_snap_seq.reset();
	host.last_broadcast_snapshot_dom = nlohmann::json();
	broadcast_snap(all_peers, host, false);
	std::cerr << "[tactics_net_server] peer ready seat P" << peer.seat_id << "\n";
	return true;
}

void dispatch_inbound_json(MatchHost& host, std::vector<std::unique_ptr<Peer>>& peers, const std::string& json_utf8, bool from_tcp_client,
	int tcp_seat_hint, Peer* reply_peer, const std::string& room_token)
{
	try {
		const nlohmann::json root = nlohmann::json::parse(json_utf8);
		if (!root.contains("t") || !root["t"].is_string()) {
			return;
		}
		const std::string type = root["t"].get<std::string>();
		if (from_tcp_client && type == "resync") {
			if (reply_peer && reply_peer->sock != k_invalid_socket) {
				send_snap_to_peer(host, *reply_peer);
			}
			return;
		}
		if (from_tcp_client && type == "cli") {
			if (!root.contains("v")) {
				return;
			}
			const int wire_v = root["v"].is_number_integer() ? root["v"].get<int>() : static_cast<int>(root["v"].get<double>());
			if (wire_v != tactics::kNetworkWireVersion) {
				return;
			}
			int seat = tcp_seat_hint >= 2 ? tcp_seat_hint : 2;
			const int n = host.game ? get_match_player_count(*host.game) : 0;
			if (n < 1) {
				return;
			}
			seat = std::clamp(seat, 2, n);
			if (!root.contains("line") || !root["line"].is_string()) {
				return;
			}
			const std::string line = root["line"].get<std::string>();
			if (!room_token.empty()) {
				const auto reject = [&](const char* why) {
					if (reply_peer && reply_peer->sock != k_invalid_socket) {
						send_ws_text(*reply_peer, json_cli_ack(std::string("Rejected: ") + why));
					}
				};
				if (!reply_peer) {
					return;
				}
				// ctr must be strictly increasing per connection: a replayed frame reuses an
				// already-accepted ctr and fails here even with a valid digest.
				const uint64_t ctr = root.contains("ctr") && root["ctr"].is_number_unsigned() ? root["ctr"].get<uint64_t>() : 0;
				if (ctr <= reply_peer->last_cli_ctr) {
					reject("stale cli counter");
					return;
				}
				if (!root.contains("sig") || !root["sig"].is_string()
				    || !tactics::verify_cli_auth_digest_utf8(room_token, reply_peer->auth_nonce, seat, ctr, line,
					    root["sig"].get<std::string>())) {
					reject("invalid cli signature");
					return;
				}
				reply_peer->last_cli_ctr = ctr;
			}
			std::string out;
			const bool quit = host.exec_remote_tcp_seat_cli_line(seat, line, out);
			(void)quit;
			if (host.game) {
				const uint64_t cmd_seq = host.game->record_authority_command(seat, line);
				broadcast_cmd(peers, seat, line, cmd_seq);
				if (cmd_seq % tactics::kNetworkCheckpointCommandInterval == 0) {
					broadcast_snap(peers, host, true);
				}
			}
			if (reply_peer && reply_peer->sock != k_invalid_socket) {
				if (!send_ws_text(*reply_peer, json_cli_ack(out))) {
					close_peer(*reply_peer);
				}
			}
		}
	} catch (const std::exception& e) {
		std::cerr << "[tactics_net_server] JSON error: " << e.what() << "\n";
	}
}

/** Parse complete frames out of rx_scratch. Returns false when the peer must be disconnected. */
bool drain_ws_frames(MatchHost& host, std::vector<std::unique_ptr<Peer>>& peers, Peer& peer)
{
	while (peer.sock != k_invalid_socket && peer.rx_scratch.size() >= 2) {
		uint8_t mask[4]{};
		const uint8_t b0 = peer.rx_scratch[0];
		const uint8_t b1 = peer.rx_scratch[1];
		const bool fin = (b0 & 0x80) != 0;
		const uint8_t opcode = b0 & 0x0f;
		const bool masked = (b1 & 0x80) != 0;
		const uint64_t payload_len7 = b1 & 0x7f;
		size_t idx = 2;
		uint64_t payload_len = payload_len7;
		if (payload_len7 == 126) {
			if (peer.rx_scratch.size() < 4) {
				return true;
			}
			payload_len = (static_cast<uint64_t>(peer.rx_scratch[2]) << 8) | peer.rx_scratch[3];
			idx = 4;
		} else if (payload_len7 == 127) {
			if (peer.rx_scratch.size() < 10) {
				return true;
			}
			uint64_t len64 = 0;
			for (int b = 0; b < 8; ++b) {
				len64 = (len64 << 8) | peer.rx_scratch[2 + b];
			}
			if (len64 > kMaxWsTextPayloadBytes) {
				return false;
			}
			payload_len = len64;
			idx = 10;
		}
		if (masked) {
			if (peer.rx_scratch.size() < idx + 4) {
				return true;
			}
			std::memcpy(mask, &peer.rx_scratch[idx], 4);
			idx += 4;
		}
		if (payload_len > kMaxWsTextPayloadBytes) {
			return false;
		}
		if (idx + payload_len > peer.rx_scratch.size()) {
			return true;
		}
		std::vector<uint8_t> payload(static_cast<size_t>(payload_len));
		for (uint64_t b = 0; b < payload_len; ++b) {
			uint8_t byte = peer.rx_scratch[idx + static_cast<size_t>(b)];
			if (masked) {
				byte ^= mask[static_cast<size_t>(b % 4)];
			}
			payload[static_cast<size_t>(b)] = byte;
		}
		peer.rx_scratch.erase(peer.rx_scratch.begin(),
			peer.rx_scratch.begin() + static_cast<std::ptrdiff_t>(idx + payload_len));

		if (opcode == 0x8) {
			return false;
		}
		if (opcode == 0x9) {
			if (!send_ws_pong(peer, payload)) {
				return false;
			}
			continue;
		}
		if (opcode == 0xA) {
			continue;
		}
		if (opcode == 0x1) {
			if (fin) {
				peer.utf8_fragment_assembly.clear();
				if (!payload.empty()) {
					std::string json(reinterpret_cast<const char*>(payload.data()), payload.size());
					dispatch_inbound_json(host, peers, json, true, peer.seat_id, &peer, host.room_token);
				}
			} else {
				peer.utf8_fragment_assembly = std::move(payload);
			}
			continue;
		}
		if (opcode == 0x0) {
			peer.utf8_fragment_assembly.insert(peer.utf8_fragment_assembly.end(), payload.begin(), payload.end());
			if (fin) {
				if (!peer.utf8_fragment_assembly.empty()) {
					std::string json(reinterpret_cast<const char*>(peer.utf8_fragment_assembly.data()), peer.utf8_fragment_assembly.size());
					dispatch_inbound_json(host, peers, json, true, peer.seat_id, &peer, host.room_token);
				}
				peer.utf8_fragment_assembly.clear();
			}
			continue;
		}
	}
	return peer.sock != k_invalid_socket;
}

void process_ws_rx(MatchHost& host, std::vector<std::unique_ptr<Peer>>& peers, size_t peer_index)
{
	Peer& peer = *peers[peer_index];
	bool disconnect = false;
	// Bounded reads per poll tick so one chatty peer cannot starve the others.
	for (int reads = 0; reads < 64 && peer.sock != k_invalid_socket; ++reads) {
		uint8_t buf[16384];
		const int read = recv_some(peer, buf, sizeof(buf));
		if (read < 0) {
			disconnect = true;
			break;
		}
		if (read == 0) {
			break;
		}
		peer.rx_scratch.insert(peer.rx_scratch.end(), buf, buf + read);
		if (!drain_ws_frames(host, peers, peer)) {
			disconnect = true;
			break;
		}
	}
	if (disconnect && peer.sock != k_invalid_socket) {
		host.clear_remote_seat_selection(peer.seat_id);
		close_peer(peer);
		std::cerr << "[tactics_net_server] peer disconnected\n";
	}
}

/** Shared per-peer poll handling for both platform poll loops. */
void handle_peer_io(MatchHost& host, std::vector<std::unique_ptr<Peer>>& peers, const size_t peer_i,
	const bool readable, const bool writable, const bool errored)
{
	Peer& peer = *peers[peer_i];
	if (errored) {
		host.clear_remote_seat_selection(peer.seat_id);
		close_peer(peer);
		return;
	}
	if (writable && !peer.tx_buffer.empty() && !flush_peer_tx(peer)) {
		host.clear_remote_seat_selection(peer.seat_id);
		close_peer(peer);
		return;
	}
	bool effective_readable = readable;
#if defined(TACTICS_NET_USE_OPENSSL)
	// Decrypted bytes buffered inside OpenSSL are invisible to poll - drain them too.
	if (peer.ssl && SSL_pending(peer.ssl) > 0) {
		effective_readable = true;
	}
#endif
	if (!effective_readable || peer.sock == k_invalid_socket) {
		return;
	}
	if (peer.phase == Peer::Phase::HttpHandshake) {
		uint8_t b[4096];
		const int r = recv_some(peer, b, sizeof(b));
		if (r < 0) {
			close_peer(peer);
			return;
		}
		if (r == 0) {
			return;
		}
		peer.handshake_accum.insert(peer.handshake_accum.end(), b, b + r);
		if (peer.handshake_accum.size() > 64 * 1024) {
			close_peer(peer);
			return;
		}
		const std::string http_text(reinterpret_cast<const char*>(peer.handshake_accum.data()), peer.handshake_accum.size());
		size_t term = http_text.find("\r\n\r\n");
		size_t term_len = 4;
		if (term == std::string::npos) {
			term = http_text.find("\n\n");
			term_len = 2;
		}
		if (term == std::string::npos) {
			return;
		}
		const std::string header_block = http_text.substr(0, term);
		// A client may pipeline its first WS frames in the same segment as the upgrade
		// request - those bytes belong to the WS stream, not the discarded HTTP block.
		std::vector<uint8_t> leftover(peer.handshake_accum.begin() + static_cast<std::ptrdiff_t>(term + term_len),
			peer.handshake_accum.end());
		peer.handshake_accum.clear();
		if (!complete_ws_handshake(host, peer, header_block, peers, peer_i)) {
			close_peer(peer);
			return;
		}
		if (!leftover.empty() && peer.sock != k_invalid_socket) {
			peer.rx_scratch.insert(peer.rx_scratch.end(), leftover.begin(), leftover.end());
			if (!drain_ws_frames(host, peers, peer)) {
				host.clear_remote_seat_selection(peer.seat_id);
				close_peer(peer);
			}
		}
	} else {
		process_ws_rx(host, peers, peer_i);
	}
}

}  // namespace

int main(int argc, char** argv)
{
	int port = 8788;
	std::string content_dir;
	std::string bind_addr = "127.0.0.1";
	bool bind_public = false;
	std::string room_token;
	std::string tls_cert_path;
	std::string tls_key_path;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			port = std::stoi(argv[++i]);
		} else if (std::strcmp(argv[i], "--content") == 0 && i + 1 < argc) {
			content_dir = argv[++i];
		} else if (std::strcmp(argv[i], "--bind") == 0 && i + 1 < argc) {
			bind_addr = argv[++i];
		} else if (std::strcmp(argv[i], "--public") == 0) {
			bind_public = true;
		} else if (std::strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
			room_token = argv[++i];
		} else if (std::strcmp(argv[i], "--tls-cert") == 0 && i + 1 < argc) {
			tls_cert_path = argv[++i];
		} else if (std::strcmp(argv[i], "--tls-key") == 0 && i + 1 < argc) {
			tls_key_path = argv[++i];
		}
	}
	const bool wants_tls = !tls_cert_path.empty() || !tls_key_path.empty();
	if ((tls_cert_path.empty() && !tls_key_path.empty()) || (!tls_cert_path.empty() && tls_key_path.empty())) {
		std::cerr << "[tactics_net_server] --tls-cert and --tls-key must be provided together\n";
		return 1;
	}
#if !defined(TACTICS_NET_USE_OPENSSL)
	if (wants_tls) {
		std::cerr << "[tactics_net_server] This build does not include in-process TLS. "
		          << "Rebuild with -DTACTICS_NET_USE_OPENSSL=ON or use a reverse proxy TLS terminator.\n";
		return 1;
	}
#endif
	if (bind_public) {
		bind_addr = "0.0.0.0";
	}

#ifdef _WIN32
	WSADATA wsa{};
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		std::cerr << "[tactics_net_server] WSAStartup failed\n";
		return 1;
	}
	SetConsoleCtrlHandler(
		[](DWORD) -> BOOL {
			g_run = false;
			return TRUE;
		},
		TRUE);
#else
	std::signal(SIGINT, on_signal);
	std::signal(SIGTERM, on_signal);
#endif

	MatchHost host;
	host.room_token = room_token;
	host.content_catalog_loaded = content_catalog_files_present(content_dir);
	reset_match_to_player_count(host.game, 2, content_dir);

#if defined(TACTICS_NET_USE_OPENSSL)
	SSL_CTX* tls_ctx = nullptr;
	if (wants_tls) {
		SSL_library_init();
		SSL_load_error_strings();
		OpenSSL_add_ssl_algorithms();
		tls_ctx = SSL_CTX_new(TLS_server_method());
		if (!tls_ctx) {
			std::cerr << "[tactics_net_server] SSL_CTX_new failed\n";
			return 1;
		}
		// Nonblocking sockets: let SSL_write report partial progress and tolerate the tx
		// buffer moving between retries (we erase sent bytes from the front).
		SSL_CTX_set_mode(tls_ctx, SSL_MODE_ENABLE_PARTIAL_WRITE | SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER);
		if (SSL_CTX_use_certificate_file(tls_ctx, tls_cert_path.c_str(), SSL_FILETYPE_PEM) != 1) {
			std::cerr << "[tactics_net_server] failed loading TLS cert: " << tls_cert_path << "\n";
			SSL_CTX_free(tls_ctx);
			return 1;
		}
		if (SSL_CTX_use_PrivateKey_file(tls_ctx, tls_key_path.c_str(), SSL_FILETYPE_PEM) != 1) {
			std::cerr << "[tactics_net_server] failed loading TLS key: " << tls_key_path << "\n";
			SSL_CTX_free(tls_ctx);
			return 1;
		}
		if (SSL_CTX_check_private_key(tls_ctx) != 1) {
			std::cerr << "[tactics_net_server] TLS private key does not match certificate\n";
			SSL_CTX_free(tls_ctx);
			return 1;
		}
	}
#endif

	sock_t listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (listen_sock == k_invalid_socket) {
		std::cerr << "[tactics_net_server] socket() failed\n";
		return 1;
	}
	int reuse = 1;
	setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&reuse), sizeof(reuse));
	if (!set_nonblocking(listen_sock)) {
		std::cerr << "[tactics_net_server] set_nonblocking failed\n";
		sock_close(listen_sock);
		return 1;
	}
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<uint16_t>(port));
	if (bind_addr == "0.0.0.0") {
		addr.sin_addr.s_addr = htonl(INADDR_ANY);
	} else {
		addr.sin_addr.s_addr = inet_addr(bind_addr.c_str());
		if (addr.sin_addr.s_addr == INADDR_NONE) {
			std::cerr << "[tactics_net_server] invalid --bind address: " << bind_addr << "\n";
			sock_close(listen_sock);
			return 1;
		}
	}
	if (bind(listen_sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		std::cerr << "[tactics_net_server] bind failed (" << sock_errno() << ")\n";
		sock_close(listen_sock);
		return 1;
	}
	if (listen(listen_sock, 16) != 0) {
		std::cerr << "[tactics_net_server] listen failed\n";
		sock_close(listen_sock);
		return 1;
	}

	std::vector<std::unique_ptr<Peer>> peers;
	std::cerr << "[tactics_net_server] listening on " << bind_addr << ":" << port
	          << " (P1 authority on server; WebSocket clients are P2+). Clients: "
	          << (wants_tls ? "wss://127.0.0.1:" : "ws://127.0.0.1:") << port << "/\n";
	if (wants_tls) {
		std::cerr << "[tactics_net_server] native TLS enabled via --tls-cert/--tls-key\n";
	}

	while (g_run) {
#ifdef _WIN32
		std::vector<WSAPOLLFD> pfds;
		std::vector<size_t> peer_index_by_pfd;
		WSAPOLLFD l{};
		l.fd = listen_sock;
		l.events = POLLIN;
		pfds.push_back(l);
		peer_index_by_pfd.push_back(SIZE_MAX);
		for (size_t i = 0; i < peers.size(); ++i) {
			if (peers[i] && peers[i]->sock != k_invalid_socket) {
				WSAPOLLFD e{};
				e.fd = peers[i]->sock;
				e.events = peers[i]->tx_buffer.empty() ? POLLIN : (POLLIN | POLLOUT);
				pfds.push_back(e);
				peer_index_by_pfd.push_back(i);
			}
		}
		const int pr = WSAPoll(pfds.data(), static_cast<ULONG>(pfds.size()), 200);
		if (pr == 0) {
			continue;
		}
		if (pr < 0) {
			break;
		}
		if ((pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
			break;
		}
		if ((pfds[0].revents & POLLIN) != 0) {
			sock_t ns = accept(listen_sock, nullptr, nullptr);
			if (ns != k_invalid_socket) {
				set_nonblocking(ns);
				auto peer = std::make_unique<Peer>();
				peer->sock = ns;
#if defined(TACTICS_NET_USE_OPENSSL)
				if (tls_ctx) {
					if (!set_blocking(ns)) {
						sock_close(ns);
						continue;
					}
					peer->ssl = SSL_new(tls_ctx);
					if (!peer->ssl) {
						sock_close(ns);
						continue;
					}
					SSL_set_fd(peer->ssl, static_cast<int>(ns));
					if (SSL_accept(peer->ssl) != 1) {
						SSL_free(peer->ssl);
						peer->ssl = nullptr;
						sock_close(ns);
						continue;
					}
					set_nonblocking(ns);
				}
#endif
				peer->phase = Peer::Phase::HttpHandshake;
				peers.push_back(std::move(peer));
				std::cerr << "[tactics_net_server] accepted TCP peer (handshake)\n";
			}
		}
		for (size_t j = 1; j < pfds.size(); ++j) {
			const size_t peer_i = peer_index_by_pfd[j];
			if (peer_i >= peers.size() || !peers[peer_i]) {
				continue;
			}
			handle_peer_io(host, peers, peer_i,
				(pfds[j].revents & POLLIN) != 0,
				(pfds[j].revents & POLLOUT) != 0,
				(pfds[j].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0);
		}
#else
		std::vector<pollfd> pfds;
		std::vector<size_t> peer_index_by_pfd;
		pfds.push_back({listen_sock, POLLIN, 0});
		peer_index_by_pfd.push_back(SIZE_MAX);
		for (size_t i = 0; i < peers.size(); ++i) {
			if (peers[i] && peers[i]->sock >= 0) {
				pfds.push_back({peers[i]->sock,
					static_cast<short>(peers[i]->tx_buffer.empty() ? POLLIN : (POLLIN | POLLOUT)), 0});
				peer_index_by_pfd.push_back(i);
			}
		}
		const int pr = poll(pfds.data(), pfds.size(), 200);
		if (pr == 0) {
			continue;
		}
		if (pr < 0) {
			break;
		}
		if (pfds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			break;
		}
		if (pfds[0].revents & POLLIN) {
			sock_t ns = accept(listen_sock, nullptr, nullptr);
			if (ns >= 0) {
				set_nonblocking(ns);
				auto peer = std::make_unique<Peer>();
				peer->sock = ns;
#if defined(TACTICS_NET_USE_OPENSSL)
				if (tls_ctx) {
					if (!set_blocking(ns)) {
						sock_close(ns);
						continue;
					}
					peer->ssl = SSL_new(tls_ctx);
					if (!peer->ssl) {
						sock_close(ns);
						continue;
					}
					SSL_set_fd(peer->ssl, ns);
					if (SSL_accept(peer->ssl) != 1) {
						SSL_free(peer->ssl);
						peer->ssl = nullptr;
						sock_close(ns);
						continue;
					}
					set_nonblocking(ns);
				}
#endif
				peer->phase = Peer::Phase::HttpHandshake;
				peers.push_back(std::move(peer));
				std::cerr << "[tactics_net_server] accepted TCP peer (handshake)\n";
			}
		}
		for (size_t j = 1; j < pfds.size(); ++j) {
			const size_t peer_i = peer_index_by_pfd[j];
			if (peer_i >= peers.size() || !peers[peer_i]) {
				continue;
			}
			handle_peer_io(host, peers, peer_i,
				(pfds[j].revents & POLLIN) != 0,
				(pfds[j].revents & POLLOUT) != 0,
				(pfds[j].revents & (POLLERR | POLLHUP | POLLNVAL)) != 0);
		}
#endif
		peers.erase(std::remove_if(peers.begin(), peers.end(),
					  [](const std::unique_ptr<Peer>& p) { return !p || p->sock == k_invalid_socket; }),
			peers.end());
	}

	host.clear_all_remote_selection();
	for (auto& p : peers) {
		if (p && p->sock != k_invalid_socket) {
			close_peer(*p);
		}
	}
	peers.clear();
	sock_close(listen_sock);
#if defined(TACTICS_NET_USE_OPENSSL)
	if (tls_ctx) {
		SSL_CTX_free(tls_ctx);
	}
#endif
#ifdef _WIN32
	WSACleanup();
#endif
	std::cerr << "[tactics_net_server] shutdown\n";
	return 0;
}
