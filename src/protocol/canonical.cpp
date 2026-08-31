// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/protocol/canonical.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace fountainer::protocol {

namespace {

void serialize_into(const nlohmann::json& value, std::string& out);

}  // namespace

std::string escape_json_string(std::string_view s)
{
    // Minimal escapes like Python ensure_ascii=False: only ", \ and
    // control characters < 0x20 (with the usual short forms). UTF-8 unchanged.
    std::string out;
    out.reserve(s.size() + 2);
    for (const char c : s) {
        switch (c) {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b"; break;
        case '\t': out += "\\t"; break;
        case '\n': out += "\\n"; break;
        case '\f': out += "\\f"; break;
        case '\r': out += "\\r"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                char buf[8];
                std::snprintf(buf, sizeof buf, "\\u%04x",
                              static_cast<unsigned>(static_cast<unsigned char>(c)));
                out += buf;
            } else {
                out += c;
            }
        }
    }
    return out;
}

std::string canonical_number(double value)
{
    if (!std::isfinite(value)) {
        throw std::runtime_error("canonical JSON: non-finite number");
    }
    // The device's cJSON rule: integral doubles without a fractional part.
    if (value == std::trunc(value) &&
        value >= -9.2233720368547758e18 && value <= 9.2233720368547758e18) {
        return std::to_string(static_cast<std::int64_t>(value));
    }
    // Shortest round-trip representation like Python repr: try 15, 16, then
    // 17 significant digits until strtod yields the exact value.
    char buf[32];
    for (int precision = 15; precision <= 17; precision++) {
        std::snprintf(buf, sizeof buf, "%.*g", precision, value);
        if (std::strtod(buf, nullptr) == value) {
            break;
        }
    }
    return buf;
}

namespace {

void serialize_into(const nlohmann::json& value, std::string& out)
{
    using nlohmann::json;
    switch (value.type()) {
    case json::value_t::null:
        out += "null";
        break;
    case json::value_t::boolean:
        out += value.get<bool>() ? "true" : "false";
        break;
    case json::value_t::number_integer:
        out += std::to_string(value.get<std::int64_t>());
        break;
    case json::value_t::number_unsigned:
        out += std::to_string(value.get<std::uint64_t>());
        break;
    case json::value_t::number_float:
        out += canonical_number(value.get<double>());
        break;
    case json::value_t::string:
        out += '"';
        out += escape_json_string(value.get_ref<const std::string&>());
        out += '"';
        break;
    case json::value_t::array: {
        out += '[';
        bool first = true;
        for (const auto& element : value) {
            if (!first) {
                out += ',';
            }
            first = false;
            serialize_into(element, out);
        }
        out += ']';
        break;
    }
    case json::value_t::object: {
        // nlohmann::json (std::map) iterates keys lexicographically — that IS
        // the sort_keys order (bytewise UTF-8 == code point order).
        out += '{';
        bool first = true;
        for (auto it = value.begin(); it != value.end(); ++it) {
            if (!first) {
                out += ',';
            }
            first = false;
            out += '"';
            out += escape_json_string(it.key());
            out += "\":";
            serialize_into(it.value(), out);
        }
        out += '}';
        break;
    }
    default:
        throw std::runtime_error("canonical JSON: unsupported value type");
    }
}

}  // namespace

std::string canonical_serialize(const nlohmann::json& value)
{
    std::string out;
    serialize_into(value, out);
    return out;
}

}  // namespace fountainer::protocol
