// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Connection state and device identity (design concept §3.3/§4 G).
//
// Important: WSS-connected is NOT protocol-ready. The firmware only accepts
// dp_write/command/log_* in the Fountain state running (fp_session.c) —
// which is why Client::connect() only counts as successful at ClientState::Ready.
#pragma once

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>

namespace fountainer {

enum class ClientState {
    Disconnected,
    Resolving,
    TcpConnecting,
    TlsHandshaking,
    WebSocketHandshaking,
    FountainHandshaking,
    Ready,
    Reconnecting,
    Closing,
};

std::string_view to_string(ClientState state) noexcept;

// What the device says about itself in the hello, plus the result of the
// authentication negotiation.
struct ConnectionInfo {
    std::string device_id;         // "esp32-a1b2c3d4e5f6"
    std::string serial;            // "000001C0C01FA82A" (envelope field)
    std::string firmware_version;
    std::string hardware_revision;
    std::string boot_reason;

    std::uint8_t protocol_version = 0;
    std::string auth_scheme;       // "hmac-sha256"
    std::string auth_scope;        // "control"
    std::string auth_kid;

    std::chrono::steady_clock::time_point established_at{};
};

}  // namespace fountainer
