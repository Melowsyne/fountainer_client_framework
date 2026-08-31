// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Spontaneous device events as domain types (design concept §18). Logging is
// observability — heartbeat/device_alert/ota_status/error_report belong in
// the API, not just in a log line.
//
// Delivery rule: callbacks NEVER run under an internal mutex; the client
// invokes them on its IO thread or on the configured callback executor.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include <fountainer/connection.hpp>
#include <fountainer/error.hpp>

namespace fountainer {

struct Heartbeat {
    std::uint32_t uptime_s = 0;
    std::string firmware_version;
    bool fault_active = false;
    std::chrono::steady_clock::time_point received_at{};
};

struct DeviceAlert {
    std::string code;
    std::string severity;
    std::string datapoint;
    std::optional<float> value;
    std::optional<float> threshold;
    std::string detail;
    std::chrono::steady_clock::time_point received_at{};
};

struct OtaStatus {
    std::string target_version;
    std::string state;
    std::uint8_t attempt = 0;
    std::uint8_t progress_pct = 0;
    std::string error;
    std::string error_detail;
};

struct ErrorReport {
    std::string status;
    nlohmann::json active_faults;   // array
    nlohmann::json log;             // array
};

// State change of the client, including the cause on connection loss.
struct ConnectionStateChange {
    ClientState previous = ClientState::Disconnected;
    ClientState current = ClientState::Disconnected;
    std::optional<ConnectionInfo> info;   // set on the transition to Ready
    std::optional<Error> cause;           // set on loss/error
};

// Non-fatal protocol notice: discarded message, unknown type,
// unexpected response type. For diagnostics, not for control flow.
struct ProtocolWarning {
    std::string reason;
    std::string message_type;
    std::optional<std::string> in_reply_to;
};

// RAII unsubscription. The destructor unsubscribes; copying is not possible.
class Subscription {
public:
    using Unsubscribe = std::function<void()>;

    Subscription() = default;
    explicit Subscription(Unsubscribe unsubscribe)
        : unsubscribe_(std::move(unsubscribe))
    {
    }
    ~Subscription() { reset(); }

    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept
        : unsubscribe_(std::exchange(other.unsubscribe_, nullptr))
    {
    }
    Subscription& operator=(Subscription&& other) noexcept
    {
        if (this != &other) {
            reset();
            unsubscribe_ = std::exchange(other.unsubscribe_, nullptr);
        }
        return *this;
    }

    void reset()
    {
        if (auto unsubscribe = std::exchange(unsubscribe_, nullptr)) unsubscribe();
    }

    [[nodiscard]] bool active() const noexcept { return unsubscribe_ != nullptr; }

private:
    Unsubscribe unsubscribe_;
};

// Typed event distribution. All handlers run on the client's callback
// executor.
class EventBus {
public:
    EventBus();
    ~EventBus();
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;

    Subscription on_connection_state(std::function<void(const ConnectionStateChange&)>);
    Subscription on_heartbeat(std::function<void(const Heartbeat&)>);
    Subscription on_device_alert(std::function<void(const DeviceAlert&)>);
    Subscription on_ota_status(std::function<void(const OtaStatus&)>);
    Subscription on_error_report(std::function<void(const ErrorReport&)>);
    Subscription on_protocol_warning(std::function<void(const ProtocolWarning&)>);

    // Raw escape hatch for future/unknown message types — instead of
    // dropping the connection (design concept §18).
    Subscription on_unknown_message(
        std::function<void(std::string_view, const nlohmann::json&)>);

    // --- used by the client implementation ---
    void publish(const ConnectionStateChange& event);
    void publish(const Heartbeat& event);
    void publish(const DeviceAlert& event);
    void publish(const OtaStatus& event);
    void publish(const ErrorReport& event);
    void publish(const ProtocolWarning& event);
    void publish_unknown(std::string_view type, const nlohmann::json& message);

private:
    struct Impl;
    // shared + weak in the subscription tokens: a token that outlives the bus
    // unsubscribes as a no-op on destruction (no dangling).
    std::shared_ptr<Impl> impl_;
};

}  // namespace fountainer
