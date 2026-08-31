// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/datapoints/poller.hpp"

#include <algorithm>
#include <mutex>

#include "fountainer/logging/logger.hpp"

namespace fountainer {

namespace {

constexpr const char* kLogCat = "POLLER";

// A dp_read request must stay below the 4096 B frame limit; envelope,
// signature and JSON overhead need room. With names up to 25 characters,
// 100 names ≈ 2.8 KB — 100 as the upper bound is conservative and almost
// always covers the full catalog in a single cycle.
constexpr std::size_t kMaxNamesPerRequest = 100;

}  // namespace

struct DatapointPoller::Impl {
    DatapointManager& manager;

    struct Entry {
        std::chrono::milliseconds interval{};
        TimePoint next_due{};
        bool in_flight = false;
        bool once = false;       // OnConnect: once per (re)start
        bool once_done = false;
    };

    mutable std::mutex mutex;
    std::map<std::string, Entry> entries;
    std::map<PollClass, std::chrono::milliseconds> class_intervals = {
        {PollClass::Realtime, std::chrono::milliseconds(1000)},
        {PollClass::Status, std::chrono::milliseconds(5000)},
        {PollClass::Config, std::chrono::milliseconds(60000)},
    };
    std::chrono::seconds keepalive{240};
    bool running = false;
    bool paused = false;
    bool raster_dirty = true;   // grid is rebuilt after start()/resume()/reconnect
    PollStats stats{};

    explicit Impl(DatapointManager& m) : manager(m) {}

