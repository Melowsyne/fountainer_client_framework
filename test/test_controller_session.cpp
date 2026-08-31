// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// ControllerSession: complete handshake (hello -> hello_ack -> signed
// ota_check proof -> ota_none -> RUNNING), signing of control messages,
// verify/anti-replay during operation, error paths. Replaces the tests of the
// old FountainSession; the golden vector still lives in test_auth.cpp.
#include <catch2/catch_test_macros.hpp>

// Designated initializers with default members are intentional here.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include <fountainer/protocol/envelope.hpp>
#include <fountainer/protocol/session.hpp>

using namespace fountainer;
using namespace fountainer::protocol;
using nlohmann::json;

namespace {

struct Harness {
    std::vector<json> sent;
    std::vector<std::pair<std::uint16_t, std::string>> closes;
    std::optional<ConnectionInfo> ready;
    std::optional<Error> failed;
    std::vector<json> reports;
    std::vector<ProtocolWarning> warnings;

    HmacKey key{std::vector<std::uint8_t>(32, 0x42)};
    TimePoint now = Clock::now();
    ControllerSession session;

    // Device side (mirror): signs c2s with the same nonces.
    AuthContext device_ctx;
    std::int64_t device_seq = 0;

    explicit Harness(std::string expected_device = "esp32-test")
        : session(make_config(std::move(expected_device)),
                  [this](std::string frame) {
                      sent.push_back(json::parse(frame));
                      return true;
                  },
                  [this](std::uint16_t code, std::string reason) {
                      closes.emplace_back(code, std::move(reason));
                  },
                  [this] { return now; })
    {
        ControllerSession::Callbacks callbacks;
        callbacks.on_ready = [this](const ConnectionInfo& info) { ready = info; };
        callbacks.on_failed = [this](Error error) { failed = std::move(error); };
        callbacks.on_dp_report = [this](const json& dp, std::optional<std::uint32_t>) {
            reports.push_back(dp);
        };
        callbacks.on_warning = [this](const ProtocolWarning& warning) {
            warnings.push_back(warning);
        };
        session.set_callbacks(std::move(callbacks));
    }

    ControllerSessionConfig make_config(std::string expected_device)
    {
        ControllerSessionConfig config;
        config.credentials = std::make_shared<FixedCredentialProvider>(
            "1", HmacKey{std::vector<std::uint8_t>(32, 0x42)});
        config.kid = "1";
        config.expected_device_id = std::move(expected_device);
        return config;
    }

    json device_hello(const std::string& device_id = "esp32-test")
    {
        return build_message("hello",
                             {{"device_id", device_id},
                              {"protocol_version", 2},
                              {"fw_version", "9.9.9"},
                              {"hw_rev", "0.10.0"},
                              {"boot_reason", "power_on"},
                              {"auth_schemes", {"hmac-sha256"}},
                              {"auth_kids", {"1"}},
                              {"client_nonce", "Q0xJRU5UX05PTkNFXzEyMzQ1Ng=="}},
                             "hello-1", "", 1000);
    }

    // Feed in the hello and set up the device auth from the hello_ack.
    void run_hello()
    {
        session.on_transport_open();
        session.on_text(device_hello().dump());
        REQUIRE(!sent.empty());
        const json& ack = sent.back();
        REQUIRE(ack["type"] == "hello_ack");
        REQUIRE(ack["accepted"] == true);
        device_ctx = AuthContext{std::vector<std::uint8_t>(32, 0x42), "1",
                                 "esp32-test",
                                 ack["server_nonce"].get<std::string>(),
                                 "Q0xJRU5UX05PTkNFXzEyMzQ1Ng=="};
    }

    json signed_device_message(std::string_view type, const json& body,
                               const std::string& msg_id)
    {
        json message = build_message(type, body, msg_id, "", 2000);
        sign(message, device_ctx, ++device_seq, Direction::C2s);
        return message;
    }

    void run_to_running()
    {
        run_hello();
        const auto frames_before = sent.size();
        session.on_text(signed_device_message("ota_check",
                                              {{"current_version", "9.9.9"}},
                                              "chk-1")
                            .dump());
        REQUIRE(sent.size() == frames_before + 1);
        CHECK(sent.back()["type"] == "ota_none");
        CHECK(sent.back()["in_reply_to"] == "chk-1");
        REQUIRE(session.running());
        REQUIRE(ready.has_value());
    }
};

}  // namespace

TEST_CASE("handshake reaches RUNNING and reports ConnectionInfo")
{
    Harness h;
    h.run_to_running();

    CHECK(h.ready->device_id == "esp32-test");
    CHECK(h.ready->firmware_version == "9.9.9");
    CHECK(h.ready->hardware_revision == "0.10.0");
    CHECK(h.ready->protocol_version == 2);
    CHECK(h.ready->auth_kid == "1");
    CHECK(h.ready->auth_scheme == "hmac-sha256");
}

