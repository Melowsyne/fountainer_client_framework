// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Conversion between wire JSON and typed datapoint values.
// The codec is the ONLY place that knows the wire contract:
//
//   BOOL              -> JSON true/false          (not 0/1!)
//   U8/ENUM/I8/...    -> JSON number
//   U64               -> 16-digit HEX STRING      (datapoints.c)
//   STR               -> JSON string, < 64 bytes
//
// Client-side validation is a pre-check for fast feedback — the firmware
// always remains authoritative (design concept §11.7).
#pragma once

#include <cstdint>
#include <string>
#include <variant>

#include <nlohmann/json.hpp>

#include <fountainer/datapoints/datapoint.hpp>
#include <fountainer/result.hpp>

namespace fountainer {

// Dynamic value for tools/GUI/backend (design concept §10.5). The typed
// API remains the default path.
using DatapointValue = std::variant<bool, std::uint8_t, std::uint16_t,
                                    std::uint32_t, std::uint64_t, std::int8_t,
                                    std::int16_t, std::int32_t, float,
                                    std::string>;

// Which variant alternative does the descriptor assign to this datapoint?
[[nodiscard]] std::size_t value_index_for(DatapointType type) noexcept;

// Wire JSON -> value. Error: TypeMismatch (wrong JSON kind or unparsable
// U64 string).  Range limits are NOT checked here — values that were read
// may lie outside the range (e.g. after a firmware update).
[[nodiscard]] Result<DatapointValue> value_from_json(
    const DatapointDescriptor& descriptor, const nlohmann::json& json);

// Value -> wire JSON. Assumes that the value matches the descriptor
// (call validate() beforehand).
[[nodiscard]] nlohmann::json value_to_json(const DatapointDescriptor& descriptor,
                                           const DatapointValue& value);

// Check type, value range and string length against the descriptor.
[[nodiscard]] Status validate(const DatapointDescriptor& descriptor,
                              const DatapointValue& value);

// Human-readable for CLI/logs (uses unit/decimals from the annotations).
[[nodiscard]] std::string to_display_string(const DatapointDescriptor& descriptor,
                                            const DatapointValue& value);

// Lift a typed value into the variant; checks that the C++ type matches
// the datapoint's wire type.
template <typename T>
[[nodiscard]] Result<DatapointValue> make_value(const DatapointDescriptor& descriptor,
                                                T value)
{
    DatapointValue boxed{};
    if constexpr (std::is_same_v<T, std::string> ||
                  std::is_convertible_v<T, std::string_view>) {
        boxed = std::string(value);
    } else {
        boxed = value;
    }
    if (boxed.index() != value_index_for(descriptor.type)) {
        return fail(validation_error(
            ErrorCode::TypeMismatch,
            std::string(descriptor.name) + " expects " +
                std::string(to_string(descriptor.type)),
            std::string(descriptor.name)));
    }
    return boxed;
}

// Extract the value from the variant — returns TypeMismatch instead of throwing.
template <typename T>
[[nodiscard]] Result<T> value_cast(const DatapointValue& value)
{
    if (const T* typed = std::get_if<T>(&value)) return *typed;
    return fail(validation_error(ErrorCode::TypeMismatch,
                                 "datapoint value has a different wire type"));
}

}  // namespace fountainer
