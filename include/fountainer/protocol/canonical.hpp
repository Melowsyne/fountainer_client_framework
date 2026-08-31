// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Canonical JSON serialization for the MAC computation — byte-identical to
// Python json.dumps(body, sort_keys=True, separators=(",",":"),
// ensure_ascii=False) AND to the device's cJSON re-print:
//   - object keys sorted lexicographically (nlohmann::json uses std::map,
//     so it already iterates in sorted order — NEVER switch to ordered_json!)
//   - compact separators, UTF-8 unchanged
//   - integral floats as integers ("10" instead of "10.0" — cJSON rule)
//   - otherwise the shortest round-trip representation (probe 15->17
//     significant digits, like Python repr)
#pragma once

#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace fountainer::protocol {

std::string canonical_serialize(const nlohmann::json& value);

// Individually testable:
std::string canonical_number(double value);
std::string escape_json_string(std::string_view s);

}  // namespace fountainer::protocol