TEST_CASE("first frame other than hello closes with 4000")
{
    Harness h;
    h.session.on_transport_open();
    h.session.on_text(R"({"type":"dp_report","v":2})");
    REQUIRE(!h.closes.empty());
    CHECK(h.closes.back().first == 4000);
    REQUIRE(h.failed.has_value());
    CHECK(h.failed->code == ErrorCode::UnexpectedMessage);
}

TEST_CASE("foreign device identity is rejected with 4004")
{
    Harness h("fnt-000001");
    h.session.on_transport_open();
    h.session.on_text(h.device_hello("esp32-test").dump());
    REQUIRE(!h.closes.empty());
    CHECK(h.closes.back().first == 4004);
    CHECK(h.sent.back()["accepted"] == false);
    CHECK(h.failed->domain == ErrorDomain::Authentication);
}

TEST_CASE("bad proof closes with 4004")
{
    Harness h;
    h.run_hello();
    json proof = h.signed_device_message("ota_check", {}, "chk-1");
    proof["auth"]["mac"] = "AAAAAAAAAAAAAAAAAAAAAA==";
    h.session.on_text(proof.dump());
    REQUIRE(!h.closes.empty());
    CHECK(h.closes.back().first == 4004);
    CHECK_FALSE(h.session.running());
}

TEST_CASE("control requests are signed, dp_read is not")
{
    Harness h;
    h.run_to_running();
    h.sent.clear();

    h.session.request({.type = "dp_read", .body = {{"names", {"System_Uptime"}}}},
                      [](auto) {});
    REQUIRE(h.sent.size() == 1);
    CHECK_FALSE(h.sent[0].contains("auth"));

    h.session.request({.type = "dp_write", .body = {{"dp", {{"Fon_Event_Label", 1}}}}},
                      [](auto) {});
    REQUIRE(h.sent.size() == 2);
    REQUIRE(h.sent[1].contains("auth"));
    CHECK(h.sent[1]["auth"]["kid"] == "1");
    CHECK(h.sent[1]["auth"]["seq"] == 1);   // own s2c counter
}

TEST_CASE("responses correlate via in_reply_to")
{
    Harness h;
    h.run_to_running();
    h.sent.clear();

    std::optional<Result<json>> outcome;
    h.session.request({.type = "dp_read", .body = {{"names", {"System_Uptime"}}}},
                      [&](Result<json> r) { outcome = std::move(r); });
    const std::string msg_id = h.sent[0]["msg_id"];

    h.session.on_text(json{{"v", 2},
                           {"type", "dp_report"},
                           {"in_reply_to", msg_id},
                           {"dp", {{"System_Uptime", 123}}}}
                          .dump());
    REQUIRE(outcome.has_value());
    REQUIRE(outcome->has_value());
    CHECK((**outcome)["dp"]["System_Uptime"] == 123);
    CHECK(h.reports.empty());   // responses do NOT go through the report path
}

TEST_CASE("unsolicited dp_report reaches the report callback")
{
    Harness h;
    h.run_to_running();
    h.session.on_text(
        json{{"v", 2}, {"type", "dp_report"}, {"dp", {{"System_RSSI", -40}}}}.dump());
    REQUIRE(h.reports.size() == 1);
    CHECK(h.reports[0]["System_RSSI"] == -40);
}

TEST_CASE("invalid signature in RUNNING drops the frame but keeps the session")
{
    Harness h;
    h.run_to_running();

    json bogus = h.signed_device_message("dp_report", {{"dp", json::object()}}, "x-1");
    bogus["auth"]["mac"] = "AAAAAAAAAAAAAAAAAAAAAA==";
    h.session.on_text(bogus.dump());
    CHECK(h.session.running());
    CHECK(!h.warnings.empty());
    CHECK(h.reports.empty());

    // Replay: the same sequence number once more.
    json first = h.signed_device_message("dp_report", {{"dp", json::object()}}, "x-2");
    h.session.on_text(first.dump());
    h.session.on_text(first.dump());
    CHECK(h.reports.size() == 1);
}

TEST_CASE("handshake deadline aborts the session")
{
    Harness h;
    h.session.on_transport_open();
    h.now += std::chrono::seconds(20);
    h.session.tick(h.now);
    REQUIRE(h.failed.has_value());
    CHECK(h.failed->code == ErrorCode::HandshakeFailed);
    CHECK(h.closes.back().first == 4000);
}

TEST_CASE("unknown message types are tolerated")
{
    Harness h;
    h.run_to_running();
    h.session.on_text(R"({"v":9,"type":"totally_new_thing","x":1})");
    CHECK(h.session.running());
}

TEST_CASE("transport loss fails pending requests with Disconnected")
{
    Harness h;
    h.run_to_running();

    std::optional<Result<json>> outcome;
    h.session.request({.type = "dp_read", .body = {{"names", {"System_Uptime"}}}},
                      [&](Result<json> r) { outcome = std::move(r); });
    h.session.on_transport_closed(disconnected_error("transport"));
    REQUIRE(outcome.has_value());
    CHECK(outcome->error().code == ErrorCode::Disconnected);
    CHECK_FALSE(h.session.running());
}