    void reset_raster(TimePoint now)
    {
        for (auto& [name, entry] : entries) {
            (void)name;
            entry.next_due = now;
            entry.in_flight = false;
            if (entry.once) entry.once_done = false;
        }
        raster_dirty = false;
    }
};

DatapointPoller::DatapointPoller(DatapointManager& manager)
    : impl_(std::make_unique<Impl>(manager))
{
}

DatapointPoller::~DatapointPoller() = default;

Status DatapointPoller::every(std::chrono::milliseconds interval,
                              const std::vector<std::string>& names)
{
    for (const auto& name : names) {
        const DatapointDescriptor* descriptor = catalog::find(name);
        if (descriptor == nullptr) {
            return fail(validation_error(ErrorCode::UnknownDatapoint,
                                         "unknown datapoint '" + name + "'", name));
        }
        if (!descriptor->readable()) {
            return fail(validation_error(ErrorCode::ReadOnlyDatapoint,
                                         name + " is not readable", name));
        }
        add(name, interval);
    }
    return ok();
}

void DatapointPoller::add(std::string name, std::chrono::milliseconds interval)
{
    std::lock_guard lock(impl_->mutex);
    auto& entry = impl_->entries[std::move(name)];
    entry.interval = interval;
    entry.once = false;
    impl_->raster_dirty = true;
}

void DatapointPoller::add_once(std::string name)
{
    std::lock_guard lock(impl_->mutex);
    auto& entry = impl_->entries[std::move(name)];
    entry.once = true;
    entry.once_done = false;
    impl_->raster_dirty = true;
}

void DatapointPoller::remove(const std::string& name)
{
    std::lock_guard lock(impl_->mutex);
    impl_->entries.erase(name);
}

void DatapointPoller::set_class_interval(PollClass poll_class,
                                         std::chrono::milliseconds interval)
{
    std::lock_guard lock(impl_->mutex);
    impl_->class_intervals[poll_class] = interval;
}

void DatapointPoller::set_keepalive(std::chrono::seconds interval)
{
    std::lock_guard lock(impl_->mutex);
    impl_->keepalive = interval;
}

void DatapointPoller::start_defaults()
{
    {
        std::lock_guard lock(impl_->mutex);
        for (const auto& descriptor : catalog::all()) {
            switch (descriptor.poll_class) {
            case PollClass::Disabled:
                break;
            case PollClass::OnConnect: {
                auto& entry = impl_->entries[std::string(descriptor.name)];
                entry.once = true;
                entry.once_done = false;
                break;
            }
            default: {
                auto& entry = impl_->entries[std::string(descriptor.name)];
                entry.interval = impl_->class_intervals[descriptor.poll_class];
                entry.once = false;
                break;
            }
            }
        }
        impl_->raster_dirty = true;
    }
    start();
}

void DatapointPoller::start()
{
    std::lock_guard lock(impl_->mutex);
    impl_->running = true;
    impl_->paused = false;
    impl_->raster_dirty = true;
}

void DatapointPoller::pause()
{
    std::lock_guard lock(impl_->mutex);
    impl_->paused = true;
}

void DatapointPoller::resume()
{
    std::lock_guard lock(impl_->mutex);
    impl_->paused = false;
    impl_->raster_dirty = true;
}

void DatapointPoller::stop()
{
    std::lock_guard lock(impl_->mutex);
    impl_->running = false;
}

bool DatapointPoller::running() const noexcept
{
    std::lock_guard lock(impl_->mutex);
    return impl_->running && !impl_->paused;
}

void DatapointPoller::on_reconnected()
{
    std::lock_guard lock(impl_->mutex);
    impl_->raster_dirty = true;   // once points are read again
}

PollStats DatapointPoller::stats() const
{
    std::lock_guard lock(impl_->mutex);
    return impl_->stats;
}

DatapointPoller::TimePoint DatapointPoller::next_deadline(TimePoint now) const
{
    std::lock_guard lock(impl_->mutex);
    if (!impl_->running || impl_->paused) return TimePoint::max();
    TimePoint next = TimePoint::max();
    for (const auto& [name, entry] : impl_->entries) {
        (void)name;
        if (entry.once && entry.once_done) continue;
        next = std::min(next, impl_->raster_dirty ? now : entry.next_due);
    }
    return next;
}

void DatapointPoller::tick(TimePoint now, TimePoint last_tx)
{
    std::vector<std::string> due;
    bool need_keepalive = false;

    {
        std::lock_guard lock(impl_->mutex);
        if (!impl_->running || impl_->paused) return;
        if (impl_->raster_dirty) impl_->reset_raster(now);

        for (auto& [name, entry] : impl_->entries) {
            if (entry.once) {
                if (!entry.once_done && !entry.in_flight) {
                    due.push_back(name);
                    entry.in_flight = true;
                    entry.once_done = true;
                }
                continue;
            }
            if (entry.next_due > now) continue;
            if (entry.in_flight) {
                // No piling up (§13.7): the in-flight request covers the value.
                impl_->stats.skipped_inflight++;
            } else {
                due.push_back(name);
                entry.in_flight = true;
            }
            // Advance the deadline along the GRID; missed cycles are
            // skipped rather than caught up (§13.8).
            auto next = entry.next_due + entry.interval;
            if (next <= now) {
                const auto behind = now - entry.next_due;
                const auto skipped = behind / entry.interval;
                impl_->stats.missed_deadlines +=
                    static_cast<std::uint64_t>(skipped);
                next = entry.next_due + (skipped + 1) * entry.interval;
            }
            entry.next_due = next;
        }

        // Fountain keepalive: only when there is nothing to send anyway and
        // the session has been silent for too long (§21.1).
        if (due.empty() && impl_->keepalive.count() > 0 &&
            now - last_tx >= impl_->keepalive) {
            need_keepalive = true;
        }
    }

    if (need_keepalive) {
        impl_->stats.keepalives++;
        impl_->manager.async_read(
            {"System_Uptime"}, DatapointSource::PeriodicPoll,
            protocol::OperationPriority::Background,
            [](Result<DatapointSnapshot> result) {
                if (!result) {
                    log::debug(kLogCat,
                               "keepalive failed: " + result.error().to_string());
                }
            },
            "keepalive");
        return;
    }

    if (due.empty()) return;

    // Coalescing (§13.4): pack all due points into as few requests as
    // possible.
    {
        std::lock_guard lock(impl_->mutex);
        impl_->stats.cycles++;
        if (due.size() > 1) impl_->stats.coalesced_points += due.size();
    }

    for (std::size_t offset = 0; offset < due.size();
         offset += kMaxNamesPerRequest) {
        const auto end = std::min(due.size(), offset + kMaxNamesPerRequest);
        std::vector<std::string> chunk(due.begin() + offset, due.begin() + end);

        {
            std::lock_guard lock(impl_->mutex);
            impl_->stats.requests++;
        }

        impl_->manager.async_read(
            chunk, DatapointSource::PeriodicPoll, protocol::OperationPriority::Polling,
            [this, chunk](Result<DatapointSnapshot> result) {
                std::lock_guard lock(impl_->mutex);
                for (const auto& name : chunk) {
                    const auto it = impl_->entries.find(name);
                    if (it != impl_->entries.end()) it->second.in_flight = false;
                }
                if (!result) {
                    impl_->stats.failures++;
                    log::debug(kLogCat,
                               "poll read failed: " + result.error().to_string());
                }
            });
    }
}

}  // namespace fountainer
