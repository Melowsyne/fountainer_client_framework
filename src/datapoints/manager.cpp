// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/datapoints/manager.hpp"

#include <utility>

#include "fountainer/datapoints/constraints.hpp"
#include "fountainer/detail/sync_wait.hpp"
#include "fountainer/logging/logger.hpp"

namespace fountainer {

namespace {

constexpr const char* kLogCat = "DATAPOINTS";

// Decode dp_read responses: type known names, collect unknown ones
// (newer firmware is NOT an error — design concept §31.3).
DatapointSnapshot decode_dp_object(const nlohmann::json& dp)
{
    DatapointSnapshot snapshot;
    if (!dp.is_object()) return snapshot;
    for (const auto& [name, raw] : dp.items()) {
        const DatapointDescriptor* descriptor = catalog::find(name);
        if (descriptor == nullptr) {
            snapshot.add_unknown(name);
            continue;
        }
        auto value = value_from_json(*descriptor, raw);
        if (!value) {
            log::warn(kLogCat, "undecodable value for " + name + ": " +
                                   value.error().message);
            snapshot.add_unknown(name);
            continue;
        }
        snapshot.set(name, std::move(*value));
    }
    return snapshot;
}

}  // namespace

std::string_view to_string(DatapointSource source) noexcept
{
    switch (source) {
    case DatapointSource::ExplicitRead:      return "explicit_read";
    case DatapointSource::PeriodicPoll:      return "periodic_poll";
    case DatapointSource::UnsolicitedReport: return "unsolicited_report";
    case DatapointSource::WriteReadback:     return "write_readback";
    }
    return "?";
}

std::string_view to_string(DataQuality quality) noexcept
{
    switch (quality) {
    case DataQuality::Unknown: return "unknown";
    case DataQuality::Good:    return "good";
    case DataQuality::Stale:   return "stale";
    case DataQuality::Invalid: return "invalid";
    }
    return "?";
}

struct DatapointManager::Impl {
    SubmitFn submit;
    BlockingRunner runner;

    mutable std::mutex mutex;
    std::map<std::string, CachedValue, std::less<>> cache;
    std::map<std::string, DatapointValue> staged;

    struct Slot {
        std::uint64_t token;
        std::string name;   // empty = subscribe_all
        std::function<void(const AnyDatapointChange&)> callback;
    };
    std::vector<Slot> subscriptions;
    std::uint64_t next_token = 0;

