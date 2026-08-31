// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// HMAC auth: the GOLDEN VECTOR (AUTH-CONTRACT.md, identical to
// fountainer_server/tests/test_auth_golden.py and to the ESP32 side) is the
// gate for interoperability — if it fails, either the canonicalization or
// the MAC computation is wrong.
#include "fountainer/protocol/auth.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include "fountainer/protocol/canonical.hpp"

namespace fs = std::filesystem;
using namespace fountainer::protocol;
using nlohmann::json;

namespace {

// Test key of the golden vector (NOT a secret): 000102...1f
std::vector<std::uint8_t> golden_key()
{
    std::vector<std::uint8_t> key(32);
    for (int i = 0; i < 32; i++) {
        key[i] = static_cast<std::uint8_t>(i);
    }
    return key;
}

AuthContext golden_ctx()
{
    return AuthContext{golden_key(), "1", "esp32-a1b2c3d4e5f6",
                       "9f3aK2pL0xQ7sV4nB1dC8g==",
                       "Yt8m1Q5fT3oQh2bJ0a9w7w=="};
}

json golden_msg()
{
    return {{"v", 2}, {"type", "command"}, {"ts", 1718370040000},
            {"msg_id", "cmd-7"}, {"command", "set_state"},
            {"target_state", "On"}};
}

}  // namespace

TEST_CASE("golden vector: canonical body and hash")
{
    const json msg = golden_msg();
    CHECK(canonical_serialize(canonical_body(msg)) ==
          R"({"command":"set_state","target_state":"On"})");
    CHECK(body_hash_hex(msg) ==
          "df69a908821ed289cbaf08d81b4ae7369a6099afb684e1925890053cd255e9e2");
}

TEST_CASE("golden vector: mac")
{
    const json msg = golden_msg();
    const AuthContext ctx = golden_ctx();
    const std::string raw = mac_input(msg, ctx, Direction::S2c, "1", 1);
    CHECK(compute_mac(ctx.key, raw) == "QsNu1LP0C0yOt5Gvftvbzg==");
}

TEST_CASE("sign/verify roundtrip and tamper detection")
{
    const AuthContext ctx = golden_ctx();
    json msg = {{"v", 2}, {"type", "dp_write"}, {"ts", 123},
                {"msg_id", "w-1"},
                {"dp", {{"Fon_Report_Interval", 5}}}};
    sign(msg, ctx, 1, Direction::S2c);
    REQUIRE(msg.contains("auth"));
    CHECK(msg["auth"]["kid"] == "1");
    CHECK(msg["auth"]["seq"] == 1);
    CHECK(verify(msg, ctx, Direction::S2c).ok);

    msg["dp"]["Fon_Report_Interval"] = 999;
    const VerifyResult tampered = verify(msg, ctx, Direction::S2c);
    CHECK_FALSE(tampered.ok);
    CHECK(tampered.reason == "mac_mismatch");
}

TEST_CASE("verify rejects missing auth, kid mismatch, bad seq")
{
    const AuthContext ctx = golden_ctx();
    json msg = golden_msg();
    CHECK(verify(msg, ctx, Direction::S2c).reason == "missing_auth");

    msg["auth"] = {{"kid", "2"}, {"seq", 1}, {"mac", "x"}};
    CHECK(verify(msg, ctx, Direction::S2c).reason == "kid_mismatch");

    msg["auth"] = {{"kid", "1"}, {"seq", "not-a-number"}, {"mac", "x"}};
    CHECK(verify(msg, ctx, Direction::S2c).reason == "bad_seq");
}

TEST_CASE("anti-replay: strictly increasing")
{
    AntiReplay replay;
    CHECK(replay.check(1));
    CHECK(replay.check(2));
    CHECK_FALSE(replay.check(2));   // equal -> replay
    CHECK_FALSE(replay.check(1));   // smaller -> replay
    CHECK(replay.check(3));
    CHECK(replay.last() == 3);
}

TEST_CASE("load_hmac_key_file")
{
    const fs::path dir =
        fs::temp_directory_path() / "fountainer_auth_test";
    fs::create_directories(dir);

    const auto write = [&dir](const char* name, const std::string& content) {
        const auto file = (dir / name).string();
        std::ofstream stream(file);
        stream << content;
        return file;
    };

    const std::string hex =
        "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

    SECTION("valid key with trailing newline")
    {
        CHECK(load_hmac_key_file(write("ok.key", hex + "\n")) == golden_key());
    }
    SECTION("uppercase hex accepted")
    {
        std::string upper = hex;
        for (char& c : upper) {
            c = static_cast<char>(std::toupper(c));
        }
        CHECK(load_hmac_key_file(write("upper.key", upper)) == golden_key());
    }
    SECTION("wrong length rejected")
    {
        CHECK_THROWS(load_hmac_key_file(write("short.key", "001122\n")));
    }
    SECTION("non-hex rejected")
    {
        std::string bad = hex;
        bad[10] = 'g';
        CHECK_THROWS(load_hmac_key_file(write("bad.key", bad)));
    }
    SECTION("missing file rejected")
    {
        CHECK_THROWS(load_hmac_key_file((dir / "missing.key").string()));
    }

    std::error_code ignored;
    fs::remove_all(dir, ignored);
}
