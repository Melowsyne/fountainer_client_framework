// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/datapoints/codec.hpp"

#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>

namespace fountainer {
namespace {

Error type_mismatch(const DatapointDescriptor& descriptor, std::string detail)
{
    return validation_error(ErrorCode::TypeMismatch,
                            std::string(descriptor.name) + ": " + std::move(detail),
                            std::string(descriptor.name));
}

// U64 arrives as "000001C0C01FA82A"; when writing, the firmware also
// accepts decimal (strtoull with base 0).
Result<std::uint64_t> parse_u64(const DatapointDescriptor& descriptor,
                                const std::string& text)
{
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, 16);
    if (end == text.c_str() || (end != nullptr && *end != '\0') || errno == ERANGE) {
        return fail(type_mismatch(descriptor, "not a 64-bit hex value: '" + text + "'"));
    }
    return static_cast<std::uint64_t>(parsed);
}

// Integral wire types: JSON number -> exact C++ type, without silent
// truncation (a 300 in a U8 is an error, not a 44).
template <typename T>
Result<DatapointValue> narrow(const DatapointDescriptor& descriptor, double number)
{
    if (!std::isfinite(number)) {
        return fail(type_mismatch(descriptor, "value is not finite"));
    }
    const double rounded = std::nearbyint(number);
    if (rounded < static_cast<double>(std::numeric_limits<T>::lowest()) ||
        rounded > static_cast<double>(std::numeric_limits<T>::max())) {
        return fail(validation_error(
            ErrorCode::OutOfRange,
            std::string(descriptor.name) + ": " + std::to_string(number) +
                " does not fit into " + std::string(to_string(descriptor.type)),
            std::string(descriptor.name)));
    }
    return DatapointValue{static_cast<T>(rounded)};
}

}  // namespace

std::size_t value_index_for(DatapointType type) noexcept
{
    switch (type) {
    case DatapointType::Bool: return 0;
    case DatapointType::U8:   return 1;
    case DatapointType::Enum: return 1;   // ENUM is a U8 on the wire
    case DatapointType::U16:  return 2;
    case DatapointType::U32:  return 3;
    case DatapointType::U64:  return 4;
    case DatapointType::I8:   return 5;
    case DatapointType::I16:  return 6;
    case DatapointType::I32:  return 7;
    case DatapointType::F32:  return 8;
    case DatapointType::Str:  return 9;
    }
    return 9;
}

Result<DatapointValue> value_from_json(const DatapointDescriptor& descriptor,
                                       const nlohmann::json& json)
{
    switch (descriptor.type) {
    case DatapointType::Bool:
        if (!json.is_boolean()) return fail(type_mismatch(descriptor, "expected bool"));
        return DatapointValue{json.get<bool>()};

    case DatapointType::Str:
        if (!json.is_string()) return fail(type_mismatch(descriptor, "expected string"));
        return DatapointValue{json.get<std::string>()};

    case DatapointType::U64: {
        if (!json.is_string()) {
            return fail(type_mismatch(descriptor, "expected hex string (U64)"));
        }
        auto parsed = parse_u64(descriptor, json.get<std::string>());
        if (!parsed) return fail(parsed.error());
        return DatapointValue{*parsed};
    }

    default:
        break;
    }

    if (!json.is_number()) return fail(type_mismatch(descriptor, "expected number"));
    const double number = json.get<double>();

    switch (descriptor.type) {
    case DatapointType::U8:
    case DatapointType::Enum: return narrow<std::uint8_t>(descriptor, number);
    case DatapointType::U16:  return narrow<std::uint16_t>(descriptor, number);
    case DatapointType::U32:  return narrow<std::uint32_t>(descriptor, number);
    case DatapointType::I8:   return narrow<std::int8_t>(descriptor, number);
    case DatapointType::I16:  return narrow<std::int16_t>(descriptor, number);
    case DatapointType::I32:  return narrow<std::int32_t>(descriptor, number);
    case DatapointType::F32:  return DatapointValue{static_cast<float>(number)};
    default:                  return fail(type_mismatch(descriptor, "unsupported type"));
    }
}

