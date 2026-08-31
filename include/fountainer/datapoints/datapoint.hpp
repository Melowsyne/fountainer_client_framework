// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Datapoint core types (design concept §10). The CATALOG itself is generated
// from the firmware source dp_list.def (datapoints/generated.hpp) — this file
// only contains the types that the generator instantiates.
//
// Wire contract (verified against datapoints.c:dp_value_to_json):
//   BOOL -> JSON bool          U8/ENUM/I8/U16/I16/U32/I32/F32 -> JSON number
//   U64  -> 16-digit HEX STRING (!)       STR -> JSON string, < 64 bytes
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <type_traits>

namespace fountainer {

enum class DatapointType : std::uint8_t {
    Bool, U8, U16, U32, U64, I8, I16, I32, F32, Enum, Str,
};

enum class Access : std::uint8_t { ReadOnly, ReadWrite, WriteOnly };

enum class Persistence : std::uint8_t { Volatile, Nvs, Static };

// CLIENT policy, not firmware semantics (design concept §13.3). Source:
// tools/client_poll_policy.json — deliberately NOT in dp_list.def.
// (GCC falsely reports -Wshadow against the global `Status` alias —
// scoped-enum enumerators do not shadow anything.)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
enum class PollClass : std::uint8_t { Realtime, Status, Config, OnConnect, Disabled };
#pragma GCC diagnostic pop

std::string_view to_string(DatapointType type) noexcept;
std::string_view to_string(Access access) noexcept;
std::string_view to_string(Persistence persistence) noexcept;
std::string_view to_string(PollClass poll_class) noexcept;

// Maximum usable length of a STR datapoint. The firmware reserves
// DP_STR_MAX = 64 bytes including NUL and rejects longer values with
// "too_long" (datapoints.c:dp_value_from_json).
inline constexpr std::size_t kDatapointStringMax = 63;

// UI/presentation metadata from the /*@ ... @*/ annotations in
// dp_list.def. Purely informational — the firmware validates independently.
struct DatapointMetadata {
    std::string_view unit;       // "bar", "°C", "s", ... ("" = no unit)
    int decimals = -1;           // -1 = no recommendation
    double deadband = 0.0;       // analog on-change threshold
    std::string_view value_map;  // "state", "reset", "power", ...
    std::string_view format;     // e.g. "datetime"
};

// Runtime description of a datapoint (generated constexpr table).
struct DatapointDescriptor {
    std::uint16_t index;          // position in the catalog == DatapointId
    std::string_view name;        // wire name, identical to the firmware
    DatapointType type;
    Access access;
    Persistence persistence;
    std::uint16_t nvs_id;         // 0 for VOLATILE/STATIC
    double default_value;
    double min;                   // NaN = unbounded
    double max;                   // NaN = unbounded
    DatapointMetadata metadata;
    PollClass poll_class;

    [[nodiscard]] constexpr bool writable() const noexcept
    {
        return access == Access::ReadWrite || access == Access::WriteOnly;
    }
    [[nodiscard]] constexpr bool readable() const noexcept
    {
        return access != Access::WriteOnly;
    }
    [[nodiscard]] constexpr bool numeric() const noexcept
    {
        return type != DatapointType::Bool && type != DatapointType::Str &&
               type != DatapointType::U64;
    }
};

// Typed constant. Deliberately carries only identity — constraints and
// metadata come via descriptor() from the single generated table
// (no second, drifting data set).
template <typename T, Access A, Persistence P>
struct Datapoint {
    using value_type = T;

    static constexpr Access access = A;
    static constexpr Persistence persistence = P;

    std::uint16_t index;
    std::string_view name;

    [[nodiscard]] constexpr const DatapointDescriptor& descriptor() const noexcept;
};

// Compile-time protection against writes to RO points (design concept §10.4).
template <typename DP>
concept AnyDatapoint = requires {
    typename DP::value_type;
    { DP::access } -> std::convertible_to<Access>;
};

template <typename DP>
concept WritableDatapoint =
    AnyDatapoint<DP> &&
    (DP::access == Access::ReadWrite || DP::access == Access::WriteOnly);

template <typename DP>
concept ReadableDatapoint = AnyDatapoint<DP> && (DP::access != Access::WriteOnly);

// Maps a wire type to the C++ value type — used by the generator and
// usable by generic application code.
template <DatapointType Type>
struct datapoint_value_type;

template <> struct datapoint_value_type<DatapointType::Bool> { using type = bool; };
template <> struct datapoint_value_type<DatapointType::U8>   { using type = std::uint8_t; };
template <> struct datapoint_value_type<DatapointType::U16>  { using type = std::uint16_t; };
template <> struct datapoint_value_type<DatapointType::U32>  { using type = std::uint32_t; };
template <> struct datapoint_value_type<DatapointType::U64>  { using type = std::uint64_t; };
template <> struct datapoint_value_type<DatapointType::I8>   { using type = std::int8_t; };
template <> struct datapoint_value_type<DatapointType::I16>  { using type = std::int16_t; };
template <> struct datapoint_value_type<DatapointType::I32>  { using type = std::int32_t; };
template <> struct datapoint_value_type<DatapointType::F32>  { using type = float; };
template <> struct datapoint_value_type<DatapointType::Enum> { using type = std::uint8_t; };
template <> struct datapoint_value_type<DatapointType::Str>  { using type = std::string; };

template <DatapointType Type>
using datapoint_value_type_t = typename datapoint_value_type<Type>::type;

}  // namespace fountainer
