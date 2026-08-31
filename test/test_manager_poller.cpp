// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// DatapointManager + DatapointPoller against a fake submit (no network):
// cache semantics, readback, staging, subscriptions, read_all name list,
// poll grid/coalescing/keepalive.
#include <catch2/catch_test_macros.hpp>

// Designated initializers with default members are intentional here.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include <thread>

#include <fountainer/datapoints/manager.hpp>
#include <fountainer/datapoints/poller.hpp>

using namespace fountainer;
using nlohmann::json;
using namespace std::chrono_literals;

namespace {

struct Harness {
    DatapointManager manager;
    std::vector<protocol::RequestSpec> requests;
    std::vector<protocol::ResponseHandler> handlers;

    Harness()
    {
        manager.bind(
            [this](protocol::RequestSpec spec, protocol::ResponseHandler handler) {
                requests.push_back(std::move(spec));
                handlers.push_back(std::move(handler));
            },
            // "Blocking" runs inline in the test.
            [](std::function<void()> work) -> Status {
                work();
                return ok();
            });
    }

    void answer_last(json message) { handlers.back()(std::move(message)); }
};

}  // namespace

TEST_CASE("read decodes, fills the cache and reports unknown names")
{
    Harness h;

    std::optional<Result<DatapointSnapshot>> outcome;
    h.manager.async_read({"Fon_Current_Pressure", "System_Uptime"},
                         DatapointSource::ExplicitRead,
                         protocol::OperationPriority::InteractiveRead,
                         [&](Result<DatapointSnapshot> r) { outcome = std::move(r); });
    REQUIRE(h.requests.size() == 1);
    CHECK(h.requests[0].type == "dp_read");
    CHECK(h.requests[0].body["names"].size() == 2);

    h.answer_last({{"type", "dp_report"},
                   {"dp",
                    {{"Fon_Current_Pressure", 2.5},
                     {"System_Uptime", 1234},
                     {"Future_Datapoint", 1}}}});

    REQUIRE(outcome.has_value());
    REQUIRE(outcome->has_value());
    CHECK((*outcome)->size() == 2);
    CHECK((*outcome)->unknown() == std::vector<std::string>{"Future_Datapoint"});

    auto cached = h.manager.cached(dp::Fon_Current_Pressure);
    REQUIRE(cached.has_value());
    CHECK(cached->value == 2.5f);
    CHECK(cached->quality == DataQuality::Good);
    CHECK(cached->source == DatapointSource::ExplicitRead);
}

TEST_CASE("empty name list is refused")
{
    Harness h;
    std::optional<Result<DatapointSnapshot>> outcome;
    h.manager.async_read({}, DatapointSource::ExplicitRead,
                         protocol::OperationPriority::InteractiveRead,
                         [&](Result<DatapointSnapshot> r) { outcome = std::move(r); });
    REQUIRE(outcome.has_value());
    CHECK(outcome->error().code == ErrorCode::EmptySelection);
    CHECK(h.requests.empty());
}

TEST_CASE("read_all sends all 107 wire names explicitly")
{
    Harness h;
    h.manager.async_read_all([](auto) {});
    REQUIRE(h.requests.size() == 1);
    CHECK(h.requests[0].body["names"].size() == kDatapointCount);
}

TEST_CASE("write validates client-side and ingests the readback")
{
    Harness h;

    DatapointWriteSet changes;
    changes.set(dp::Fon_Min_Pressure, 2.5f);

    std::optional<Result<WriteResult>> outcome;
    h.manager.async_write(changes,
                          [&](Result<WriteResult> r) { outcome = std::move(r); });
    REQUIRE(h.requests.size() == 1);
    CHECK(h.requests[0].type == "dp_write");
    CHECK(h.requests[0].body["dp"]["Fon_Min_Pressure"] == 2.5);

    h.answer_last({{"type", "dp_write_result"},
                   {"status", "applied"},
                   {"warning", "nvs_save_failed"},
                   {"readback", {{"Fon_Min_Pressure", 2.5}}}});

    REQUIRE(outcome.has_value());
    REQUIRE(outcome->has_value());
    CHECK((*outcome)->applied());
    CHECK((*outcome)->warning == "nvs_save_failed");

    auto cached = h.manager.cached(dp::Fon_Min_Pressure);
    REQUIRE(cached.has_value());
    CHECK(cached->source == DatapointSource::WriteReadback);
}

