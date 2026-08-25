/**
 * Headless WebSocket join client for tactics_net_server.
 * P1 is the server. This process is assigned P2+ and sends the same CLI lines as Unreal Join.
 *
 * Run: tactics_net_client [--host 127.0.0.1] [--port 8788] [--seat 2] [--token SECRET]
 */

#include "tactics/core.hpp"
#include "tactics/sync/match_auth.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

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
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

#ifdef _WIN32
using sock_t = SOCKET;
constexpr sock_t k_invalid = INVALID_SOCKET;
inline void sock_close(sock_t s)
{
	if (s != k_invalid) {
		closesocket(s);
	}
}
#else
using sock_t = int;
constexpr sock_t k_invalid = -1;
inline void sock_close(sock_t s)
{
	if (s >= 0) {
		close(s);
	}
}
#endif

constexpr uint64_t kMaxFrame = 32ull * 1024 * 1024;

std::string b64_encode(const uint8_t* data, size_t n)
{
	static const char kTbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
	std::string out;
	out.reserve(((n + 2) / 3) * 4);
	for (size_t i = 0; i < n; i += 3) {
		const uint32_t a = data[i];
		const uint32_t b = i + 1 < n ? data[i + 1] : 0;
		const uint32_t c = i + 2 < n ? data[i + 2] : 0;
		const uint32_t triple = (a << 16) | (b << 8) | c;
		out.push_back(kTbl[(triple >> 18) & 63]);
		out.push_back(kTbl[(triple >> 12) & 63]);
		out.push_back(i + 1 < n ? kTbl[(triple >> 6) & 63] : '=');
		out.push_back(i + 2 < n ? kTbl[triple & 63] : '=');
	}
	return out;
}

