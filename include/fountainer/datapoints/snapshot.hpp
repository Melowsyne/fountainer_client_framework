// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Value collections and cache types of the DatapointManager (design concept §11.5).
#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <fountainer/datapoints/codec.hpp>
#include <fountainer/datapoints/datapoint.hpp>

namespace fountainer {

// Where does a cache value come from?
enum class DatapointSource : std::uint8_t {
    ExplicitRead,        // read()/read_all() by the application
    PeriodicPoll,        // DatapointPoller
    UnsolicitedReport,   // push dp_report from the device
    WriteReadback,       // readback from dp_write_result
};

enum class DataQuality : std::uint8_t {
    Unknown,   // never received yet
    Good,      // fresh
    Stale,     // connection was lost since then (design concept §21.3)
    Invalid,   // value did not match the catalog
};

std::string_view to_string(DatapointSource source) noexcept;
std::string_view to_string(DataQuality quality) noexcept;

struct CachedValue {
    DatapointValue value{};
    std::chrono::steady_clock::time_point received_at{};
    DatapointSource source = DatapointSource::ExplicitRead;
    DataQuality quality = DataQuality::Unknown;
    std::optional<std::uint32_t> report_sequence;
};

// Typed view of a cache entry.
template <typename T>
struct CachedDatapoint {
    T value;
    std::chrono::steady_clock::time_point received_at;
    DatapointSource source;
    DataQuality quality;
    std::optional<std::uint32_t> report_sequence;
};

// Result of a (batch) read: wire name -> value. Unknown or undecodable
// names are collected separately instead of discarding the whole read.
class DatapointSnapshot {
public:
    using Map = std::map<std::string, DatapointValue, std::less<>>;

    void set(std::string name, DatapointValue value)
    {
        values_.insert_or_assign(std::move(name), std::move(value));
    }
    void add_unknown(std::string name) { unknown_.push_back(std::move(name)); }

    [[nodiscard]] bool contains(std::string_view name) const
    {
        return values_.find(name) != values_.end();
    }

    [[nodiscard]] const DatapointValue* find(std::string_view name) const
    {
        const auto it = values_.find(name);
        return it == values_.end() ? nullptr : &it->second;
    }

    template <ReadableDatapoint DP>
    [[nodiscard]] Result<typename DP::value_type> get(const DP& datapoint) const
    {
        const DatapointValue* value = find(datapoint.name);
        if (value == nullptr) {
            return fail(validation_error(ErrorCode::UnknownDatapoint,
                                         std::string(datapoint.name) +
                                             " is not part of this snapshot",
                                         std::string(datapoint.name)));
        }
        return value_cast<typename DP::value_type>(*value);
    }

    [[nodiscard]] const Map& values() const noexcept { return values_; }
    [[nodiscard]] const std::vector<std::string>& unknown() const noexcept
    {
        return unknown_;
    }
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }

private:
    Map values_;
    std::vector<std::string> unknown_;
};

// One error entry from dp_write_result.errors ({"Foo":"out_of_range"}).
struct WriteError {
    std::string datapoint;
    std::string reason;
};

// Result of a dp_write (design concept §19.3). Transport success + remote
// rejection is the NORMAL CASE of a validation violation — hence no
// Error, but applied() == false.
struct WriteResult {
    std::string status;                       // "applied" | "rejected"
    std::vector<WriteError> errors;
    std::optional<std::string> warning;       // "nvs_save_failed"
    std::optional<std::uint8_t> network_save; // executed Network_Save
    DatapointSnapshot readback;

    [[nodiscard]] bool applied() const noexcept { return status == "applied"; }
};

}  // namespace fountainer