nlohmann::json value_to_json(const DatapointDescriptor& descriptor,
                             const DatapointValue& value)
{
    if (descriptor.type == DatapointType::U64) {
        // Uppercase hex, 16 digits — identical to the firmware.
        char buffer[17];
        std::snprintf(buffer, sizeof buffer, "%016llX",
                      static_cast<unsigned long long>(std::get<std::uint64_t>(value)));
        return nlohmann::json(std::string(buffer));
    }
    return std::visit(
        [](const auto& typed) -> nlohmann::json {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, std::uint8_t> ||
                          std::is_same_v<T, std::int8_t>) {
                // otherwise nlohmann would turn it into a character
                return nlohmann::json(static_cast<int>(typed));
            } else {
                return nlohmann::json(typed);
            }
        },
        value);
}

Status validate(const DatapointDescriptor& descriptor, const DatapointValue& value)
{
    if (value.index() != value_index_for(descriptor.type)) {
        return fail(type_mismatch(
            descriptor, "value type does not match " +
                            std::string(to_string(descriptor.type))));
    }

    if (const auto* text = std::get_if<std::string>(&value)) {
        if (text->size() > kDatapointStringMax) {
            return fail(validation_error(
                ErrorCode::ValueTooLong,
                std::string(descriptor.name) + ": " + std::to_string(text->size()) +
                    " bytes exceed the device limit of " +
                    std::to_string(kDatapointStringMax),
                std::string(descriptor.name)));
        }
        return ok();
    }

    if (!descriptor.numeric()) return ok();   // BOOL/U64: no bounds

    const double number = std::visit(
        [](const auto& typed) -> double {
            using T = std::decay_t<decltype(typed)>;
            if constexpr (std::is_same_v<T, std::string>) {
                return 0.0;
            } else {
                return static_cast<double>(typed);
            }
        },
        value);

    // The firmware holds min/max as FLOAT (dp_desc_t) — the client must
    // compare at the same precision, otherwise it rejects valid limit
    // values (float(0.01) < double(0.01), found live on the device).
    const double min_bound = static_cast<double>(static_cast<float>(descriptor.min));
    const double max_bound = static_cast<double>(static_cast<float>(descriptor.max));

    if (!std::isnan(min_bound) && number < min_bound) {
        return fail(validation_error(
            ErrorCode::OutOfRange,
            std::string(descriptor.name) + ": " + std::to_string(number) +
                " < min " + std::to_string(descriptor.min),
            std::string(descriptor.name)));
    }
    if (!std::isnan(max_bound) && number > max_bound) {
        return fail(validation_error(
            ErrorCode::OutOfRange,
            std::string(descriptor.name) + ": " + std::to_string(number) +
                " > max " + std::to_string(descriptor.max),
            std::string(descriptor.name)));
    }
    return ok();
}

std::string to_display_string(const DatapointDescriptor& descriptor,
                              const DatapointValue& value)
{
    std::string out;
    if (const auto* flag = std::get_if<bool>(&value)) {
        out = *flag ? "true" : "false";
    } else if (const auto* text = std::get_if<std::string>(&value)) {
        out = *text;
    } else if (const auto* wide = std::get_if<std::uint64_t>(&value)) {
        char buffer[17];
        std::snprintf(buffer, sizeof buffer, "%016llX",
                      static_cast<unsigned long long>(*wide));
        out = buffer;
    } else if (const auto* real = std::get_if<float>(&value)) {
        char buffer[32];
        const int decimals = descriptor.metadata.decimals >= 0
                                 ? descriptor.metadata.decimals
                                 : 3;
        std::snprintf(buffer, sizeof buffer, "%.*f", decimals,
                      static_cast<double>(*real));
        out = buffer;
    } else {
        out = std::visit(
            [](const auto& typed) -> std::string {
                using T = std::decay_t<decltype(typed)>;
                if constexpr (std::is_same_v<T, std::string>) {
                    return typed;
                } else if constexpr (std::is_signed_v<T>) {
                    return std::to_string(static_cast<long long>(typed));
                } else {
                    return std::to_string(static_cast<unsigned long long>(typed));
                }
            },
            value);
    }

    if (!descriptor.metadata.unit.empty()) {
        out += ' ';
        out.append(descriptor.metadata.unit);
    }
    return out;
}

}  // namespace fountainer