TEST_CASE("out-of-range write fails before the network")
{
    Harness h;
    DatapointWriteSet changes;
    changes.set(dp::Fon_Min_Pressure, 99.0f);
    std::optional<Result<WriteResult>> outcome;
    h.manager.async_write(changes,
                          [&](Result<WriteResult> r) { outcome = std::move(r); });
    REQUIRE(outcome.has_value());
    CHECK(outcome->error().code == ErrorCode::OutOfRange);
    CHECK(h.requests.empty());
}

TEST_CASE("remote rejection is a result, not an error")
{
    Harness h;
    DatapointWriteSet changes;
    changes.set(dp::Fon_Event_Label, std::uint8_t{7});
    std::optional<Result<WriteResult>> outcome;
    h.manager.async_write(changes,
                          [&](Result<WriteResult> r) { outcome = std::move(r); });
    h.answer_last({{"type", "dp_write_result"},
                   {"status", "rejected"},
                   {"errors", {{"Fon_Event_Label", "out_of_range"}}}});
    REQUIRE(outcome->has_value());
    CHECK_FALSE((*outcome)->applied());
    REQUIRE((*outcome)->errors.size() == 1);
    CHECK((*outcome)->errors[0].reason == "out_of_range");
}

TEST_CASE("subscriptions fire on change with old value, RAII unsubscribes")
{
    Harness h;

    std::vector<float> seen;
    {
        auto subscription = h.manager.subscribe(
            dp::Fon_Current_Pressure,
            [&](const DatapointChange<float>& change) { seen.push_back(change.value); });

        h.manager.ingest_report({{"Fon_Current_Pressure", 1.0}},
                                DatapointSource::UnsolicitedReport, 1);
        h.manager.ingest_report({{"Fon_Current_Pressure", 1.0}},
                                DatapointSource::UnsolicitedReport, 2);   // unchanged
        h.manager.ingest_report({{"Fon_Current_Pressure", 2.0}},
                                DatapointSource::UnsolicitedReport, 3);
        CHECK(seen == std::vector<float>{1.0f, 2.0f});
    }
    h.manager.ingest_report({{"Fon_Current_Pressure", 3.0}},
                            DatapointSource::UnsolicitedReport, 4);
    CHECK(seen.size() == 2);   // token gone = unsubscribed
}

TEST_CASE("staging commits atomically and survives rejection")
{
    Harness h;
    CHECK_FALSE(h.manager.has_staged_changes());
    CHECK(h.manager.stage(dp::Fon_Min_Pressure, 2.1f));
    CHECK(h.manager.stage(dp::Fon_Max_Pressure, 3.6f));
    CHECK(h.manager.has_staged_changes());
    CHECK(h.manager.validate_staged());

    // Commit -> one dp_write with both datapoints; rejection preserves staging.
    std::thread commit_thread;   // the sync commit needs the response
    Result<WriteResult> result{unexpected_t{internal_error("pending")}};
    commit_thread = std::thread([&] { result = h.manager.commit(); });
    while (h.handlers.empty()) std::this_thread::yield();
    CHECK(h.requests.back().body["dp"].size() == 2);
    h.answer_last({{"type", "dp_write_result"}, {"status", "rejected"}});
    commit_thread.join();

    REQUIRE(result.has_value());
    CHECK_FALSE(result->applied());
    CHECK(h.manager.has_staged_changes());   // preserved for correction

    h.manager.discard_staged();
    CHECK_FALSE(h.manager.has_staged_changes());
}

TEST_CASE("disconnect marks cached values stale")
{
    Harness h;
    h.manager.ingest_report({{"System_Uptime", 5}}, DatapointSource::PeriodicPoll,
                            std::nullopt);
    h.manager.mark_all_stale();
    auto cached = h.manager.cached(dp::System_Uptime);
    REQUIRE(cached.has_value());
    CHECK(cached->quality == DataQuality::Stale);
    CHECK(cached->value == 5);   // value stays displayable (design concept §21.3)
}

// ---------------------------------------------------------------------------
// Poller
// ---------------------------------------------------------------------------

