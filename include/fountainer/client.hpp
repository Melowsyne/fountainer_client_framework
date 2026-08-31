// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// fountainer::Client — the high-level facade (design concept §8).
//
//   Client client{Endpoint{"192.168.1.51", 4443, "/ws"}};
//   client.security().set_tls(TlsCredentials::mutual_tls(ca, crt, key,
//       EndpointIdentityPolicy::VerifyCertificateChainOnly));
//   client.security().set_hmac(*HmacCredentials::from_file("1", "dev.hmac"));
//   auto info = client.connect();          // success == Fountain RUNNING
//   auto p = client.datapoints().read(dp::Fon_Current_Pressure);
//
// connect() only counts as successful once the Fountain session has reached
// RUNNING — a WSS handshake alone is not enough, because the firmware ignores
// control messages before that point (§8.2).
//
// Threading: the client owns its own IO thread. Synchronous methods
// (connect, read, write, ...) block and must therefore not be called from
// callbacks — the async_ variants of the services are available there
// instead. Event/subscription callbacks run on the IO thread and must stay
// short.
//
// Lifetime: destroy subscriptions (EventBus, DatapointManager) before the
// client, or hold them via RAII in shorter-lived scopes.
#pragma once

#include <memory>

#include <fountainer/client_options.hpp>
#include <fountainer/commands.hpp>
#include <fountainer/connection.hpp>
#include <fountainer/datapoints/manager.hpp>
#include <fountainer/datapoints/poller.hpp>
#include <fountainer/endpoint.hpp>
#include <fountainer/events.hpp>
#include <fountainer/logs.hpp>
#include <fountainer/maintenance.hpp>
#include <fountainer/protocol/ota_policy.hpp>
#include <fountainer/security.hpp>

namespace fountainer {

// Escape hatch (design concept §8.1): raw requests for tools/tests.
// Deliberately not the default API.
class RawProtocol {
public:
    using SubmitFn =
        std::function<void(protocol::RequestSpec, protocol::ResponseHandler)>;
    using SendFn = std::function<void(std::string, nlohmann::json)>;
    using BlockingRunner = std::function<Status(std::function<void()>)>;

    RawProtocol() = default;
    void bind(SubmitFn submit, SendFn send, BlockingRunner runner);

    void async_request(protocol::RequestSpec spec, protocol::ResponseHandler handler);
    Result<nlohmann::json> request(protocol::RequestSpec spec);

    // Fire-and-forget for types without a defined response (META request=false).
    void send(std::string type, nlohmann::json body);

private:
    SubmitFn submit_;
    SendFn send_;
    BlockingRunner runner_;
};

class Client {
public:
    using ConnectCompletion = std::function<void(Result<ConnectionInfo>)>;
    using VoidCompletion = std::function<void(Result<void>)>;

    class Builder;

    explicit Client(Endpoint endpoint);
    ~Client();
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;

    // Builder variant for immutable configuration (design concept §8.3):
    //   auto client = Client::builder(endpoint)
    //                     .with_tls(...).with_hmac(...).build();
    static Builder builder(Endpoint endpoint);

    SecurityConfiguration& security() noexcept;
    ClientOptions& options() noexcept;

    // Only for backend/special cases; the default is NoUpdatePolicy.
    void set_ota_policy(std::shared_ptr<protocol::OtaPolicy> policy);

    // --- Lifecycle ---
    // Already Ready ⇒ returns the current ConnectionInfo immediately.
    Result<ConnectionInfo> connect();
    void async_connect(ConnectCompletion completion);

    // Waits until the connection is actually closed.
    Result<void> disconnect();
    void async_disconnect(VoidCompletion completion);

    [[nodiscard]] ClientState state() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] std::optional<ConnectionInfo> connection_info() const;

    // --- Domain services ---
    DatapointManager& datapoints() noexcept;
    DatapointPoller& polling() noexcept;
    CommandService& commands() noexcept;
    LogService& logs() noexcept;
    MaintenanceService& maintenance() noexcept;
    EventBus& events() noexcept;
    RawProtocol& raw() noexcept;

    [[nodiscard]] protocol::DispatcherMetrics metrics() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Fluent configuration; build() validates and returns errors as values.
class Client::Builder {
public:
    explicit Builder(Endpoint endpoint) : endpoint_(std::move(endpoint)) {}

    Builder& with_tls(TlsCredentials credentials);
    Builder& with_hmac(HmacCredentials credentials);
    Builder& with_credential_provider(std::shared_ptr<CredentialProvider> provider);
    Builder& with_expected_device(std::string device_id);
    Builder& with_options(ClientOptions options);
    Builder& with_reconnect(ReconnectPolicy policy);
    Builder& with_ota_policy(std::shared_ptr<protocol::OtaPolicy> policy);

    // Files are only checked here — configuration errors are reported as a
    // Result, not only at connect().
    [[nodiscard]] Result<Client> build();

private:
    Endpoint endpoint_;
    std::optional<TlsCredentials> tls_;
    std::optional<HmacCredentials> hmac_;
    std::shared_ptr<CredentialProvider> provider_;
    std::string expected_device_;
    std::optional<ClientOptions> options_;
    std::optional<ReconnectPolicy> reconnect_;
    std::shared_ptr<protocol::OtaPolicy> ota_;
};

}  // namespace fountainer
