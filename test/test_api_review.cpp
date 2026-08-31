// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Regression tests for the findings of the API review (refactoring plan §5):
// R3 subscription lifetime, R4 default request timeout, R8 log sink.
#include <catch2/catch_test_macros.hpp>

// Designated initializers with default members are intentional here.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include <memory>

#include <fountainer/client.hpp>
#include <fountainer/datapoints/manager.hpp>
#include <fountainer/events.hpp>
#include <fountainer/logging/logger.hpp>
#include <fountainer/protocol/dispatcher.hpp>

using namespace fountainer;
using namespace std::chrono_literals;

TEST_CASE("R3: event subscription may outlive the bus")
{
    Subscription token;
    {
        auto bus = std::make_unique<EventBus>();
        token = bus->on_heartbeat([](const Heartbeat&) {});
        CHECK(token.active());
    }   // the bus dies before the token
    token.reset();   // must not be a use-after-free (ASan gate)
    CHECK_FALSE(token.active());
}

TEST_CASE("R3: datapoint subscription may outlive the manager")
{
    Subscription token;
    {
        auto manager = std::make_unique<DatapointManager>();
        token = manager->subscribe_all([](const AnyDatapointChange&) {});
    }
    token.reset();
    CHECK_FALSE(token.active());
}

TEST_CASE("R4: dispatcher default timeout is configurable")
{
    using namespace fountainer::protocol;
    const TimePoint start = Clock::now();

    RequestDispatcher dispatcher(
        [](const RequestSpec&) -> Result<std::string> { return std::string("m-1"); },
        RateBudget{}, 500ms);

    std::optional<Result<nlohmann::json>> outcome;
    // NO explicit timeout -> the dispatcher's default (500 ms).
    dispatcher.submit({.type = "dp_read"},
                      [&](Result<nlohmann::json> r) { outcome = std::move(r); },
                      start);

    dispatcher.tick(start + 400ms);
    CHECK_FALSE(outcome.has_value());

    dispatcher.tick(start + 600ms);
    REQUIRE(outcome.has_value());
    CHECK(outcome->error().code == ErrorCode::RequestTimeout);
}

TEST_CASE("R4: explicit per-request timeout overrides the default")
{
    using namespace fountainer::protocol;
    const TimePoint start = Clock::now();

    RequestDispatcher dispatcher(
        [](const RequestSpec&) -> Result<std::string> { return std::string("m-1"); },
        RateBudget{}, 500ms);

    std::optional<Result<nlohmann::json>> outcome;
    dispatcher.submit({.type = "dp_read", .timeout = 5000ms},
                      [&](Result<nlohmann::json> r) { outcome = std::move(r); },
                      start);

    dispatcher.tick(start + 1000ms);
    CHECK_FALSE(outcome.has_value());   // the default (500 ms) does NOT apply
    dispatcher.tick(start + 6000ms);
    REQUIRE(outcome.has_value());
}

TEST_CASE("R7: builder validates configuration up front")
{
    // Without TLS/HMAC, build() fails immediately with a configuration error —
    // not only the later connect(). (As a side effect this constructs/tears
    // down the client's IO thread — R2 regression under TSan/ASan.)
    auto missing = Client::builder(Endpoint{"127.0.0.1"}).build();
    REQUIRE_FALSE(missing.has_value());
    CHECK(missing.error().domain == ErrorDomain::Configuration);
    CHECK(missing.error().code == ErrorCode::MissingCredentials);

    auto bad_file = Client::builder(Endpoint{"127.0.0.1"})
                        .with_tls(TlsCredentials::mutual_tls(
                            "/nonexistent/ca.pem", "/nonexistent/c.crt",
                            "/nonexistent/c.key"))
                        .build();
    REQUIRE_FALSE(bad_file.has_value());
    CHECK(bad_file.error().code == ErrorCode::FileNotReadable);
}

TEST_CASE("R8: log sink receives library output and can be removed")
{
    struct Captured {
        log::Level level;
        std::string category;
        std::string message;
    };
    auto captured = std::make_shared<std::vector<Captured>>();

    log::set_sink([captured](log::Level level, const char* category,
                             const std::string& message) {
        captured->push_back({level, category, message});
    });
    log::set_level(log::Level::Info);
    log::info("TEST", "hello sink");
    log::debug("TEST", "filtered out");   // below the level filter

    REQUIRE(captured->size() == 1);
    CHECK(captured->front().category == "TEST");
    CHECK(captured->front().message == "hello sink");

    log::set_sink(nullptr);   // restore the stderr default
    log::info("TEST", "back to stderr");
    CHECK(captured->size() == 1);
}