    // Apply the changes and COLLECT the callbacks that are due — they are
    // invoked outside the lock.
    std::vector<std::function<void()>> update_cache(const DatapointSnapshot& snapshot,
                                                    DatapointSource source,
                                                    std::optional<std::uint32_t> seq)
    {
        const auto now = std::chrono::steady_clock::now();
        std::vector<std::function<void()>> notifications;

        std::lock_guard lock(mutex);
        for (const auto& [name, value] : snapshot.values()) {
            const DatapointDescriptor* descriptor = catalog::find(name);
            if (descriptor == nullptr) continue;

            auto& entry = cache[name];
            std::optional<DatapointValue> old;
            const bool had_value = entry.quality != DataQuality::Unknown;
            if (had_value) old = entry.value;
            const bool changed = !had_value || entry.value != value;

            entry.value = value;
            entry.received_at = now;
            entry.source = source;
            entry.quality = DataQuality::Good;
            entry.report_sequence = seq;

            if (!changed) continue;

            AnyDatapointChange change;
            change.descriptor = descriptor;
            change.value = value;
            change.old_value = std::move(old);
            change.source = source;
            change.received_at = now;

            for (const auto& slot : subscriptions) {
                if (slot.name.empty() || slot.name == name) {
                    notifications.push_back(
                        [callback = slot.callback, change] { callback(change); });
                }
            }
        }
        return notifications;
    }
};

DatapointManager::DatapointManager() : impl_(std::make_shared<Impl>()) {}
DatapointManager::~DatapointManager() = default;

void DatapointManager::bind(SubmitFn submit, BlockingRunner runner)
{
    impl_->submit = std::move(submit);
    impl_->runner = std::move(runner);
}

// ---------------------------------------------------------------------------
// Async core
// ---------------------------------------------------------------------------

void DatapointManager::async_read(std::vector<std::string> names,
                                  DatapointSource source,
                                  protocol::OperationPriority priority,
                                  ReadCompletion completion,
                                  std::string coalesce_key)
{
    if (names.empty()) {
        completion(fail(validation_error(
            ErrorCode::EmptySelection,
            "read() needs explicit names — an empty dp_read would only return "
            "the volatile datapoints on the local transport")));
        return;
    }
    for (const auto& name : names) {
        if (catalog::find(name) == nullptr) {
            completion(fail(validation_error(ErrorCode::UnknownDatapoint,
                                             "unknown datapoint '" + name + "'",
                                             name)));
            return;
        }
    }

    protocol::RequestSpec spec;
    spec.type = "dp_read";
    spec.body = {{"names", names}};
    spec.priority = priority;
    spec.coalesce_key = std::move(coalesce_key);

    impl_->submit(
        std::move(spec),
        [this, source, completion = std::move(completion)](
            Result<nlohmann::json> response) {
            if (!response) {
                completion(fail(response.error()));
                return;
            }
            auto snapshot =
                decode_dp_object(response->value("dp", nlohmann::json::object()));
            for (const auto& entry :
                 response->value("unknown", nlohmann::json::array())) {
                if (entry.is_string()) snapshot.add_unknown(entry.get<std::string>());
            }
            auto notify = impl_->update_cache(snapshot, source, std::nullopt);
            for (auto& call : notify) call();
            completion(std::move(snapshot));
        });
}

void DatapointManager::async_read_all(ReadCompletion completion)
{
    // Design concept §12: ALWAYS the full 107 names — names=[] only returned
    // the 56 VOLATILE points on the local transport.
    std::vector<std::string> names;
    names.reserve(kDatapointCount);
    for (const auto& descriptor : catalog::all()) {
        names.emplace_back(descriptor.name);
    }
    async_read(std::move(names), DatapointSource::ExplicitRead,
               protocol::OperationPriority::InteractiveRead, std::move(completion),
               "read_all");
}

void DatapointManager::async_write(const DatapointWriteSet& changes,
                                   WriteCompletion completion)
{
    if (auto status = changes.validate(); !status) {
        completion(fail(status.error()));
        return;
    }
    nlohmann::json dp = nlohmann::json::object();
    for (const auto& [name, value] : changes.values()) {
        dp[name] = value_to_json(*catalog::find(name), value);
    }
    async_write_json(std::move(dp), protocol::OperationPriority::InteractiveControl,
                     std::move(completion));
}

void DatapointManager::async_write_json(nlohmann::json dp_object,
                                        protocol::OperationPriority priority,
                                        WriteCompletion completion)
{
    protocol::RequestSpec spec;
    spec.type = "dp_write";
    spec.body = {{"dp", std::move(dp_object)}};
    spec.priority = priority;

    impl_->submit(std::move(spec),
                  [this, completion = std::move(completion)](
                      Result<nlohmann::json> response) {
                      if (!response) {
                          completion(fail(response.error()));
                          return;
                      }
                      completion(parse_write_result(*response));
                  });
}

WriteResult DatapointManager::parse_write_result(const nlohmann::json& message)
{
    WriteResult result;
    result.status = message.value("status", std::string{});
    if (const auto it = message.find("errors"); it != message.end() && it->is_object()) {
        for (const auto& [name, reason] : it->items()) {
            result.errors.push_back(
                WriteError{name, reason.is_string() ? reason.get<std::string>()
                                                    : reason.dump()});
        }
    }
    // Also pick up single errors ("error":"unknown_command" style).
    if (const auto it = message.find("error"); it != message.end() && it->is_string()) {
        result.errors.push_back(WriteError{"", it->get<std::string>()});
    }
    if (const auto it = message.find("warning"); it != message.end() && it->is_string()) {
        result.warning = it->get<std::string>();
    }
    if (const auto it = message.find("network_save");
        it != message.end() && it->is_number()) {
        result.network_save = static_cast<std::uint8_t>(it->get<int>());
    }

    // Exploit the readback (design concept §11.4): no extra dp_read needed.
    if (const auto it = message.find("readback"); it != message.end() && it->is_object()) {
        result.readback = decode_dp_object(*it);
        auto notify = impl_->update_cache(result.readback,
                                          DatapointSource::WriteReadback,
                                          std::nullopt);
        for (auto& call : notify) call();
    }
    return result;
}

// ---------------------------------------------------------------------------
// Synchronous wrappers
// ---------------------------------------------------------------------------

Result<DatapointSnapshot> DatapointManager::read_names(std::vector<std::string> names)
{
    return detail::sync_wait<DatapointSnapshot>(
        impl_->runner,
        [this, names = std::move(names)](
            std::function<void(Result<DatapointSnapshot>)> done) mutable {
            async_read(std::move(names), DatapointSource::ExplicitRead,
                       protocol::OperationPriority::InteractiveRead,
                       std::move(done));
        });
}

Result<DatapointSnapshot> DatapointManager::read_all()
{
    return detail::sync_wait<DatapointSnapshot>(
        impl_->runner,
        [this](std::function<void(Result<DatapointSnapshot>)> done) {
            async_read_all(std::move(done));
        });
}

Result<DatapointValue> DatapointManager::read(std::string_view name)
{
    auto snapshot = read_names({std::string(name)});
    if (!snapshot) return fail(snapshot.error());
    const DatapointValue* value = snapshot->find(name);
    if (value == nullptr) {
        return fail(validation_error(ErrorCode::UnknownDatapoint,
                                     "device did not return '" + std::string(name) +
                                         "'",
                                     std::string(name)));
    }
    return *value;
}

Result<WriteResult> DatapointManager::write(const DatapointWriteSet& changes)
{
    return detail::sync_wait<WriteResult>(
        impl_->runner, [this, &changes](std::function<void(Result<WriteResult>)> done) {
            async_write(changes, std::move(done));
        });
}

Result<WriteResult> DatapointManager::write(std::string_view name, DatapointValue value)
{
    DatapointWriteSet changes;
    if (auto status = changes.set(name, std::move(value)); !status) {
        return fail(status.error());
    }
    return write(changes);
}

// ---------------------------------------------------------------------------
// Cache
// ---------------------------------------------------------------------------

std::optional<CachedValue> DatapointManager::cached_raw(std::string_view name) const
{
    std::lock_guard lock(impl_->mutex);
    const auto it = impl_->cache.find(name);
    if (it == impl_->cache.end() || it->second.quality == DataQuality::Unknown) {
        return std::nullopt;
    }
    return it->second;
}

DatapointSnapshot DatapointManager::cached_snapshot() const
{
    DatapointSnapshot snapshot;
    std::lock_guard lock(impl_->mutex);
    for (const auto& [name, entry] : impl_->cache) {
        if (entry.quality != DataQuality::Unknown) snapshot.set(name, entry.value);
    }
    return snapshot;
}

// ---------------------------------------------------------------------------
// Staging/Commit
// ---------------------------------------------------------------------------

Status DatapointManager::stage(std::string_view name, DatapointValue value)
{
    const DatapointDescriptor* descriptor = catalog::find(name);
    if (descriptor == nullptr) {
        return fail(validation_error(ErrorCode::UnknownDatapoint,
                                     "unknown datapoint '" + std::string(name) + "'",
                                     std::string(name)));
    }
    if (!descriptor->writable()) {
        return fail(validation_error(ErrorCode::ReadOnlyDatapoint,
                                     std::string(name) + " is read-only",
                                     std::string(name)));
    }
    if (value.index() != value_index_for(descriptor->type)) {
        return fail(validation_error(ErrorCode::TypeMismatch,
                                     std::string(name) + " expects " +
                                         std::string(to_string(descriptor->type)),
                                     std::string(name)));
    }
    std::lock_guard lock(impl_->mutex);
    impl_->staged.insert_or_assign(std::string(name), std::move(value));
    return ok();
}

bool DatapointManager::has_staged_changes() const
{
    std::lock_guard lock(impl_->mutex);
    return !impl_->staged.empty();
}

namespace {

std::optional<double> numeric_of(const DatapointValue& value)
{
    return std::visit(
        [](const auto& typed) -> std::optional<double> {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, std::string>) return std::nullopt;
            else return static_cast<double>(typed);
        },
        value);
}

}  // namespace

