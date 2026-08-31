// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/security.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <utility>

#include <openssl/crypto.h>

namespace fountainer {
namespace {

Status require_readable(const std::string& path, const char* what)
{
    if (path.empty()) {
        return fail(config_error(ErrorCode::MissingCredentials,
                                 std::string(what) + " is not configured"));
    }
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(config_error(ErrorCode::FileNotReadable,
                                 std::string(what) + " is not readable: " + path));
    }
    return ok();
}

Result<std::vector<std::uint8_t>> decode_hex_key(std::string_view hex)
{
    std::vector<std::uint8_t> bytes;
    bytes.reserve(32);
    int high = -1;
    for (const char c : hex) {
        if (std::isspace(static_cast<unsigned char>(c))) continue;
        int nibble;
        if (c >= '0' && c <= '9') nibble = c - '0';
        else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10;
        else {
            // NEVER quote the file content — it is key material.
            return fail(config_error(ErrorCode::MissingCredentials,
                                     "hmac key contains a non-hex character"));
        }
        if (high < 0) {
            high = nibble;
        } else {
            bytes.push_back(static_cast<std::uint8_t>((high << 4) | nibble));
            high = -1;
        }
    }
    if (high >= 0 || bytes.size() != 32) {
        return fail(config_error(
            ErrorCode::MissingCredentials,
            "hmac key must be exactly 64 hex characters (32 bytes), got " +
                std::to_string(bytes.size()) + " bytes"));
    }
    return bytes;
}

}  // namespace

// --- TlsCredentials ---------------------------------------------------------

TlsCredentials TlsCredentials::mutual_tls(std::string ca_file,
                                          std::string client_certificate_file,
                                          std::string client_key_file,
                                          EndpointIdentityPolicy policy)
{
    TlsCredentials out;
    out.ca_file_ = std::move(ca_file);
    out.certificate_file_ = std::move(client_certificate_file);
    out.key_file_ = std::move(client_key_file);
    out.policy_ = policy;
    return out;
}

TlsCredentials TlsCredentials::server_only(std::string ca_file,
                                           EndpointIdentityPolicy policy)
{
    TlsCredentials out;
    out.ca_file_ = std::move(ca_file);
    out.policy_ = policy;
    return out;
}

Status TlsCredentials::validate() const
{
    if (!unsafe_.disable_peer_verification) {
        if (auto status = require_readable(ca_file_, "tls ca_file"); !status) {
            return status;
        }
    }
    if (uses_mutual_tls()) {
        if (auto status = require_readable(certificate_file_,
                                           "tls client_certificate_file");
            !status) {
            return status;
        }
        if (auto status = require_readable(key_file_, "tls client_key_file");
            !status) {
            return status;
        }
    }
    return ok();
}

// --- HmacKey / HmacCredentials ---------------------------------------------

HmacKey::~HmacKey()
{
    if (!bytes_.empty()) {
        OPENSSL_cleanse(bytes_.data(), bytes_.size());
    }
}

Result<HmacCredentials> HmacCredentials::from_file(std::string kid,
                                                   const std::string& path)
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return fail(config_error(ErrorCode::FileNotReadable,
                                 "hmac key file is not readable: " + path));
    }
    std::ostringstream buffer;
    buffer << file.rdbuf();
    auto result = from_hex(std::move(kid), buffer.str());
    if (!result) {
        Error error = result.error();
        error.message += " (" + path + ")";
        return fail(std::move(error));
    }
    return result;
}

Result<HmacCredentials> HmacCredentials::from_hex(std::string kid,
                                                  std::string_view hex)
{
    if (kid.empty()) {
        return fail(config_error(ErrorCode::MissingCredentials,
                                 "hmac kid must not be empty"));
    }
    auto bytes = decode_hex_key(hex);
    if (!bytes) return fail(bytes.error());
    return HmacCredentials(std::move(kid), HmacKey(std::move(*bytes)));
}

// --- FixedCredentialProvider ------------------------------------------------

FixedCredentialProvider::FixedCredentialProvider(std::string kid, HmacKey key,
                                                 std::string expected_device_id)
    : kid_(std::move(kid)),
      key_(std::move(key)),
      expected_device_id_(std::move(expected_device_id))
{
}

Result<HmacKey> FixedCredentialProvider::resolve(std::string_view device_id,
                                                 std::string_view kid)
{
    if (!expected_device_id_.empty() && device_id != expected_device_id_) {
        return fail(make_error(ErrorDomain::Authentication, ErrorCode::AuthRejected,
                               "unexpected device_id '" + std::string(device_id) +
                                   "' (configured: " + expected_device_id_ + ")"));
    }
    if (kid != kid_) {
        return fail(make_error(ErrorDomain::Authentication, ErrorCode::UnknownKeyId,
                               "device offers kid '" + std::string(kid) +
                                   "', configured is '" + kid_ + "'"));
    }
    return key_;
}

// --- SecurityConfiguration --------------------------------------------------

Status SecurityConfiguration::set_tls(TlsCredentials credentials)
{
    if (locked_) {
        return fail(make_error(ErrorDomain::Configuration, ErrorCode::InvalidState,
                               "TLS cannot be changed while connected"));
    }
    if (auto status = credentials.validate(); !status) return status;
    tls_ = std::move(credentials);
    has_tls_ = true;
    return ok();
}

Status SecurityConfiguration::set_hmac(HmacCredentials credentials)
{
    if (locked_) {
        return fail(make_error(ErrorDomain::Configuration, ErrorCode::InvalidState,
                               "HMAC credentials cannot be changed while connected"));
    }
    kid_ = credentials.kid();
    // Deliberately WITHOUT device binding: the identity check is always done
    // by the session with the CURRENT expected_device_id() — otherwise the
    // behaviour would depend on the order of set_hmac()/set_expected_device_id().
    provider_ = std::make_shared<FixedCredentialProvider>(credentials.kid(),
                                                          credentials.key());
    return ok();
}

Status SecurityConfiguration::set_credential_provider(
    std::shared_ptr<CredentialProvider> provider)
{
    if (locked_) {
        return fail(make_error(ErrorDomain::Configuration, ErrorCode::InvalidState,
                               "credential provider cannot be changed while connected"));
    }
    if (!provider) {
        return fail(config_error(ErrorCode::MissingCredentials,
                                 "credential provider must not be null"));
    }
    provider_ = std::move(provider);
    return ok();
}

void SecurityConfiguration::set_expected_device_id(std::string device_id)
{
    expected_device_id_ = std::move(device_id);
}

Status SecurityConfiguration::validate() const
{
    if (!has_tls_) {
        return fail(config_error(ErrorCode::MissingCredentials,
                                 "no TLS credentials configured — the local "
                                 "device server requires mutual TLS"));
    }
    if (auto status = tls_.validate(); !status) return status;
    if (!provider_) {
        return fail(config_error(ErrorCode::MissingCredentials,
                                 "no HMAC credentials configured — the Fountain "
                                 "handshake cannot complete"));
    }
    return ok();
}

}  // namespace fountainer
