// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Canonical serialization: must be byte-identical to Python
// json.dumps(sort_keys=True, separators=(",",":"), ensure_ascii=False)
// (AUTH-CONTRACT) — including the cJSON rule "integral floats without .0".
#include "fountainer/protocol/canonical.hpp"

#include <limits>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

using fountainer::protocol::canonical_number;
using fountainer::protocol::canonical_serialize;
using fountainer::protocol::escape_json_string;
using nlohmann::json;

TEST_CASE("golden body serializes compact and sorted")
{
    const json body = {{"target_state", "On"}, {"command", "set_state"}};
    CHECK(canonical_serialize(body) ==
          R"({"command":"set_state","target_state":"On"})");
}

TEST_CASE("keys are sorted recursively, arrays keep order")
{
    const json value = json::parse(
        R"({"b":{"z":1,"a":2},"a":[3,1,{"y":0,"x":0}]})");
    CHECK(canonical_serialize(value) ==
          R"({"a":[3,1,{"x":0,"y":0}],"b":{"a":2,"z":1}})");
}

TEST_CASE("integral doubles print as integers (cJSON rule)")
{
    CHECK(canonical_serialize(json::parse("10.0")) == "10");
    CHECK(canonical_serialize(json::parse("-3.0")) == "-3");
    CHECK(canonical_serialize(json::parse("0.0")) == "0");
    CHECK(canonical_serialize(json::parse("10")) == "10");
}

TEST_CASE("floats match Python repr (shortest round-trip)")
{
    CHECK(canonical_number(0.1) == "0.1");
    CHECK(canonical_number(23.5) == "23.5");
    CHECK(canonical_number(-0.5) == "-0.5");
    CHECK(canonical_number(1e22) == "1e+22");
    CHECK(canonical_number(1e-5) == "1e-05");
    CHECK(canonical_number(2.5e-10) == "2.5e-10");
    // Round-trip precision: needs 17 significant digits
    CHECK(canonical_number(0.30000000000000004) == "0.30000000000000004");
}

TEST_CASE("non-finite numbers are rejected")
{
    CHECK_THROWS(canonical_number(
        std::numeric_limits<double>::infinity()));
}

TEST_CASE("string escaping is minimal (ensure_ascii=False)")
{
    CHECK(escape_json_string("a\"b\\c") == R"(a\"b\\c)");
    CHECK(escape_json_string("tab\tnl\ncr\r") == R"(tab\tnl\ncr\r)");
    CHECK(escape_json_string(std::string_view("\x01", 1)) == "\\u0001");
    // UTF-8 stays unchanged (no \uXXXX escape)
    CHECK(escape_json_string("gr\xc3\xbc\xc3\x9f") == "gr\xc3\xbc\xc3\x9f");
}

TEST_CASE("empty body and scalars")
{
    CHECK(canonical_serialize(json::object()) == "{}");
    CHECK(canonical_serialize(json::array()) == "[]");
    CHECK(canonical_serialize(json(true)) == "true");
    CHECK(canonical_serialize(json(nullptr)) == "null");
    CHECK(canonical_serialize(json("x")) == "\"x\"");
}
