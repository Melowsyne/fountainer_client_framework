// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// DatapointPoller (design concept §13): ONE scheduler instead of 107 timers.
//
//   - due datapoints are collected per tick and bundled into a few
//     dp_read requests (coalescing),
//   - requests run with priority Polling — user actions overtake them,
//   - the dispatcher's rate budget stays within the firmware limit (5 frames/s);
//     the poller does not pile anything up (in-flight ⇒ skip, §13.7),
//   - deadlines come from the planned time grid; missed cycles are
//     skipped, not caught up (§13.8),
//   - if nothing has been sent for a while, the poller generates a
//     Fountain keepalive (dp_read System_Uptime), because the firmware only
//     counts RECEIVED text frames against its 300 s idle timeout (§21.1).
//
// Deliberately without its own timer: the client calls tick(now) on the IO thread.
#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <fountainer/datapoints/manager.hpp>

namespace fountainer {

struct PollStats {
    std::uint64_t cycles = 0;             // ticks with at least one request
    std::uint64_t requests = 0;
    std::uint64_t coalesced_points = 0;   // points that share a request
    std::uint64_t skipped_inflight = 0;
    std::uint64_t missed_deadlines = 0;   // skipped cycles
    std::uint64_t keepalives = 0;
    std::uint64_t failures = 0;
};

class DatapointPoller {
public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    explicit DatapointPoller(DatapointManager& manager);
    ~DatapointPoller();
    DatapointPoller(const DatapointPoller&) = delete;
    DatapointPoller& operator=(const DatapointPoller&) = delete;

    // ------------------------------------------------------------------
    // Configuration (allowed before or after start())
    // ------------------------------------------------------------------

    template <ReadableDatapoint... DP>
    void every(std::chrono::milliseconds interval, const DP&... datapoints)
    {
        (add(std::string(datapoints.name), interval), ...);
    }

    Status every(std::chrono::milliseconds interval,
                 const std::vector<std::string>& names);

    // Once after start() or after each reconnect resync.
    template <ReadableDatapoint... DP>
    void once(const DP&... datapoints)
    {
        (add_once(std::string(datapoints.name)), ...);
    }

    template <AnyDatapoint... DP>
    void disable(const DP&... datapoints)
    {
        (remove(std::string(datapoints.name)), ...);
    }

    // Activate the poll-class defaults from the generated catalog
    // (Realtime 1 s, Status 5 s, Config 60 s, OnConnect once; §13.3).
    void start_defaults();

    void set_class_interval(PollClass poll_class, std::chrono::milliseconds interval);

    // Fountain keepalive interval (0 = off). Default 240 s — below the
    // firmware's 300 s idle timeout.
    void set_keepalive(std::chrono::seconds interval);

    // ------------------------------------------------------------------
    // Lifecycle
    // ------------------------------------------------------------------

    void start();
    void pause();
    void resume();
    void stop();     // stops; does not remove the once markers

    [[nodiscard]] bool running() const noexcept;

    // After a reconnect: rebuild the grid, read the OnConnect points again.
    void on_reconnected();

    [[nodiscard]] PollStats stats() const;

    // Driven by the client on the IO thread. last_tx = last frame sent by
    // the session (for the keepalive decision).
    void tick(TimePoint now, TimePoint last_tx);

    // Next relevant point in time (for the client's timer programming);
    // TimePoint::max() when nothing is pending.
    [[nodiscard]] TimePoint next_deadline(TimePoint now) const;

private:
    void add(std::string name, std::chrono::milliseconds interval);
    void add_once(std::string name);
    void remove(const std::string& name);

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace fountainer
