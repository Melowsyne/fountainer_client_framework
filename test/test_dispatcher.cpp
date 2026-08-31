// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// RequestDispatcher: per-request deadlines, permitted response types,
// priorities, coalescing, rate budget — all with a fake clock.
#include <catch2/catch_test_macros.hpp>

// Designated initializers with default members are intentional here.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include <fountainer/protocol/dispatcher.hpp>
#include <fountainer/protocol/fountain_messages.hpp>

using namespace fountainer;
using namespace fountainer::protocol;
using namespace std::chrono_literals;

namespace {

struct Harness {
    std::vector<RequestSpec> sent;
    bool send_ok = true;
    int counter = 0;
    RequestDispatcher dispatcher;
    TimePoint now = Clock::now();

    explicit Harness(RateBudget budget = {})
        : dispatcher(
              [this](const RequestSpec& spec) -> Result<std::string> {
                  if (!send_ok) {
                      return fail(make_error(ErrorDomain::Disconnected,
                                             ErrorCode::TransportClosed, "down"));
                  }
                  sent.push_back(spec);
                  return "m-" + std::to_string(++counter);
              },
              budget)
    {
    }

    void advance(std::chrono::milliseconds delta)
    {
        now += delta;
        dispatcher.tick(now);
    }
};

nlohmann::json reply(const std::string& msg_id, const std::string& type)
{
    return {{"type", type}, {"in_reply_to", msg_id}};
}

}  // namespace

TEST_CASE("request completes with the correlated response")
{
    Harness h;
    std::optional<Result<nlohmann::json>> outcome;

    h.dispatcher.submit({.type = "dp_read", .body = {{"names", {"System_Uptime"}}}},
                        [&](Result<nlohmann::json> r) { outcome = std::move(r); },
                        h.now);
    REQUIRE(h.sent.size() == 1);
    CHECK_FALSE(outcome.has_value());

    CHECK(h.dispatcher.on_message(reply("m-1", "dp_report")));
    REQUIRE(outcome.has_value());
    CHECK(outcome->has_value());
}

TEST_CASE("unexpected response type fails the request")
{
    Harness h;
    std::optional<Result<nlohmann::json>> outcome;

    h.dispatcher.submit({.type = "dp_write"},
                        [&](Result<nlohmann::json> r) { outcome = std::move(r); },
                        h.now);
    CHECK(h.dispatcher.on_message(reply("m-1", "command_result")));
    REQUIRE(outcome.has_value());
    REQUIRE_FALSE(outcome->has_value());
    CHECK(outcome->error().code == ErrorCode::UnexpectedResponseType);
}

TEST_CASE("ota_check accepts both ota_none and ota_available")
{
    const auto* meta = find_meta("ota_check");
    REQUIRE(meta != nullptr);
    CHECK(meta->accepts_response("ota_none"));
    CHECK(meta->accepts_response("ota_available"));
    CHECK_FALSE(meta->accepts_response("dp_report"));
}

TEST_CASE("per-request deadline produces a typed timeout")
{
    Harness h;
    std::optional<Result<nlohmann::json>> slow, fast;

    h.dispatcher.submit({.type = "dp_read", .timeout = 5000ms},
                        [&](Result<nlohmann::json> r) { slow = std::move(r); },
                        h.now);
    h.dispatcher.submit({.type = "command", .timeout = 1000ms},
                        [&](Result<nlohmann::json> r) { fast = std::move(r); },
                        h.now);

    h.advance(1500ms);
    REQUIRE(fast.has_value());        // only the short request expired
    CHECK_FALSE(fast->has_value());
    CHECK(fast->error().code == ErrorCode::RequestTimeout);
    CHECK_FALSE(slow.has_value());

    h.advance(4000ms);
    REQUIRE(slow.has_value());
    CHECK(slow->error().code == ErrorCode::RequestTimeout);
}

