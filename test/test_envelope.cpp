// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Envelope layer: wire version from the META table, optional fields,
// rejection of unknown types.
#include "fountainer/protocol/envelope.hpp"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using fountainer::protocol::build_message;
using nlohmann::json;

TEST_CASE("wire version follows the META table")
{
    CHECK(build_message("hello_ack")["v"] == 1);
    CHECK(build_message("ota_none")["v"] == 2);
    CHECK(build_message("dp_read")["v"] == 2);
}

TEST_CASE("body fields land at the top level")
{
    const json msg = build_message("dp_read", {{"names", json::array()}},
                                  "s-1", "", 42);
    CHECK(msg["type"] == "dp_read");
    CHECK(msg["ts"] == 42);
    CHECK(msg["msg_id"] == "s-1");
    CHECK_FALSE(msg.contains("in_reply_to"));
    CHECK(msg["names"] == json::array());
}

TEST_CASE("in_reply_to is set when given, ts defaults to now")
{
    const json msg = build_message("ota_none", json::object(), "", "d-7");
    CHECK(msg["in_reply_to"] == "d-7");
    CHECK_FALSE(msg.contains("msg_id"));
    CHECK(msg["ts"].get<std::int64_t>() > 0);
}

TEST_CASE("unknown message types are rejected")
{
    CHECK_THROWS(build_message("definitely_not_a_type"));
}