bool fill_random(uint8_t* out, size_t n)
{
#ifdef _WIN32
	HCRYPTPROV prov = 0;
	if (!CryptAcquireContext(&prov, nullptr, nullptr, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
		return false;
	}
	const BOOL ok = CryptGenRandom(prov, static_cast<DWORD>(n), out);
	CryptReleaseContext(prov, 0);
	return ok != 0;
#else
	for (size_t i = 0; i < n; ++i) {
		out[i] = static_cast<uint8_t>(std::rand() & 0xff);
	}
	return true;
#endif
}

bool send_all(sock_t s, const uint8_t* p, size_t n)
{
	size_t off = 0;
	while (off < n) {
#ifdef _WIN32
		const int w = send(s, reinterpret_cast<const char*>(p + off), static_cast<int>(n - off), 0);
#else
		const int w = static_cast<int>(send(s, p + off, n - off, 0));
#endif
		if (w <= 0) {
			return false;
		}
		off += static_cast<size_t>(w);
	}
	return true;
}

bool send_ws_text_masked(sock_t s, const std::string& utf8)
{
	const size_t payload_len = utf8.size();
	std::vector<uint8_t> frame;
	frame.push_back(0x81);
	if (payload_len <= 125) {
		frame.push_back(static_cast<uint8_t>(0x80 | payload_len));
	} else if (payload_len <= 0xffff) {
		frame.push_back(static_cast<uint8_t>(0x80 | 126));
		frame.push_back(static_cast<uint8_t>((payload_len >> 8) & 0xff));
		frame.push_back(static_cast<uint8_t>(payload_len & 0xff));
	} else {
		frame.push_back(static_cast<uint8_t>(0x80 | 127));
		for (int b = 7; b >= 0; --b) {
			frame.push_back(static_cast<uint8_t>((payload_len >> (b * 8)) & 0xff));
		}
	}
	uint8_t mask[4]{};
	if (!fill_random(mask, 4)) {
		return false;
	}
	frame.insert(frame.end(), mask, mask + 4);
	for (size_t i = 0; i < payload_len; ++i) {
		frame.push_back(static_cast<uint8_t>(utf8[i]) ^ mask[i % 4]);
	}
	return send_all(s, frame.data(), frame.size());
}

bool recv_some(sock_t s, std::vector<uint8_t>& buf)
{
	uint8_t tmp[4096];
#ifdef _WIN32
	const int n = recv(s, reinterpret_cast<char*>(tmp), sizeof(tmp), 0);
#else
	const int n = static_cast<int>(recv(s, tmp, sizeof(tmp), 0));
#endif
	if (n <= 0) {
		return false;
	}
	buf.insert(buf.end(), tmp, tmp + n);
	return true;
}

bool pop_ws_text(std::vector<uint8_t>& buf, std::string& out_text, bool& closed)
{
	closed = false;
	if (buf.size() < 2) {
		return false;
	}
	const uint8_t b0 = buf[0];
	const uint8_t b1 = buf[1];
	const uint8_t opcode = b0 & 0x0f;
	const bool masked = (b1 & 0x80) != 0;
	uint64_t payload_len = b1 & 0x7f;
	size_t idx = 2;
	if (payload_len == 126) {
		if (buf.size() < 4) {
			return false;
		}
		payload_len = (static_cast<uint64_t>(buf[2]) << 8) | buf[3];
		idx = 4;
	} else if (payload_len == 127) {
		if (buf.size() < 10) {
			return false;
		}
		payload_len = 0;
		for (int b = 0; b < 8; ++b) {
			payload_len = (payload_len << 8) | buf[2 + b];
		}
		idx = 10;
	}
	uint8_t mask[4]{};
	if (masked) {
		if (buf.size() < idx + 4) {
			return false;
		}
		std::memcpy(mask, &buf[idx], 4);
		idx += 4;
	}
	if (payload_len > kMaxFrame) {
		closed = true;
		return false;
	}
	if (buf.size() < idx + payload_len) {
		return false;
	}
	std::string payload;
	payload.resize(static_cast<size_t>(payload_len));
	for (uint64_t i = 0; i < payload_len; ++i) {
		uint8_t byte = buf[idx + static_cast<size_t>(i)];
		if (masked) {
			byte ^= mask[static_cast<size_t>(i % 4)];
		}
		payload[static_cast<size_t>(i)] = static_cast<char>(byte);
	}
	buf.erase(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(idx + payload_len));
	if (opcode == 0x8) {
		closed = true;
		return false;
	}
	if (opcode == 0x1) {
		out_text = std::move(payload);
		return true;
	}
	return false;
}

bool http_upgrade(sock_t s, const std::string& host, int port, int seat)
{
	uint8_t key_raw[16]{};
	if (!fill_random(key_raw, 16)) {
		return false;
	}
	const std::string key = b64_encode(key_raw, 16);
	std::string path = "/";
	if (seat > 0) {
		path = "/?seat=" + std::to_string(seat);
	}
	const std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port)
		+ "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: " + key
		+ "\r\nSec-WebSocket-Version: 13\r\n\r\n";
	if (!send_all(s, reinterpret_cast<const uint8_t*>(req.data()), req.size())) {
		return false;
	}
	std::string hdr;
	char tmp[512];
	while (hdr.find("\r\n\r\n") == std::string::npos && hdr.size() < 8192) {
#ifdef _WIN32
		const int n = recv(s, tmp, sizeof(tmp), 0);
#else
		const int n = static_cast<int>(recv(s, tmp, sizeof(tmp), 0));
#endif
		if (n <= 0) {
			return false;
		}
		hdr.append(tmp, tmp + n);
	}
	return hdr.find("101") != std::string::npos;
}

}  // namespace

int main(int argc, char** argv)
{
	std::string host = "127.0.0.1";
	int port = 8788;
	int seat = 0;
	std::string token;
	for (int i = 1; i < argc; ++i) {
		if (std::strcmp(argv[i], "--host") == 0 && i + 1 < argc) {
			host = argv[++i];
		} else if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
			port = std::atoi(argv[++i]);
		} else if (std::strcmp(argv[i], "--seat") == 0 && i + 1 < argc) {
			seat = std::atoi(argv[++i]);
		} else if (std::strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
			token = argv[++i];
		}
	}

#ifdef _WIN32
	WSADATA wsa{};
	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
		std::cerr << "[tactics_net_client] WSAStartup failed\n";
		return 1;
	}
