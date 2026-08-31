// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Envelope layer — mirror of fountain_proto/envelope.py: wraps a body
// into a complete wire message (envelope fields + body fields at the
// top JSON level; "auth" is set later by auth::sign()).
#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace fountainer::protocol {

std::int64_t now_ms();

// Throws std::runtime_error for an unknown message type ("v" comes from the
// META table). msg_id/in_reply_to empty = omit the field; ts < 0 = now_ms().
nlohmann::json build_message(std::string_view name,
                             const nlohmann::json& body = nlohmann::json::object(),
                             const std::string& msg_id = "",
                             const std::string& in_reply_to = "",
                             std::int64_t ts = -1);

}  // namespace fountainer::protocol
