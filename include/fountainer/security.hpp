// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Security configuration (design concept §9). TLS and Fountain HMAC are two
// separate domains:
//
//   TLS/mTLS   encrypts the transport, verifies the server chain and
//              authenticates us with a client certificate.
//   HMAC       authenticates control messages WITHIN the session and
//              binds them to device ID, KID, nonces and sequence.
//
// Principle: no silent "insecure". Whoever disables the hostname check
// says so explicitly (EndpointIdentityPolicy) — it no longer results from
// an "auto" heuristic.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include <fountainer/result.hpp>

namespace fountainer {

// How is the peer's identity verified?
enum class EndpointIdentityPolicy {
    // Verify the certificate chain AND the DNS name/IP SAN. Default for
    // everything that has a real name.
    VerifyDnsName,
    // Verify only the chain against the private CA. Legitimate for devices
    // with a dynamic DHCP IP and no IP SAN — but it must be intentional.
    VerifyCertificateChainOnly,
};

// Deliberately named ugly: only for development, never the default.
struct UnsafeTlsOptions {
    // Do not verify the server certificate at all. This makes the connection
    // worthless against active attackers.
    bool disable_peer_verification = false;
};

class TlsCredentials {
public:
    // The usual case for local maintenance access: private CA + our own
    // client certificate, device IP without SAN.
    static TlsCredentials mutual_tls(
        std::string ca_file, std::string client_certificate_file,
        std::string client_key_file,
        EndpointIdentityPolicy policy = EndpointIdentityPolicy::VerifyDnsName);

    // Server verification only (no client certificate). The firmware's local
    // server requires mTLS — this one is for other peers.
    static TlsCredentials server_only(
        std::string ca_file,
        EndpointIdentityPolicy policy = EndpointIdentityPolicy::VerifyDnsName);

    [[nodiscard]] const std::string& ca_file() const noexcept { return ca_file_; }
    [[nodiscard]] const std::string& certificate_file() const noexcept
    {
        return certificate_file_;
    }
    [[nodiscard]] const std::string& key_file() const noexcept { return key_file_; }
    [[nodiscard]] bool uses_mutual_tls() const noexcept
    {
        return !certificate_file_.empty();
    }
    [[nodiscard]] EndpointIdentityPolicy identity_policy() const noexcept
    {
        return policy_;
    }
    [[nodiscard]] const UnsafeTlsOptions& unsafe() const noexcept { return unsafe_; }

    TlsCredentials& set_unsafe_options(UnsafeTlsOptions options)
    {
        unsafe_ = options;
        return *this;
    }

    // Checks completeness and readability of the files BEFORE the first
    // connection attempt.
    [[nodiscard]] Status validate() const;

private:
    std::string ca_file_;
    std::string certificate_file_;
    std::string key_file_;
    EndpointIdentityPolicy policy_ = EndpointIdentityPolicy::VerifyDnsName;
    UnsafeTlsOptions unsafe_{};
};

// 32-byte key with zeroization on destruction.
class HmacKey {
public:
    HmacKey() = default;
    explicit HmacKey(std::vector<std::uint8_t> bytes) : bytes_(std::move(bytes)) {}
    ~HmacKey();

    HmacKey(const HmacKey&) = default;
    HmacKey& operator=(const HmacKey&) = default;
    HmacKey(HmacKey&&) = default;
    HmacKey& operator=(HmacKey&&) = default;

    [[nodiscard]] const std::vector<std::uint8_t>& bytes() const noexcept
    {
        return bytes_;
    }
    [[nodiscard]] bool empty() const noexcept { return bytes_.empty(); }

private:
    std::vector<std::uint8_t> bytes_;
};

class HmacCredentials {
public:
    // 64 hex characters from a file (mode 0600, never in the repo).
    static Result<HmacCredentials> from_file(std::string kid, const std::string& path);
    static Result<HmacCredentials> from_hex(std::string kid, std::string_view hex);

    [[nodiscard]] const std::string& kid() const noexcept { return kid_; }
    [[nodiscard]] const HmacKey& key() const noexcept { return key_; }

private:
    HmacCredentials(std::string kid, HmacKey key)
        : kid_(std::move(kid)), key_(std::move(key))
    {
    }

    std::string kid_;
    HmacKey key_;
};

// Backend extension (design concept §9.3): multiple devices, KID rotation,
// keys from a DB/vault. The local client uses FixedCredentialProvider.
class CredentialProvider {
public:
    virtual ~CredentialProvider() = default;

    // Called during the hello as soon as device_id and kid are known.
    // Error ⇒ the handshake is rejected with auth_required.
    virtual Result<HmacKey> resolve(std::string_view device_id,
                                    std::string_view kid) = 0;
};

class FixedCredentialProvider final : public CredentialProvider {
public:
    // expected_device_id empty = accept any identity (the device is
    // authenticated via mTLS anyway).
    FixedCredentialProvider(std::string kid, HmacKey key,
                            std::string expected_device_id = {});

    Result<HmacKey> resolve(std::string_view device_id,
                            std::string_view kid) override;

private:
    std::string kid_;
    HmacKey key_;
    std::string expected_device_id_;
};

// Bundled security intent of a client.
class SecurityConfiguration {
public:
    Status set_tls(TlsCredentials credentials);
    Status set_hmac(HmacCredentials credentials);
    Status set_credential_provider(std::shared_ptr<CredentialProvider> provider);

    // Expected device identity; empty = no check.
    void set_expected_device_id(std::string device_id);

    [[nodiscard]] const TlsCredentials* tls() const noexcept
    {
        return has_tls_ ? &tls_ : nullptr;
    }
    [[nodiscard]] const std::shared_ptr<CredentialProvider>& credentials() const noexcept
    {
        return provider_;
    }
    [[nodiscard]] const std::string& kid() const noexcept { return kid_; }
    [[nodiscard]] const std::string& expected_device_id() const noexcept
    {
        return expected_device_id_;
    }

    // After connect() security parameters must no longer change silently
    // (design concept §8.3) — the client locks them.
    void lock() noexcept { locked_ = true; }
    void unlock() noexcept { locked_ = false; }
    [[nodiscard]] bool locked() const noexcept { return locked_; }

    [[nodiscard]] Status validate() const;

private:
    TlsCredentials tls_;
    bool has_tls_ = false;
    std::shared_ptr<CredentialProvider> provider_;
    std::string kid_;
    std::string expected_device_id_;
    bool locked_ = false;
};

}  // namespace fountainer
