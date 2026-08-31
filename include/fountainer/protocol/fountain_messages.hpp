// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Fountain v2.2 message table — HAND TRANSCRIPTION of the normative
// server table fountain_proto/_messages.py (META, lines 46-76). Deliberately
// no include of firmware headers and no generated code: this project
// keeps the specification as its own clean constexpr table.
// When the protocol changes, reconcile with the server table FIRST.
//
// Addition compared to the server META: the SET of allowed responses.
// A single response field is not enough, because ota_check may semantically
// be answered with ota_none OR ota_available (design concept §3.5/§4 D).
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fountainer::protocol {

inline constexpr int kWireVersion = 2;
inline constexpr int kHandshakeVersion = 1;

// Hard receive limit of the firmware for ONE text frame
// (LOCAL_MAX_FRAME_SIZE, local_buffer_pool.h) — larger frames terminate the
// session. Part of the wire contract, hence here and not in the transport.
inline constexpr std::size_t kMaxControlFrameBytes = 4096;
inline constexpr std::string_view kAuthScheme = "hmac-sha256";
inline constexpr std::string_view kAuthScope = "control";
inline constexpr std::string_view kSessionProofMsg = "ota_check";

// Direction as seen by the Fountain protocol: c2s = device -> Fountain server
// (i.e. device -> THIS program; role inversion, design concept §3.1).
enum class Direction { C2s, S2c };

enum class AuthLevel { None, Session, Control };

struct MessageMeta {
    std::string_view name;
    Direction direction;
    int wire;                     // 1 = handshake family, otherwise 2
    AuthLevel auth;
    bool request;
    std::array<std::string_view, 2> responses;   // allowed response types
    std::uint8_t response_count;

    [[nodiscard]] constexpr bool accepts_response(std::string_view type) const
    {
        for (std::uint8_t i = 0; i < response_count; ++i) {
            if (responses[i] == type) return true;
        }
        return false;
    }
};

inline constexpr std::array<MessageMeta, 22> kMessages = {{
    {"hello",             Direction::C2s, 1, AuthLevel::None,    true,  {"hello_ack", ""},                 1},
    {"hello_ack",         Direction::S2c, 1, AuthLevel::None,    false, {"", ""},                          0},
    {"protocol_mismatch", Direction::C2s, 1, AuthLevel::None,    false, {"", ""},                          0},
    {"ota_check",         Direction::C2s, 2, AuthLevel::Session, true,  {"ota_none", "ota_available"},     2},
    {"ota_available",     Direction::S2c, 2, AuthLevel::Control, false, {"ota_status", ""},                1},
    {"ota_none",          Direction::S2c, 2, AuthLevel::None,    false, {"", ""},                          0},
    {"ota_cancel",        Direction::S2c, 2, AuthLevel::Control, false, {"ota_status", ""},                1},
    {"ota_status",        Direction::C2s, 2, AuthLevel::None,    false, {"", ""},                          0},
    {"heartbeat",         Direction::C2s, 2, AuthLevel::None,    false, {"", ""},                          0},
    {"dp_report",         Direction::C2s, 2, AuthLevel::None,    false, {"", ""},                          0},
    {"dp_read",           Direction::S2c, 2, AuthLevel::None,    true,  {"dp_report", ""},                 1},
    {"dp_write",          Direction::S2c, 2, AuthLevel::Control, true,  {"dp_write_result", ""},           1},
    {"dp_write_result",   Direction::C2s, 2, AuthLevel::None,    false, {"", ""},                          0},
    {"command",           Direction::S2c, 2, AuthLevel::Control, true,  {"command_result", ""},            1},
    {"command_result",    Direction::C2s, 2, AuthLevel::None,    false, {"", ""},                          0},
    {"error_report",      Direction::C2s, 2, AuthLevel::None,    false, {"", ""},                          0},
    {"device_alert",      Direction::C2s, 2, AuthLevel::None,    false, {"", ""},                          0},
    {"log_read",          Direction::S2c, 2, AuthLevel::Control, true,  {"log_batch", ""},                 1},
    {"log_batch",         Direction::C2s, 2, AuthLevel::None,    false, {"", ""},                          0},
    {"log_read_prev",     Direction::S2c, 2, AuthLevel::Control, true,  {"log_batch", ""},                 1},
    {"log_ack_prev",      Direction::S2c, 2, AuthLevel::Control, true,  {"log_ack_result", ""},            1},
    {"log_ack_result",    Direction::C2s, 2, AuthLevel::None,    false, {"", ""},                          0},
}};

// nullptr if the type is unknown (unknown types are ignored during
// operation — forward compatibility).
constexpr const MessageMeta* find_meta(std::string_view name)
{
    for (const auto& meta : kMessages) {
        if (meta.name == name) {
            return &meta;
        }
    }
    return nullptr;
}

}  // namespace fountainer::protocol
