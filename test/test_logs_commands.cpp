// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// LogService (including pagination via the last received record) and
// CommandService against a fake submit.
#include <catch2/catch_test_macros.hpp>

// Designated initializers with default members are intentional here.
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include <thread>

#include <fountainer/commands.hpp>
#include <fountainer/logs.hpp>

using namespace fountainer;
using nlohmann::json;
using namespace std::chrono_literals;

namespace {

struct Harness {
    DatapointManager manager;
    LogService logs;
    CommandService commands;
    std::vector<protocol::RequestSpec> requests;
    std::vector<protocol::ResponseHandler> handlers;

    Harness()
    {
        auto submit = [this](protocol::RequestSpec spec,
                             protocol::ResponseHandler handler) {
            requests.push_back(std::move(spec));
            handlers.push_back(std::move(handler));
        };
        auto runner = [](std::function<void()> work) -> Status {
            work();
            return ok();
        };
        manager.bind(submit, runner);
        logs.bind(submit, runner, manager);
        commands.bind(submit, runner);
    }

    // Answers the open request at the given index (FIFO by index).
    void answer(std::size_t index, json message) { handlers.at(index)(std::move(message)); }
};

json log_batch(std::uint32_t first_seq, std::uint32_t count, std::uint32_t next_seq)
{
    json records = json::array();
    for (std::uint32_t i = 0; i < count; i++) {
        records.push_back({{"s", first_seq + i},
                           {"u", 1000 + i},
                           {"ev", 7},
                           {"mod", 2},
                           {"lvl", 3},
                           {"t", "record " + std::to_string(first_seq + i)}});
    }
    return {{"type", "log_batch"}, {"boot_id", 11},   {"first_seq_available", 1},
            {"next_seq", next_seq}, {"dropped_count", 0}, {"overflow", false},
            {"records", records}};
}

}  // namespace

TEST_CASE("log_read parses the firmware batch shape")
{
    Harness h;
    std::optional<Result<LogBatch>> outcome;
    h.logs.async_read({.since_sequence = 5, .minimum_level = LogLevel::Info,
                       .max_records = 200},
                      [&](Result<LogBatch> r) { outcome = std::move(r); });

    REQUIRE(h.requests.size() == 1);
    CHECK(h.requests[0].type == "log_read");
    CHECK(h.requests[0].body["since_seq"] == 5);
    CHECK(h.requests[0].body["min_level"] == 3);
    CHECK(h.requests[0].body["max_records"] == 128);   // firmware limit

    h.answer(0, log_batch(6, 3, 9));
    REQUIRE(outcome->has_value());
    CHECK((*outcome)->boot_id == 11);
    CHECK((*outcome)->records.size() == 3);
    CHECK((*outcome)->last_sequence() == 8);
}

TEST_CASE("read_all continues from the last RECEIVED record, not next_seq")
{
    Harness h;

    Result<std::vector<LogRecord>> result{unexpected_t{internal_error("pending")}};
    std::thread worker([&] { result = h.logs.read_all(); });

    while (h.handlers.empty()) std::this_thread::yield();
    // Batch 1: the ring knows up to 100, but the byte budget cut off at 40.
    h.answer(0, log_batch(1, 40, 101));

    while (h.handlers.size() < 2) std::this_thread::yield();
    CHECK(h.requests[1].body["since_seq"] == 40);   // last record, NOT 101
    h.answer(1, log_batch(41, 60, 101));

    worker.join();
    REQUIRE(result.has_value());
    CHECK(result->size() == 100);
    CHECK(result->front().sequence == 1);
    CHECK(result->back().sequence == 100);
}

TEST_CASE("log_read_prev exposes availability")
{
    Harness h;
    std::optional<Result<LogBatch>> outcome;
    h.logs.async_read_previous({}, [&](Result<LogBatch> r) { outcome = std::move(r); });
    CHECK(h.requests[0].type == "log_read_prev");
    h.answer(0, {{"type", "log_batch"}, {"boot_id", 10}, {"available", true},
                 {"records", json::array()}});
    REQUIRE(outcome->has_value());
    CHECK((*outcome)->previous_boot_available);
    CHECK((*outcome)->boot_id == 10);
}

TEST_CASE("commands map to the wire contract")
{
    Harness h;

    h.commands.async_set_state(FountainState::Off, [](auto) {});
    REQUIRE(h.requests.size() == 1);
    CHECK(h.requests[0].type == "command");
    CHECK(h.requests[0].body["command"] == "set_state");
    CHECK(h.requests[0].body["target_state"] == "Off");
    CHECK(h.requests[0].priority == protocol::OperationPriority::SafetyControl);

    h.commands.async_set_state(FountainState::Auto, [](auto) {});
    CHECK(h.requests[1].priority == protocol::OperationPriority::InteractiveControl);

    // 45 s -> 2 steps of 30 s each (rounded up).
    h.commands.async_turn_on_for(45s, [](auto) {});
    CHECK(h.requests[2].body["command"] == "turn_on_duration");
    CHECK(h.requests[2].body["duration_steps"] == 2);

    std::optional<Result<CommandResult>> outcome;
    h.commands.async_reboot([&](Result<CommandResult> r) { outcome = std::move(r); });
    h.answer(3, {{"type", "command_result"}, {"status", "rejected"},
                 {"error", "not_permitted"}});
    REQUIRE(outcome->has_value());
    CHECK_FALSE((*outcome)->applied());
    CHECK((*outcome)->error == "not_permitted");
}

TEST_CASE("turn_on_for maps the UI presets to exact step counts")
{
    Harness h;
    // 2 / 5 / 10 / 15 min -> 4 / 10 / 20 / 30 steps; an odd 61 s rounds up.
    const std::pair<std::chrono::seconds, int> cases[] = {
        {120s, 4}, {300s, 10}, {600s, 20}, {900s, 30}, {61s, 3},
    };
    std::size_t i = 0;
    for (const auto& [duration, steps] : cases) {
        h.commands.async_turn_on_for(duration, [](auto) {});
        REQUIRE(h.requests.size() == i + 1);
        CHECK(h.requests[i].body["command"] == "turn_on_duration");
        CHECK(h.requests[i].body["duration_steps"] == steps);
        ++i;
    }
}

TEST_CASE("zero duration is rejected client-side")
{
    Harness h;
    std::optional<Result<CommandResult>> outcome;
    h.commands.async_turn_on_for(0s, [&](Result<CommandResult> r) { outcome = std::move(r); });
    REQUIRE(outcome.has_value());
    CHECK(outcome->error().code == ErrorCode::OutOfRange);
    CHECK(h.requests.empty());
}
