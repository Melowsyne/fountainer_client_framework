// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Addressing of the device. Deliberately a pure value type — choosing the
// endpoint is the application's business; the framework no longer derives
// anything security-relevant from it (see EndpointIdentityPolicy in security.hpp).
#pragma once

#include <cstdint>
#include <string>

namespace fountainer {

// Local maintenance access of the firmware: wss://<ip>:4443/ws, subprotocol
// "fountain", mTLS mandatory (local_server.h).
inline constexpr std::uint16_t kDefaultLocalPort = 4443;
inline constexpr const char* kDefaultPath = "/ws";
inline constexpr const char* kSubprotocol = "fountain";

struct Endpoint {
    std::string host;                        // IP literal OR domain name
    std::uint16_t port = kDefaultLocalPort;
    std::string path = kDefaultPath;
    std::string subprotocol = kSubprotocol;

    [[nodiscard]] std::string to_string() const
    {
        return "wss://" + host + ":" + std::to_string(port) + path;
    }
};

}  // namespace fountainer
