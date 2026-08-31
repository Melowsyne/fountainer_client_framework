// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Runtime options of the client (design concept §8/§21).
#pragma once

#include <chrono>

#include <fountainer/protocol/dispatcher.hpp>

namespace fountainer {

struct ReconnectPolicy {
    bool enabled = true;
    std::chrono::seconds initial_delay{1};
    std::chrono::seconds max_delay{60};
    // Authentication failures get a long, fixed delay — otherwise the client
    // would fill up the device's auth-failure strikes.
    std::chrono::seconds authentication_delay{300};
    bool jitter = true;
};

struct ClientOptions {
    std::chrono::seconds connect_timeout{10};

    // WS transport keepalive (half-open detection); the Fountain keepalive
    // against the device's 300 s idle timeout is the poller's responsibility.
    std::chrono::seconds transport_idle_timeout{60};

    std::chrono::milliseconds handshake_timeout{15000};
    std::chrono::milliseconds default_request_timeout{10000};

    ReconnectPolicy reconnect{};
    protocol::RateBudget rate_budget{};

    // Enables wd_fault/link_fault in the DiagnosticsService (§15.2).
    bool enable_test_commands = false;
};

}  // namespace fountainer
