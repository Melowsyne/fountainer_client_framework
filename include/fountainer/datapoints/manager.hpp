// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// DatapointManager (design concept §11): catalog access, read/write, cache,
// readback processing, push reports, staging/commit, subscriptions.
// It owns NO timer logic — cyclic polling is done by DatapointPoller.
//
// Terminology discipline (design concept §11.5):
//   read()    network            cached()   local cache only
//   stage()   local intent       commit()   write staged values atomically
//
// Threading: the async_* methods and ingest_* run on the client's IO
// thread. The synchronous methods block and therefore must NOT be called
// from callbacks (IO thread) — the client detects this and returns
// InvalidState instead of deadlocking.
#pragma once

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <fountainer/datapoints/catalog.hpp>
#include <fountainer/datapoints/snapshot.hpp>
#include <fountainer/datapoints/write_set.hpp>
#include <fountainer/events.hpp>
#include <fountainer/protocol/dispatcher.hpp>

namespace fountainer {

// Cache/value change for subscriptions (design concept §14).
struct AnyDatapointChange {
    const DatapointDescriptor* descriptor = nullptr;
    DatapointValue value{};
    std::optional<DatapointValue> old_value;
    DatapointSource source = DatapointSource::ExplicitRead;
    std::chrono::steady_clock::time_point received_at{};
};

template <typename T>
struct DatapointChange {
    T value;
    std::optional<T> old_value;
    DatapointSource source;
    std::chrono::steady_clock::time_point received_at;
};

class DatapointManager {
public:
    // Bridge to the session: submit a request (runs on the IO thread).
    using SubmitFn =
        std::function<void(protocol::RequestSpec, protocol::ResponseHandler)>;
    // Synchronous wrapper: hands the work over to the IO thread; returns an
    // error when called from the IO thread itself (deadlock guard in the client).
    using BlockingRunner = std::function<Status(std::function<void()>)>;

    using ReadCompletion = std::function<void(Result<DatapointSnapshot>)>;
    using WriteCompletion = std::function<void(Result<WriteResult>)>;

    DatapointManager();
    ~DatapointManager();
    DatapointManager(const DatapointManager&) = delete;
    DatapointManager& operator=(const DatapointManager&) = delete;

    // Wiring done by the client.
    void bind(SubmitFn submit, BlockingRunner runner);

    // ------------------------------------------------------------------
    // Asynchronous core API (IO-thread-safe)
    // ------------------------------------------------------------------

    // Explicit name list; an empty list is an error (the firmware would
    // locally return only the VOLATILE points — design concept §12).
    void async_read(std::vector<std::string> names, DatapointSource source,
                    protocol::OperationPriority priority, ReadCompletion completion,
                    std::string coalesce_key = {});

    void async_read_all(ReadCompletion completion);

    void async_write(const DatapointWriteSet& changes, WriteCompletion completion);

    // ------------------------------------------------------------------
    // Synchronous convenience API (not from the IO thread!)
    // ------------------------------------------------------------------

    template <ReadableDatapoint DP>
    Result<typename DP::value_type> read(const DP& datapoint)
    {
        auto snapshot = read_names({std::string(datapoint.name)});
        if (!snapshot) return fail(snapshot.error());
        return snapshot->get(datapoint);
    }

    // Several datapoints in ONE request.
    template <ReadableDatapoint... DP>
        requires(sizeof...(DP) > 1)
    Result<DatapointSnapshot> read(const DP&... datapoints)
    {
        return read_names({std::string(datapoints.name)...});
    }

    Result<DatapointSnapshot> read_names(std::vector<std::string> names);
    Result<DatapointSnapshot> read_all();

    // Dynamic runtime API (design concept §10.5) — second layer, not the default.
    Result<DatapointValue> read(std::string_view name);

    template <WritableDatapoint DP>
    Result<WriteResult> write(const DP& datapoint, typename DP::value_type value)
    {
        DatapointWriteSet changes;
        changes.set(datapoint, std::move(value));
        return write(changes);
    }