TEST_CASE("poller coalesces due datapoints into one request")
{
    Harness h;
    DatapointPoller poller(h.manager);
    poller.set_keepalive(std::chrono::seconds{0});
    poller.every(1s, dp::Fon_Current_Pressure, dp::Fon_Current_State);
    poller.every(5s, dp::System_RSSI);
    poller.start();

    const auto t0 = DatapointPoller::Clock::now();
    poller.tick(t0, t0);
    REQUIRE(h.requests.size() == 1);   // all three due together
    CHECK(h.requests[0].body["names"].size() == 3);
    CHECK(h.requests[0].priority == protocol::OperationPriority::Polling);

    // Respond -> in_flight is free again.
    h.answer_last({{"type", "dp_report"}, {"dp", json::object()}});

    poller.tick(t0 + 1s, t0 + 1s);
    REQUIRE(h.requests.size() == 2);   // only the 1 s group
    CHECK(h.requests[1].body["names"].size() == 2);
}

TEST_CASE("poller does not stack requests while one is in flight")
{
    Harness h;
    DatapointPoller poller(h.manager);
    poller.set_keepalive(std::chrono::seconds{0});
    poller.every(1s, dp::Fon_Current_Pressure);
    poller.start();

    const auto t0 = DatapointPoller::Clock::now();
    poller.tick(t0, t0);
    poller.tick(t0 + 1s, t0);    // response still pending
    poller.tick(t0 + 2s, t0);
    CHECK(h.requests.size() == 1);
    CHECK(poller.stats().skipped_inflight == 2);

    h.answer_last({{"type", "dp_report"}, {"dp", json::object()}});
    poller.tick(t0 + 3s, t0);
    CHECK(h.requests.size() == 2);
}

TEST_CASE("missed cycles are skipped, not replayed")
{
    Harness h;
    DatapointPoller poller(h.manager);
    poller.set_keepalive(std::chrono::seconds{0});
    poller.every(1s, dp::Fon_Current_Pressure);
    poller.start();

    const auto t0 = DatapointPoller::Clock::now();
    poller.tick(t0, t0);
    h.answer_last({{"type", "dp_report"}, {"dp", json::object()}});

    poller.tick(t0 + 10s, t0);   // 9 cycles missed
    CHECK(h.requests.size() == 2);
    CHECK(poller.stats().missed_deadlines >= 8);
}

TEST_CASE("once-datapoints are read exactly once per connection")
{
    Harness h;
    DatapointPoller poller(h.manager);
    poller.set_keepalive(std::chrono::seconds{0});
    poller.once(dp::Device_Serial_Number);
    poller.start();

    const auto t0 = DatapointPoller::Clock::now();
    poller.tick(t0, t0);
    REQUIRE(h.requests.size() == 1);
    h.answer_last({{"type", "dp_report"}, {"dp", json::object()}});
    poller.tick(t0 + 1s, t0);
    CHECK(h.requests.size() == 1);

    poller.on_reconnected();     // resync after reconnect
    poller.tick(t0 + 2s, t0);
    CHECK(h.requests.size() == 2);
}

TEST_CASE("keepalive fires only when the session is silent")
{
    Harness h;
    DatapointPoller poller(h.manager);
    poller.set_keepalive(std::chrono::seconds{240});
    poller.start();

    const auto t0 = DatapointPoller::Clock::now();
    poller.tick(t0, t0);
    CHECK(h.requests.empty());

    poller.tick(t0 + 241s, t0);   // silent for 241 s
    REQUIRE(h.requests.size() == 1);
    CHECK(h.requests[0].body["names"] == json::array({"System_Uptime"}));
    CHECK(h.requests[0].priority == protocol::OperationPriority::Background);
    CHECK(poller.stats().keepalives == 1);

    // Just sent -> no keepalive.
    h.answer_last({{"type", "dp_report"}, {"dp", json::object()}});
    poller.tick(t0 + 242s, t0 + 241s);
    CHECK(h.requests.size() == 1);
}

TEST_CASE("start_defaults maps the poll classes from the generated catalog")
{
    Harness h;
    DatapointPoller poller(h.manager);
    poller.set_keepalive(std::chrono::seconds{0});
    poller.start_defaults();

    const auto t0 = DatapointPoller::Clock::now();
    poller.tick(t0, t0);
    REQUIRE_FALSE(h.requests.empty());

    // Disabled datapoints (sensitive/command) must never run automatically.
    std::size_t polled = 0;
    for (const auto& request : h.requests) {
        for (const auto& name : request.body["names"]) {
            polled++;
            CHECK(name != "Network_Password");
            CHECK(name != "Backup_Password");
            CHECK(name != "Network_Save");
            CHECK(name != "Log_Command");
        }
    }
    CHECK(polled == kDatapointCount - 4);
}
