// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/protocol/auth.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>

#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "fountainer/protocol/canonical.hpp"

namespace fountainer::protocol {

namespace {

constexpr char kUnitSeparator = '\x1f';

std::string base64_encode(const unsigned char* data, std::size_t len)
{
    std::string out((len + 2) / 3 * 4 + 1, '\0');
    const int n = EVP_EncodeBlock(
        reinterpret_cast<unsigned char*>(out.data()), data,
        static_cast<int>(len));
    out.resize(static_cast<std::size_t>(n));
    return out;
}

// Envelope field as a string for the MAC input; missing/null -> "".
std::string field_string(const nlohmann::json& msg, const char* key)
{
    const auto it = msg.find(key);
    if (it == msg.end() || it->is_null()) {
        return "";
    }
    if (it->is_string()) {
        return it->get<std::string>();
    }
    if (it->is_number_integer()) {
        return std::to_string(it->get<std::int64_t>());
    }
    if (it->is_number_unsigned()) {
        return std::to_string(it->get<std::uint64_t>());
    }
    return canonical_serialize(*it);
}

}  // namespace

nlohmann::json canonical_body(const nlohmann::json& msg)
{
    nlohmann::json body = nlohmann::json::object();
    for (auto it = msg.begin(); it != msg.end(); ++it) {
        const bool is_envelope =
            std::find(kEnvelopeFields.begin(), kEnvelopeFields.end(),
                      it.key()) != kEnvelopeFields.end();
        if (!is_envelope) {
            body[it.key()] = it.value();
        }
    }
    return body;
}

std::string body_hash_hex(const nlohmann::json& msg)
{
    const std::string canon = canonical_serialize(canonical_body(msg));
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(canon.data()),
           canon.size(), digest);
    static const char* hex = "0123456789abcdef";
    std::string out;
    out.reserve(2 * SHA256_DIGEST_LENGTH);
    for (const unsigned char byte : digest) {
        out += hex[byte >> 4];
        out += hex[byte & 0x0f];
    }
    return out;
}

std::string mac_input(const nlohmann::json& msg, const AuthContext& ctx,
                      Direction dir, std::string_view kid, std::int64_t seq)
{
    // Order (AUTH-CONTRACT D.1): v, type, direction, device_id, serial,
    // ts, msg_id, in_reply_to, kid, seq, server_nonce, client_nonce, bhash
    const std::string fields[13] = {
        field_string(msg, "v"),
        field_string(msg, "type"),
        std::string(dir == Direction::C2s ? "c2s" : "s2c"),
        ctx.device_id,
        field_string(msg, "serial"),
        field_string(msg, "ts"),
        field_string(msg, "msg_id"),
        field_string(msg, "in_reply_to"),
        std::string(kid),
        std::to_string(seq),
        ctx.server_nonce,
        ctx.client_nonce,
        body_hash_hex(msg),
    };
    std::string raw;
    for (int i = 0; i < 13; i++) {
        if (i > 0) {
            raw += kUnitSeparator;
        }
        raw += fields[i];
    }
    return raw;
}

std::string compute_mac(const std::vector<std::uint8_t>& key,
                        std::string_view raw)
{
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digest_len = 0;
    HMAC(EVP_sha256(), key.data(), static_cast<int>(key.size()),
         reinterpret_cast<const unsigned char*>(raw.data()), raw.size(),
         digest, &digest_len);
    return base64_encode(digest, 16);   // first 128 bits
}

void sign(nlohmann::json& msg, const AuthContext& ctx, std::int64_t seq,
          Direction dir)
{
    const std::string raw = mac_input(msg, ctx, dir, ctx.kid, seq);
    msg["auth"] = {{"kid", ctx.kid}, {"seq", seq},
                   {"mac", compute_mac(ctx.key, raw)}};
}

VerifyResult verify(const nlohmann::json& msg, const AuthContext& ctx,
                    Direction dir)
{
    const auto auth_it = msg.find("auth");
    if (auth_it == msg.end() || !auth_it->is_object()) {
        return {false, "missing_auth"};
    }
    const std::string kid = auth_it->value("kid", "");
    if (kid != ctx.kid) {
        return {false, "kid_mismatch"};
    }
    const auto seq_it = auth_it->find("seq");
    if (seq_it == auth_it->end() || !seq_it->is_number_integer()) {
        return {false, "bad_seq"};
    }
    const std::int64_t seq = seq_it->get<std::int64_t>();
    const std::string raw = mac_input(msg, ctx, dir, kid, seq);
    const std::string expected = compute_mac(ctx.key, raw);
    const std::string given = auth_it->value("mac", "");
    if (expected.size() != given.size() ||
        CRYPTO_memcmp(expected.data(), given.data(), expected.size()) != 0) {
        return {false, "mac_mismatch"};
    }
    return {true, "ok"};
}

std::vector<std::uint8_t> load_hmac_key_file(const std::string& path)
{
    std::ifstream stream(path);
    if (!stream) {
        throw std::runtime_error("fountain.hmac_key_file: cannot open " + path);
    }
    std::string hex;
    for (char c = 0; stream.get(c);) {
        if (!std::isspace(static_cast<unsigned char>(c))) {
            hex += c;
        }
    }
    if (hex.size() != 64) {
        throw std::runtime_error(
            "fountain.hmac_key_file: expected 64 hex chars in " + path);
    }
    auto nibble = [&path](char c) -> std::uint8_t {
        if (c >= '0' && c <= '9') return static_cast<std::uint8_t>(c - '0');
        if (c >= 'a' && c <= 'f') return static_cast<std::uint8_t>(c - 'a' + 10);
        if (c >= 'A' && c <= 'F') return static_cast<std::uint8_t>(c - 'A' + 10);
        throw std::runtime_error(
            "fountain.hmac_key_file: invalid hex in " + path);
    };
    std::vector<std::uint8_t> key(32);
    for (std::size_t i = 0; i < 32; i++) {
        key[i] = static_cast<std::uint8_t>(
            (nibble(hex[2 * i]) << 4) | nibble(hex[2 * i + 1]));
    }
    return key;
}

}  // namespace fountainer::protocol