    Result<WriteResult> write(const DatapointWriteSet& changes);
    Result<WriteResult> write(std::string_view name, DatapointValue value);

    // ------------------------------------------------------------------
    // Cache (purely local, never network)
    // ------------------------------------------------------------------

    template <ReadableDatapoint DP>
    [[nodiscard]] std::optional<CachedDatapoint<typename DP::value_type>> cached(
        const DP& datapoint) const
    {
        const auto raw = cached_raw(datapoint.name);
        if (!raw) return std::nullopt;
        const auto* typed = std::get_if<typename DP::value_type>(&raw->value);
        if (typed == nullptr) return std::nullopt;
        return CachedDatapoint<typename DP::value_type>{
            *typed, raw->received_at, raw->source, raw->quality,
            raw->report_sequence};
    }

    [[nodiscard]] std::optional<CachedValue> cached_raw(std::string_view name) const;
    [[nodiscard]] DatapointSnapshot cached_snapshot() const;

    // ------------------------------------------------------------------
    // Staging/commit for configuration editors (design concept §11.6)
    // ------------------------------------------------------------------

    template <WritableDatapoint DP>
    Status stage(const DP& datapoint, typename DP::value_type value)
    {
        return stage(datapoint.name, *make_value(catalog::at(datapoint),
                                                 std::move(value)));
    }

    Status stage(std::string_view name, DatapointValue value);
    [[nodiscard]] bool has_staged_changes() const;

    // Checks single-value AND cross-field constraints (mirror of the
    // firmware's dp_constraints_ok()); missing operands come from the
    // cache. Pre-validation — the firmware remains authoritative (§11.7).
    [[nodiscard]] Status validate_staged() const;
    [[nodiscard]] Status validate_constraints(const DatapointWriteSet& changes) const;
    Result<WriteResult> commit();
    void discard_staged();

    // ------------------------------------------------------------------
    // Subscriptions (design concept §14) — cache changes, delivered outside
    // of any internal lock.
    // ------------------------------------------------------------------

    template <ReadableDatapoint DP, typename Callback>
    Subscription subscribe(const DP& datapoint, Callback&& callback)
    {
        return subscribe_raw(
            datapoint.name,
            [cb = std::forward<Callback>(callback)](const AnyDatapointChange& change) {
                using T = typename DP::value_type;
                if (const T* typed = std::get_if<T>(&change.value)) {
                    std::optional<T> old;
                    if (change.old_value) {
                        if (const T* prev = std::get_if<T>(&*change.old_value)) {
                            old = *prev;
                        }
                    }
                    cb(DatapointChange<T>{*typed, std::move(old), change.source,
                                          change.received_at});
                }
            });
    }

    Subscription subscribe_raw(std::string_view name,
                               std::function<void(const AnyDatapointChange&)> cb);
    Subscription subscribe_all(std::function<void(const AnyDatapointChange&)> cb);

    // ------------------------------------------------------------------
    // Ingestion by client/session (IO thread)
    // ------------------------------------------------------------------

    // Take the dp object of a dp_report into the cache.
    DatapointSnapshot ingest_report(const nlohmann::json& dp, DatapointSource source,
                                    std::optional<std::uint32_t> sequence);

    // Connection lost: keep the values but mark them as Stale (§21.3).
    void mark_all_stale();

private:
    Result<WriteResult> write_json(nlohmann::json dp_object,
                                   protocol::OperationPriority priority);
    void async_write_json(nlohmann::json dp_object,
                          protocol::OperationPriority priority,
                          WriteCompletion completion);
    WriteResult parse_write_result(const nlohmann::json& message);

    struct Impl;
    // shared + weak in the subscription tokens (like EventBus): tokens
    // may safely outlive the manager.
    std::shared_ptr<Impl> impl_;
};

}  // namespace fountainer