TEST_CASE("disconnect fails all requests with Disconnected")
{
    Harness h;
    std::vector<ErrorCode> seen;

    for (int i = 0; i < 3; i++) {
        h.dispatcher.submit({.type = "dp_read"},
                            [&](Result<nlohmann::json> r) {
                                REQUIRE_FALSE(r.has_value());
                                seen.push_back(r.error().code);
                            },
                            h.now);
    }
    h.dispatcher.fail_all(disconnected_error("dp_read"));
    CHECK(seen == std::vector<ErrorCode>{ErrorCode::Disconnected,
                                         ErrorCode::Disconnected,
                                         ErrorCode::Disconnected});
}

TEST_CASE("priority order: safety overtakes polling")
{
    // Budget of 1 in flight so that the queue becomes visible.
    Harness h(RateBudget{.requests_per_second = 100, .burst = 100,
                         .max_in_flight = 1});

    h.dispatcher.submit({.type = "dp_read", .priority = OperationPriority::Polling},
                        [](auto) {}, h.now);
    h.dispatcher.submit({.type = "dp_read", .priority = OperationPriority::Polling},
                        [](auto) {}, h.now);
    h.dispatcher.submit({.type = "command",
                         .priority = OperationPriority::SafetyControl},
                        [](auto) {}, h.now);
    REQUIRE(h.sent.size() == 1);   // max_in_flight

    CHECK(h.dispatcher.on_message(reply("m-1", "dp_report")));
    h.advance(1ms);
    REQUIRE(h.sent.size() == 2);
    CHECK(h.sent[1].type == "command");   // safety before the second poll
}

TEST_CASE("rate budget defers sends")
{
    Harness h(RateBudget{.requests_per_second = 2, .burst = 2, .max_in_flight = 10});

    for (int i = 0; i < 5; i++) {
        h.dispatcher.submit({.type = "dp_read", .timeout = 60000ms}, [](auto) {},
                            h.now);
    }
    CHECK(h.sent.size() == 2);        // burst

    h.advance(1000ms);                 // 2 tokens refilled
    CHECK(h.sent.size() == 4);

    h.advance(1000ms);
    CHECK(h.sent.size() == 5);
    CHECK(h.dispatcher.metrics().rate_limit_deferrals > 0);
}

TEST_CASE("coalescing joins identical queued polls")
{
    Harness h(RateBudget{.requests_per_second = 100, .burst = 100,
                         .max_in_flight = 1});

    int callbacks = 0;
    h.dispatcher.submit({.type = "dp_read", .priority = OperationPriority::Polling},
                        [&](auto) { callbacks++; }, h.now);   // goes out immediately

    RequestSpec poll{.type = "dp_read", .priority = OperationPriority::Polling,
                     .coalesce_key = "poll:1s"};
    h.dispatcher.submit(poll, [&](auto) { callbacks++; }, h.now);
    h.dispatcher.submit(poll, [&](auto) { callbacks++; }, h.now);
    CHECK(h.dispatcher.metrics().coalesced == 1);
    CHECK(h.dispatcher.queued() == 1);

    CHECK(h.dispatcher.on_message(reply("m-1", "dp_report")));
    h.advance(1ms);
    REQUIRE(h.sent.size() == 2);
    CHECK(h.dispatcher.on_message(reply("m-2", "dp_report")));
    CHECK(callbacks == 3);   // one frame answers both poll registrations
}

TEST_CASE("send failure completes the handler with an error")
{
    Harness h;
    h.send_ok = false;
    std::optional<Result<nlohmann::json>> outcome;
    h.dispatcher.submit({.type = "dp_read"},
                        [&](Result<nlohmann::json> r) { outcome = std::move(r); },
                        h.now);
    REQUIRE(outcome.has_value());
    CHECK(outcome->error().code == ErrorCode::TransportClosed);
}

TEST_CASE("late response after timeout is consumed, not dispatched")
{
    Harness h;
    h.dispatcher.submit({.type = "dp_read", .timeout = 100ms}, [](auto) {}, h.now);
    h.advance(200ms);
    // Latecomer: recognized as a response (true), but without a handler.
    CHECK(h.dispatcher.on_message(reply("m-1", "dp_report")));
}