#endif

	sock_t s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (s == k_invalid) {
		std::cerr << "[tactics_net_client] socket failed\n";
		return 1;
	}
	sockaddr_in addr{};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(static_cast<uint16_t>(port));
	addr.sin_addr.s_addr = inet_addr(host.c_str());
	if (connect(s, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
		std::cerr << "[tactics_net_client] connect failed. Is run_net_server.bat running?\n";
		sock_close(s);
		return 1;
	}
	if (!http_upgrade(s, host, port, seat)) {
		std::cerr << "[tactics_net_client] websocket handshake failed\n";
		sock_close(s);
		return 1;
	}

	std::cerr << "[tactics_net_client] joined " << host << ":" << port
	          << ". Type CLI lines (help, board, deploy, end, quit).\n";

	int assigned_seat = seat > 0 ? seat : 2;
	std::string auth_nonce;
	uint64_t ctr = 0;
	std::atomic<bool> run{true};
	std::mutex mu;
	std::queue<std::string> typed;
	std::thread reader([&]() {
		std::string line;
		while (run && std::getline(std::cin, line)) {
			std::lock_guard<std::mutex> g(mu);
			typed.push(std::move(line));
		}
		run = false;
	});

	std::vector<uint8_t> rx;
	while (run) {
#ifdef _WIN32
		WSAPOLLFD pfd{};
		pfd.fd = s;
		pfd.events = POLLIN;
		WSAPoll(&pfd, 1, 80);
		if (pfd.revents & POLLIN) {
			if (!recv_some(s, rx)) {
				break;
			}
		}
#else
		fd_set rfds;
		FD_ZERO(&rfds);
		FD_SET(s, &rfds);
		timeval tv{};
		tv.tv_usec = 80000;
		if (select(s + 1, &rfds, nullptr, nullptr, &tv) > 0 && FD_ISSET(s, &rfds)) {
			if (!recv_some(s, rx)) {
				break;
			}
		}
#endif
		for (;;) {
			std::string text;
			bool closed = false;
			if (!pop_ws_text(rx, text, closed)) {
				if (closed) {
					run = false;
				}
				break;
			}
			try {
				const auto j = nlohmann::json::parse(text);
				const std::string t = j.value("t", "");
				if (t == "welcome") {
					assigned_seat = j.value("seat", assigned_seat);
					auth_nonce = j.value("auth_nonce", "");
					std::cout << "[welcome] seat P" << assigned_seat << "\n";
					nlohmann::json r;
					r["t"] = "resync";
					r["v"] = tactics::kNetworkWireVersion;
					if (!send_ws_text_masked(s, r.dump())) {
						run = false;
					}
				} else if (t == "cli_ack") {
					std::cout << j.value("msg", "") << "\n";
				} else if (t == "snap" || t == "snap_delta") {
					// Board state is on the host. Ask for it with `board` if needed.
				} else if (t == "cmd") {
					// Peer command journal; ignore.
				}
			} catch (const std::exception&) {
			}
		}

		std::vector<std::string> batch;
		{
			std::lock_guard<std::mutex> g(mu);
			while (!typed.empty()) {
				batch.push_back(std::move(typed.front()));
				typed.pop();
			}
		}
		for (const std::string& line : batch) {
			if (line == "quit" || line == "exit") {
				run = false;
				break;
			}
			nlohmann::json cli;
			cli["t"] = "cli";
			cli["v"] = tactics::kNetworkWireVersion;
			cli["seat"] = assigned_seat;
			cli["line"] = line;
			if (!token.empty()) {
				++ctr;
				cli["ctr"] = ctr;
				cli["sig"] = tactics::compute_cli_auth_digest_utf8(token, auth_nonce, assigned_seat, ctr, line);
			}
			if (!send_ws_text_masked(s, cli.dump())) {
				run = false;
				break;
			}
		}
	}

	run = false;
	sock_close(s);
#ifdef _WIN32
	CancelIoEx(GetStdHandle(STD_INPUT_HANDLE), nullptr);
	WSACleanup();
#endif
	if (reader.joinable()) {
		reader.detach();
	}
	return 0;
}