Status DatapointManager::validate_staged() const
{
    {
        std::lock_guard lock(impl_->mutex);
        for (const auto& [name, value] : impl_->staged) {
            if (auto status = fountainer::validate(*catalog::find(name), value);
                !status) {
                return status;
            }
        }
    }
    // Cross-field rules: staged values, the rest from the cache.
    return check_cross_constraints([this](std::string_view name)
                                       -> std::optional<double> {
        {
            std::lock_guard lock(impl_->mutex);
            const auto it = impl_->staged.find(std::string(name));
            if (it != impl_->staged.end()) return numeric_of(it->second);
        }
        const auto cached = cached_raw(name);
        return cached ? numeric_of(cached->value) : std::nullopt;
    });
}

Status DatapointManager::validate_constraints(const DatapointWriteSet& changes) const
{
    return check_cross_constraints(
        changes, [this](std::string_view name) -> std::optional<double> {
            const auto cached = cached_raw(name);
            return cached ? numeric_of(cached->value) : std::nullopt;
        });
}

Result<WriteResult> DatapointManager::commit()
{
    DatapointWriteSet changes;
    {
        std::lock_guard lock(impl_->mutex);
        if (impl_->staged.empty()) {
            return fail(validation_error(ErrorCode::EmptySelection,
                                         "no staged changes to commit"));
        }
        for (const auto& [name, value] : impl_->staged) {
            if (auto status = changes.set(name, value); !status) {
                return fail(status.error());
            }
        }
    }
    auto result = write(changes);
    if (result && result->applied()) {
        // Discard only after device confirmation — on "rejected" the staging
        // is kept so the application can correct it.
        discard_staged();
    }
    return result;
}

