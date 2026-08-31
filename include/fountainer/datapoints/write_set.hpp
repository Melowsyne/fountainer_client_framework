// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Batch write (design concept §11.3). Matches the atomic semantics of the
// firmware's dp_write_batch(): EITHER all points are applied OR none
// (errors names the ones that were refused).
#pragma once

#include <map>
#include <string>

#include <fountainer/datapoints/catalog.hpp>
#include <fountainer/datapoints/codec.hpp>

namespace fountainer {

class DatapointWriteSet {
public:
    // Typed: only writable datapoints, the value type is enforced.
    template <WritableDatapoint DP>
    DatapointWriteSet& set(const DP& datapoint, typename DP::value_type value)
    {
        auto boxed = make_value(catalog::at(datapoint), std::move(value));
        // Type errors are ruled out here by the concept.
        values_.insert_or_assign(std::string(datapoint.name), std::move(*boxed));
        return *this;
    }

    // Dynamic (tools/GUI): validates name, writability and type.
    Status set(std::string_view name, DatapointValue value);

    [[nodiscard]] bool empty() const noexcept { return values_.empty(); }
    [[nodiscard]] std::size_t size() const noexcept { return values_.size(); }
    void clear() { values_.clear(); }

    [[nodiscard]] const std::map<std::string, DatapointValue>& values() const noexcept
    {
        return values_;
    }

    // Check all values against the catalog constraints (type, min/max,
    // string length). First error wins.
    [[nodiscard]] Status validate() const;

private:
    std::map<std::string, DatapointValue> values_;
};

}  // namespace fountainer
