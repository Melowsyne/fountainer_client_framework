// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/logs.hpp"

#include <fountainer/datapoints/generated.hpp>
#include <fountainer/detail/sync_wait.hpp>

namespace fountainer {

namespace {

LogBatch parse_batch(const nlohmann::json& message)
{
    LogBatch batch;
    batch.boot_id = message.value("boot_id", std::uint32_t{0});
    batch.first_seq_available = message.value("first_seq_available", std::uint32_t{0});
    batch.next_seq = message.value("next_seq", std::uint32_t{0});
    batch.dropped_count = message.value("dropped_count", std::uint32_t{0});
    batch.overflow = message.value("overflow", false);
    batch.previous_boot_available = message.value("available", false);

    for (const auto& raw : message.value("records", nlohmann::json::array())) {
        if (!raw.is_object()) continue;
        LogRecord record;
        record.sequence = raw.value("s", std::uint32_t{0});
        record.uptime_ms = raw.value("u", std::uint32_t{0});
        record.event_id = raw.value("ev", std::uint16_t{0});
        record.module_id = static_cast<std::uint8_t>(raw.value("mod", 0));
        record.level = static_cast<std::uint8_t>(raw.value("lvl", 0));
        for (const auto& arg : raw.value("a", nlohmann::json::array())) {
            if (arg.is_number()) record.args.push_back(arg.get<std::int64_t>());
        }
        record.text = raw.value("t", std::string{});
        batch.records.push_back(std::move(record));
    }
    return batch;
}

}  // namespace

void LogService::bind(SubmitFn submit, BlockingRunner runner,
                      DatapointManager& manager)
{
    submit_ = std::move(submit);
    runner_ = std::move(runner);
    manager_ = &manager;
}

void LogService::async_read_impl(const char* type, LogReadOptions options,
                                 BatchCompletion completion)
{
    protocol::RequestSpec spec;
    spec.type = type;
    spec.body = nlohmann::json::object();
    if (options.since_sequence > 0) spec.body["since_seq"] = options.since_sequence;
    if (options.minimum_level != LogLevel::None) {
        spec.body["min_level"] = static_cast<int>(options.minimum_level);
    }
    spec.body["max_records"] = std::min<std::uint16_t>(options.max_records, 128);
    spec.priority = protocol::OperationPriority::LogTransfer;
    // Log batches can be large and take longer than a dp_read.
    spec.timeout = std::chrono::milliseconds(20000);

    submit_(std::move(spec),
            [completion = std::move(completion)](Result<nlohmann::json> response) {
                if (!response) {
                    completion(fail(response.error()));
                    return;
                }
                completion(parse_batch(*response));
            });
}

void LogService::async_read(LogReadOptions options, BatchCompletion completion)
{
    async_read_impl("log_read", options, std::move(completion));
}

void LogService::async_read_previous(LogReadOptions options,
                                     BatchCompletion completion)
{
    async_read_impl("log_read_prev", options, std::move(completion));
}

void LogService::async_ack_previous(
    std::uint32_t boot_id, std::function<void(Result<LogAckResult>)> completion)
{
    protocol::RequestSpec spec;
    spec.type = "log_ack_prev";
    spec.body = {{"boot_id", boot_id}};
    spec.priority = protocol::OperationPriority::LogTransfer;

    submit_(std::move(spec),
            [completion = std::move(completion)](Result<nlohmann::json> response) {
                if (!response) {
                    completion(fail(response.error()));
                    return;
                }
                completion(LogAckResult{response->value("ok", false)});
            });
}

Result<LogBatch> LogService::read(LogReadOptions options)
{
    return detail::sync_wait<LogBatch>(
        runner_, [this, options](std::function<void(Result<LogBatch>)> done) {
            async_read(options, std::move(done));
        });
}

Result<LogBatch> LogService::read_previous(LogReadOptions options)
{
    return detail::sync_wait<LogBatch>(
        runner_, [this, options](std::function<void(Result<LogBatch>)> done) {
            async_read_previous(options, std::move(done));
        });
}

Result<std::vector<LogRecord>> LogService::read_all(LogReadOptions options)
{
    std::vector<LogRecord> all;
    LogReadOptions page = options;
    // 64 instead of the protocol limit of 128: the LOCAL server never
    // delivers WS frames above ~10 KB (measured live: 100 records ok, 110
    // vanish without comment — a firmware finding, reported to the firmware
    // team). 64 is also the firmware default and stays below the limit even
    // with long texts.
    page.max_records = 64;

    for (;;) {
        auto batch = read(page);
        if (!batch && batch.error().code == ErrorCode::RequestTimeout) {
            // A single flash-log pull can hang on the device (lock
            // contention with ongoing flash writers) — EXACTLY ONE retry per
            // page, then give up (observed live on the devkit).
            batch = read(page);
        }
        if (!batch) return fail(batch.error());

        for (auto& record : batch->records) all.push_back(std::move(record));

        // Continue from the LAST RECEIVED record (§16.2): the byte budget
        // may have truncated the batch; next_seq only describes the ring,
        // not what was delivered.
        const auto last = batch->last_sequence();
        if (!last) break;                       // empty batch = done
        if (*last + 1 >= batch->next_seq) break; // ring fully read
        page.since_sequence = *last;
    }
    return all;
}

Result<LogAckResult> LogService::ack_previous(std::uint32_t boot_id)
{
    return detail::sync_wait<LogAckResult>(
        runner_, [this, boot_id](std::function<void(Result<LogAckResult>)> done) {
            async_ack_previous(boot_id, std::move(done));
        });
}

Result<void> LogService::write_log_command(std::uint8_t value)
{
    if (manager_ == nullptr) {
        return fail(internal_error("LogService is not bound to a client"));
    }
    auto result = manager_->write(dp::Log_Command, value);
    if (!result) return fail(result.error());
    if (!result->applied()) {
        Error error = make_error(ErrorDomain::Remote, ErrorCode::RemoteRejected,
                                 "device rejected Log_Command");
        if (!result->errors.empty()) error.remote_detail = result->errors.front().reason;
        return fail(std::move(error));
    }
    return ok();
}

Result<void> LogService::clear_runtime() { return write_log_command(1); }
Result<void> LogService::flush() { return write_log_command(3); }

}  // namespace fountainer
