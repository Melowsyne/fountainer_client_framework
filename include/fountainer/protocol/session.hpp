// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// ControllerSession — Fountain v2.2 in the CONTROLLER role over an
// arbitrary text transport.
//
// Role direction (design concept §3.1): at the network level we are the
// WebSocket CLIENT, at the protocol level the ESP32 remains the DEVICE and
// starts with hello. We play the controller/server role: hello_ack, verify
// the proof, decide on OTA, sign control messages.
//
// Deliberately WITHOUT Asio: send/close/clock are injected, tick(now)
// drives deadlines. This makes the complete state machine unit-testable
// and reusable in the backend over an accepted connection.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

#include <fountainer/connection.hpp>
#include <fountainer/events.hpp>
#include <fountainer/protocol/auth.hpp>
#include <fountainer/protocol/dispatcher.hpp>
#include <fountainer/protocol/fountain_messages.hpp>
#include <fountainer/protocol/ota_policy.hpp>
#include <fountainer/security.hpp>

namespace fountainer::protocol {

enum class SessionState { Closed, AwaitHello, AwaitProof, Running };

std::string_view to_string(SessionState state) noexcept;

struct ControllerSessionConfig {
    std::shared_ptr<CredentialProvider> credentials;
    std::string kid = "1";                 // the one we offer to the device
    std::string expected_device_id;        // "" = allow any identity
    std::shared_ptr<OtaPolicy> ota_policy; // nullptr = NoUpdatePolicy
    std::chrono::milliseconds handshake_timeout{15000};
    std::chrono::milliseconds default_request_timeout{10000};
    RateBudget budget{};
};

class ControllerSession {
public:
    using SendFn = std::function<bool(std::string)>;
    using CloseFn = std::function<void(std::uint16_t, std::string)>;
    using NowFn = std::function<TimePoint()>;

    struct Callbacks {
        // Fountain RUNNING reached — only THAT counts as "connected".
        std::function<void(const ConnectionInfo&)> on_ready;
        // Handshake/authentication failed for good.
        std::function<void(Error)> on_failed;
        // ONLY unsolicited reports; responses to dp_read go through the
        // dispatcher to the caller.
        std::function<void(const nlohmann::json& dp, std::optional<std::uint32_t> seq)>
            on_dp_report;
        std::function<void(const Heartbeat&)> on_heartbeat;
        std::function<void(const DeviceAlert&)> on_device_alert;
        std::function<void(const OtaStatus&)> on_ota_status;
        std::function<void(const ErrorReport&)> on_error_report;
        std::function<void(const ProtocolWarning&)> on_warning;
        std::function<void(std::string_view, const nlohmann::json&)> on_unknown;
    };

    ControllerSession(ControllerSessionConfig config, SendFn send, CloseFn close,
                      NowFn now = nullptr);

    void set_callbacks(Callbacks callbacks);

    // --- Transport events ---
    void on_transport_open();
    void on_transport_closed(const Error& error);
    void on_text(std::string_view frame);

    // Check deadlines (handshake + requests) and send waiting requests.
    void tick(TimePoint now);

    // --- Application ---
    // The handler is guaranteed to be called exactly once.
    void request(RequestSpec spec, ResponseHandler handler);

    // Message without expecting a response (hello_ack, ota_none, ...).
    bool send_message(std::string_view type, const nlohmann::json& body,
                      const std::string& in_reply_to = "");

    [[nodiscard]] SessionState state() const noexcept { return state_; }
    [[nodiscard]] bool running() const noexcept
    {
        return state_ == SessionState::Running;
    }
    [[nodiscard]] const ConnectionInfo& info() const noexcept { return info_; }
    [[nodiscard]] DispatcherMetrics metrics() const { return dispatcher_.metrics(); }

    // Time of the last SENT frame — basis for the Fountain keepalive
    // (the device only counts received frames against its 300 s idle
    // timeout; a WS ping does not help).
    [[nodiscard]] TimePoint last_tx() const noexcept { return last_tx_; }

private:
    void handle_hello(const nlohmann::json& message);
    void handle_proof(const nlohmann::json& message);
    void dispatch_running(const nlohmann::json& message);
    void answer_ota_check(const nlohmann::json& message);

    Result<std::string> encode_and_send(const RequestSpec& spec);
    bool transmit(const nlohmann::json& message);
    void enter(SessionState next);
    void abort(Error error, std::uint16_t close_code, const std::string& reason);
    void warn(std::string reason, std::string type,
              std::optional<std::string> in_reply_to = std::nullopt);
    std::string next_msg_id();

    ControllerSessionConfig config_;
    SendFn send_;
    CloseFn close_;
    NowFn now_;
    Callbacks callbacks_;

    RequestDispatcher dispatcher_;

    SessionState state_ = SessionState::Closed;
    AuthContext auth_{};
    AntiReplay replay_{};
    std::int64_t s2c_seq_ = 0;
    std::int64_t msg_seq_ = 0;
    ConnectionInfo info_{};

    TimePoint handshake_deadline_{};
    TimePoint last_tx_{};
    bool failed_ = false;
};

}  // namespace fountainer::protocol
