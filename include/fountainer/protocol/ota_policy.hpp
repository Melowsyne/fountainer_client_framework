// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// OTA decision in the handshake (design concept §17). After hello_ack the
// device sends a signed ota_check — the controller MUST respond, otherwise
// the session never gets going.
//
// Locally the response is always ota_none: local_protocol.c deliberately
// sets on_ota to NULL, so a locally sent ota_available would not start
// any OTA job at all. A real OTA workflow belongs in the backend.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <variant>

#include <fountainer/connection.hpp>

namespace fountainer::protocol {

struct OtaCheck {
    std::string current_version;
    std::string hw_rev;
};

struct NoUpdate {};

struct OtaAvailable {
    std::string version;
    std::string url;
    std::string sha256;
    std::uint64_t size = 0;
};

using OtaDecision = std::variant<NoUpdate, OtaAvailable>;

class OtaPolicy {
public:
    virtual ~OtaPolicy() = default;
    virtual OtaDecision check(const ConnectionInfo& device, const OtaCheck& request) = 0;
};

// Default for local maintenance connections.
class NoUpdatePolicy final : public OtaPolicy {
public:
    OtaDecision check(const ConnectionInfo&, const OtaCheck&) override
    {
        return NoUpdate{};
    }
};

inline std::shared_ptr<OtaPolicy> no_update_policy()
{
    return std::make_shared<NoUpdatePolicy>();
}

}  // namespace fountainer::protocol
