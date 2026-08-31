// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Log API (design concept §16). Firmware contract (task_com_fill_log_batch):
//
//   log_read      {since_seq, min_level, max_records<=128}
//   log_batch     {boot_id, first_seq_available, next_seq, dropped_count,
//                  overflow, records:[{s,u,ev,mod,lvl,a[],t}]}
//   log_read_prev {min_level, max_records} -> {boot_id, available, records}
//   log_ack_prev  {boot_id} -> log_ack_result {ok}
//
// IMPORTANT for pagination: the byte budget (24 kB, POOR link 4 kB) can
// TRUNCATE a batch, while next_seq reports the global ring state.
// read_all() therefore continues with the sequence of the last record that
// was actually received, never blindly with next_seq (§16.2).
#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include <fountainer/datapoints/manager.hpp>
#include <fountainer/protocol/dispatcher.hpp>
#include <fountainer/result.hpp>

namespace fountainer {

enum class LogLevel : std::uint8_t { None = 0, Error, Warning, Info, Debug, Trace };

struct LogRecord {
    std::uint32_t sequence = 0;      // s
    std::uint32_t uptime_ms = 0;     // u
    std::uint16_t event_id = 0;      // ev
    std::uint8_t module_id = 0;      // mod
    std::uint8_t level = 0;          // lvl
    std::vector<std::int64_t> args;  // a
    std::string text;                // t
};

struct LogBatch {
    std::uint32_t boot_id = 0;
    std::uint32_t first_seq_available = 0;
    std::uint32_t next_seq = 0;
    std::uint32_t dropped_count = 0;
    bool overflow = false;
    bool previous_boot_available = false;   // log_read_prev only
    std::vector<LogRecord> records;

    // Continuation point for the next page (last received record).
    [[nodiscard]] std::optional<std::uint32_t> last_sequence() const
    {
        if (records.empty()) return std::nullopt;
        return records.back().sequence;
    }
};

struct LogAckResult {
    bool ok = false;
};

struct LogReadOptions {
    std::uint32_t since_sequence = 0;
    LogLevel minimum_level = LogLevel::None;   // None = no filter
    std::uint16_t max_records = 64;            // firmware caps at 128
};

class LogService {
public:
    using SubmitFn =
        std::function<void(protocol::RequestSpec, protocol::ResponseHandler)>;
    using BlockingRunner = std::function<Status(std::function<void()>)>;
    using BatchCompletion = std::function<void(Result<LogBatch>)>;

    // manager is needed for the Log_Command datapoint operations.
    void bind(SubmitFn submit, BlockingRunner runner, DatapointManager& manager);

    // --- synchronous (not from the IO thread) ---
    Result<LogBatch> read(LogReadOptions options = {});
    Result<LogBatch> read_previous(LogReadOptions options = {});

    // Paginated full retrieval of the runtime log.
    Result<std::vector<LogRecord>> read_all(LogReadOptions options = {});

    Result<LogAckResult> ack_previous(std::uint32_t boot_id);

    // Log_Command datapoint, named semantically (design concept §16.4):
    //   1 = clear_runtime (DESTRUCTIVE), 2 = ack previous, 3 = flush.
    Result<void> clear_runtime();
    Result<void> flush();

    // --- asynchronous ---
    void async_read(LogReadOptions options, BatchCompletion completion);
    void async_read_previous(LogReadOptions options, BatchCompletion completion);
    void async_ack_previous(std::uint32_t boot_id,
                            std::function<void(Result<LogAckResult>)> completion);

private:
    void async_read_impl(const char* type, LogReadOptions options,
                         BatchCompletion completion);
    Result<void> write_log_command(std::uint8_t value);

    SubmitFn submit_;
    BlockingRunner runner_;
    DatapointManager* manager_ = nullptr;
};

}  // namespace fountainer
