// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/protocol/envelope.hpp"

#include <chrono>
#include <stdexcept>

#include "fountainer/protocol/fountain_messages.hpp"

namespace fountainer::protocol {

std::int64_t now_ms()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

nlohmann::json build_message(std::string_view name, const nlohmann::json& body,
                             const std::string& msg_id,
                             const std::string& in_reply_to, std::int64_t ts)
{
    const MessageMeta* meta = find_meta(name);
    if (meta == nullptr) {
        throw std::runtime_error("unknown message type: " + std::string(name));
    }
    nlohmann::json msg = {{"v", meta->wire}, {"type", std::string(name)},
                          {"ts", ts >= 0 ? ts : now_ms()}};
    if (!msg_id.empty()) {
        msg["msg_id"] = msg_id;
    }
    if (!in_reply_to.empty()) {
        msg["in_reply_to"] = in_reply_to;
    }
    if (body.is_object()) {
        for (auto it = body.begin(); it != body.end(); ++it) {
            msg[it.key()] = it.value();
        }
    }
    return msg;
}

}  // namespace fountainer::protocol
