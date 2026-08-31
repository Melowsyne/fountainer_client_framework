// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Runtime access to the generated datapoint catalog (design concept §12.2).
// Important for read_all(): with names=[] the firmware LOCALLY returns only
// the 56 VOLATILE points — therefore the complete name list is always sent
// (local_protocol.c:cb_local_fill_snapshot).
#pragma once

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

#include <fountainer/datapoints/generated.hpp>

namespace fountainer::catalog {

[[nodiscard]] inline std::span<const DatapointDescriptor> all() noexcept
{
    return {kDatapointDescriptors.data(), kDatapointDescriptors.size()};
}

// nullptr for an unknown name — unknown datapoints are an expected
// condition (newer firmware), not a programming error.
[[nodiscard]] const DatapointDescriptor* find(std::string_view name) noexcept;

[[nodiscard]] inline const DatapointDescriptor& at(std::uint16_t index)
{
    return kDatapointDescriptors.at(index);
}

[[nodiscard]] inline const DatapointDescriptor& at(DatapointId id)
{
    return kDatapointDescriptors.at(static_cast<std::uint16_t>(id));
}

template <AnyDatapoint DP>
[[nodiscard]] constexpr const DatapointDescriptor& at(const DP& datapoint) noexcept
{
    return kDatapointDescriptors[datapoint.index];
}

// All 107 wire names in catalog order.
[[nodiscard]] std::vector<std::string_view> all_names();

[[nodiscard]] std::vector<std::string_view> names_with_access(Access access);
[[nodiscard]] std::vector<std::string_view> names_with_persistence(Persistence p);
[[nodiscard]] std::vector<std::string_view> names_with_poll_class(PollClass p);

}  // namespace fountainer::catalog
