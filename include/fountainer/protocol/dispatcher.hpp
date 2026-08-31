// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// RequestDispatcher (design concept §20). Replaces the former SINGLE global
// request_timer_ of the FountainProtocolHandler with:
//
//   - one deadline PER request,
//   - a set of allowed response types per request,
//   - priorities (a pump-off never waits behind the polling queue),
//   - a rate budget below the firmware limit (5 frames/s, 3 strikes
//     within 10 s kill the session — local_rate_limit.h),
//   - cancellation and clean draining of all requests on disconnect.
//
// Deliberately without Asio: the clock comes in via tick(now), so that the
// complete logic is testable with a fake clock.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include <fountainer/result.hpp>

namespace fountainer::protocol {

using Clock = std::chrono::steady_clock;
using TimePoint = Clock::time_point;

// Smaller value = more important.
enum class OperationPriority : std::uint8_t {
    SafetyControl = 0,      // set_state(Off), emergency stop
    InteractiveControl,     // dp_write, command by the user
    InteractiveRead,        // read() by the user
    LogTransfer,            // log_read/log_read_prev
    Polling,                // DatapointPoller
    Background,             // Keepalive, Resync
};

std::string_view to_string(OperationPriority priority) noexcept;

struct RequestSpec {
    std::string type;                            // "dp_read", "dp_write", ...
    nlohmann::json body = nlohmann::json::object();

    // Total budget from submit(); nullopt = the dispatcher's default
    // (ClientOptions::default_request_timeout).
    std::optional<std::chrono::milliseconds> timeout;
    OperationPriority priority = OperationPriority::InteractiveRead;

    // Can be merged with an already waiting request with the same key
    // (poller: do not request the same datapoints twice).
    std::string coalesce_key;
};

// Success = response received (including "rejected"!). Error = timeout,
// disconnect, transport error, unexpected response type.
using ResponseHandler = std::function<void(Result<nlohmann::json>)>;

// The poller must not occupy the entire firmware capacity — headroom must
// remain for writes, commands, log pull and retries (design concept §13.5).
struct RateBudget {
    double requests_per_second = 4.0;
    double burst = 8.0;
    std::size_t max_in_flight = 4;
};

struct DispatcherMetrics {
    std::uint64_t submitted = 0;
    std::uint64_t sent = 0;
    std::uint64_t completed = 0;
    std::uint64_t timeouts = 0;
    std::uint64_t send_failures = 0;
    std::uint64_t disconnect_failures = 0;
    std::uint64_t coalesced = 0;
    std::uint64_t rate_limit_deferrals = 0;
    std::uint64_t unexpected_responses = 0;
    std::size_t in_flight = 0;
    std::size_t queued = 0;
};

class RequestDispatcher {
public:
    // Encodes (envelope, msg_id, signature if needed) and sends; returns the
    // msg_id or an error. Provided by the ControllerSession.
    using EncodeAndSend = std::function<Result<std::string>(const RequestSpec&)>;
    using WarningHandler = std::function<void(std::string reason, std::string type)>;

    explicit RequestDispatcher(
        EncodeAndSend send, RateBudget budget = {},
        std::chrono::milliseconds default_timeout = std::chrono::milliseconds{10000});

    void set_warning_handler(WarningHandler handler);
    void set_budget(RateBudget budget) { budget_ = budget; }
    void set_default_timeout(std::chrono::milliseconds timeout)
    {
        default_timeout_ = timeout;
    }

    // The handler is ALWAYS called exactly once — even on timeout,
    // disconnect or cancel.
    void submit(RequestSpec spec, ResponseHandler handler, TimePoint now);

    // true if the message was consumed as the response to an open request.
    // false ⇒ spontaneous event, belongs to the session.
    bool on_message(const nlohmann::json& message);

    // Check deadlines and send waiting requests within the budget.
    void tick(TimePoint now);

    // Connection lost: fail all open and waiting requests.
    void fail_all(const Error& error);

    // Blocks new transmissions (e.g. during the handshake).
    void set_sending_enabled(bool enabled) { sending_enabled_ = enabled; }

    [[nodiscard]] DispatcherMetrics metrics() const;
    [[nodiscard]] std::size_t in_flight() const noexcept { return pending_.size(); }
    [[nodiscard]] std::size_t queued() const noexcept { return queue_.size(); }

private:
    struct Waiting {
        RequestSpec spec;
        std::vector<ResponseHandler> handlers;   // >1 after coalescing
        TimePoint deadline;
        std::uint64_t sequence;                  // FIFO within one priority
    };

    struct Pending {
        std::string type;
        std::vector<ResponseHandler> handlers;
        TimePoint deadline;
    };

    void refill(TimePoint now);
    void drain(TimePoint now);
    static void complete(std::vector<ResponseHandler>& handlers,
                         const Result<nlohmann::json>& result);

    EncodeAndSend send_;
    RateBudget budget_;
    std::chrono::milliseconds default_timeout_;
    WarningHandler warning_;

    std::vector<Waiting> queue_;                 // small; strictly priority-
                                                 // sorted when dequeuing
    std::map<std::string, Pending> pending_;     // msg_id -> open request
    std::uint64_t sequence_ = 0;

    double tokens_ = 0.0;
    TimePoint last_refill_{};
    bool refill_initialised_ = false;
    bool sending_enabled_ = true;
    bool draining_ = false;

    DispatcherMetrics metrics_{};
};

}  // namespace fountainer::protocol
