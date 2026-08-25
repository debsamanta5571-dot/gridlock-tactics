#include "tactics/sync/match_auth.hpp"

#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincrypt.h>
#else
#include <openssl/sha.h>
#endif

namespace tactics {
namespace {

std::string bytes_to_hex(const uint8_t* data, const std::size_t len)
{
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (std::size_t i = 0; i < len; ++i) {
        oss << std::setw(2) << static_cast<int>(data[i]);
    }
    return oss.str();
}

bool sha256_bytes(const uint8_t* data, const std::size_t len, uint8_t out32[32])
{
#ifdef _WIN32
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    if (!CryptAcquireContext(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return false;
    }
    if (!CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash)) {
        CryptReleaseContext(prov, 0);
        return false;
    }
    if (!CryptHashData(hash, reinterpret_cast<const BYTE*>(data), static_cast<DWORD>(len), 0)) {
        CryptDestroyHash(hash);
        CryptReleaseContext(prov, 0);
        return false;
    }
    DWORD out_len = 32;
    const bool ok = CryptGetHashParam(hash, HP_HASHVAL, out32, &out_len, 0) != 0;
    CryptDestroyHash(hash);
    CryptReleaseContext(prov, 0);
    return ok && out_len == 32;
#else
    SHA256(data, len, out32);
    return true;
#endif
}

/** HMAC-SHA256 (RFC 2104). A plain hash of secret||payload is length-extendable; HMAC is not. */
bool hmac_sha256(const std::string& key, const std::string& message, uint8_t out32[32])
{
    constexpr std::size_t kBlockSize = 64;
    uint8_t key_block[kBlockSize]{};
    if (key.size() > kBlockSize) {
        uint8_t key_digest[32]{};
        if (!sha256_bytes(reinterpret_cast<const uint8_t*>(key.data()), key.size(), key_digest)) {
            return false;
        }
        std::memcpy(key_block, key_digest, 32);
    } else {
        std::memcpy(key_block, key.data(), key.size());
    }

    uint8_t ipad[kBlockSize];
    uint8_t opad[kBlockSize];
    for (std::size_t i = 0; i < kBlockSize; ++i) {
        ipad[i] = key_block[i] ^ 0x36;
        opad[i] = key_block[i] ^ 0x5c;
    }

    std::string inner_input;
    inner_input.reserve(kBlockSize + message.size());
    inner_input.append(reinterpret_cast<const char*>(ipad), kBlockSize);
    inner_input.append(message);
    uint8_t inner_digest[32]{};
    if (!sha256_bytes(reinterpret_cast<const uint8_t*>(inner_input.data()), inner_input.size(), inner_digest)) {
        return false;
    }

    std::string outer_input;
    outer_input.reserve(kBlockSize + 32);
    outer_input.append(reinterpret_cast<const char*>(opad), kBlockSize);
    outer_input.append(reinterpret_cast<const char*>(inner_digest), 32);
    return sha256_bytes(reinterpret_cast<const uint8_t*>(outer_input.data()), outer_input.size(), out32);
}

std::string canonical_cli_auth_payload_utf8(const std::string& auth_nonce, const int seat, const uint64_t ctr,
    const std::string& line_utf8)
{
    return "v2|" + auth_nonce + "|" + std::to_string(seat) + "|" + std::to_string(ctr) + "|" + line_utf8;
}

/** Compare without early exit so timing does not leak how many leading chars matched. */
bool constant_time_equals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    volatile unsigned char acc = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        acc = static_cast<unsigned char>(acc | (static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i])));
    }
    return acc == 0;
}

bool fill_random_bytes(uint8_t* out, const std::size_t len)
{
#ifdef _WIN32
    HCRYPTPROV prov = 0;
    if (!CryptAcquireContext(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT)) {
        return false;
    }
    const bool ok = CryptGenRandom(prov, static_cast<DWORD>(len), out) != 0;
    CryptReleaseContext(prov, 0);
    return ok;
#else
    // std::random_device is non-deterministic on the Linux targets we ship to.
    std::random_device rd;
    for (std::size_t i = 0; i < len; ++i) {
        out[i] = static_cast<uint8_t>(rd() & 0xff);
    }
    return true;
#endif
}

}  // namespace

std::string generate_auth_nonce_hex()
{
    uint8_t bytes[16]{};
    if (!fill_random_bytes(bytes, sizeof(bytes))) {
        return {};
    }
    return bytes_to_hex(bytes, sizeof(bytes));
}

std::string compute_cli_auth_digest_utf8(const std::string& room_secret, const std::string& auth_nonce, const int seat,
    const uint64_t ctr, const std::string& line_utf8)
{
    if (room_secret.empty()) {
        return {};
    }
    const std::string payload = canonical_cli_auth_payload_utf8(auth_nonce, seat, ctr, line_utf8);
    uint8_t digest[32]{};
    if (!hmac_sha256(room_secret, payload, digest)) {
        return {};
    }
    return bytes_to_hex(digest, 32);
}

bool verify_cli_auth_digest_utf8(const std::string& room_secret, const std::string& auth_nonce, const int seat, const uint64_t ctr,
    const std::string& line_utf8, const std::string& digest)
{
    if (room_secret.empty()) {
        return digest.empty();
    }
    if (digest.empty()) {
        return false;
    }
    return constant_time_equals(compute_cli_auth_digest_utf8(room_secret, auth_nonce, seat, ctr, line_utf8), digest);
}

}  // namespace tactics