void DatapointManager::discard_staged()
{
    std::lock_guard lock(impl_->mutex);
    impl_->staged.clear();
}

// ---------------------------------------------------------------------------
// Subscriptions
// ---------------------------------------------------------------------------

Subscription DatapointManager::subscribe_raw(
    std::string_view name, std::function<void(const AnyDatapointChange&)> callback)
{
    std::lock_guard lock(impl_->mutex);
    const std::uint64_t token = ++impl_->next_token;
    impl_->subscriptions.push_back(
        Impl::Slot{token, std::string(name), std::move(callback)});
    // weak: the token may outlive the manager (unsubscribing becomes a no-op).
    return Subscription([weak = std::weak_ptr<Impl>(impl_), token] {
        auto impl = weak.lock();
        if (!impl) return;
        std::lock_guard inner(impl->mutex);
        std::erase_if(impl->subscriptions,
                      [token](const Impl::Slot& slot) { return slot.token == token; });
    });
}

Subscription DatapointManager::subscribe_all(
    std::function<void(const AnyDatapointChange&)> callback)
{
    return subscribe_raw({}, std::move(callback));
}

// ---------------------------------------------------------------------------
// Ingestion
// ---------------------------------------------------------------------------

DatapointSnapshot DatapointManager::ingest_report(const nlohmann::json& dp,
                                                  DatapointSource source,
                                                  std::optional<std::uint32_t> sequence)
{
    auto snapshot = decode_dp_object(dp);
    auto notify = impl_->update_cache(snapshot, source, sequence);
    for (auto& call : notify) call();
    return snapshot;
}

void DatapointManager::mark_all_stale()
{
    std::lock_guard lock(impl_->mutex);
    for (auto& [name, entry] : impl_->cache) {
        (void)name;
        if (entry.quality == DataQuality::Good) entry.quality = DataQuality::Stale;
    }
}

}  // namespace fountainer
