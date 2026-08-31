#!/usr/bin/env python3
# Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
# SPDX-License-Identifier: MIT
#
# Generates include/fountainer/datapoints/generated.hpp from the firmware's
# SINGLE source of truth (components/datapoints/dp_list.def) plus the
# CLIENT-side poll-policy overlay (tools/client_poll_policy.json).
#
# Why an overlay?  The poll class is a desktop/backend decision and not
# firmware semantics (design concept §13.3) — dp_list.def stays clean.
#
# Invocation:
#   python3 tools/generate_datapoints.py \
#       --dp-list ../fountainer_firmware/src/components/datapoints/dp_list.def
#
# The result is CHECKED IN: the build needs no access to the firmware tree.
# test/test_datapoints.cpp verifies the schema hash.
from __future__ import annotations

import argparse
import hashlib
import json
import math
import re
import sys
from pathlib import Path

DP_RE = re.compile(
    r"^DP\(\s*"
    r"(?P<name>[A-Za-z0-9_]+)\s*,\s*"
    r"(?P<type>[A-Z0-9]+)\s*,\s*"
    r"(?P<access>RO|RW|WO)\s*,\s*"
    r"(?P<persist>VOLATILE|NVS|STATIC)\s*,\s*"
    r"(?P<id>\d+)\s*,\s*"
    r"(?P<default>[^,]+?)\s*,\s*"
    r"(?P<min>[^,]+?)\s*,\s*"
    r"(?P<max>[^,]+?)\s*,\s*"
    r"(?P<deadband>[^)]+?)\s*\)"
)
ANNOTATION_RE = re.compile(r"/\*@(?P<body>.*?)@\*/")

CPP_TYPE = {
    "BOOL": "bool",
    "U8": "std::uint8_t",
    "U16": "std::uint16_t",
    "U32": "std::uint32_t",
    "U64": "std::uint64_t",
    "I8": "std::int8_t",
    "I16": "std::int16_t",
    "I32": "std::int32_t",
    "F32": "float",
    "ENUM": "std::uint8_t",
    "STR": "std::string",
}
ENUM_TYPE = {
    "BOOL": "Bool", "U8": "U8", "U16": "U16", "U32": "U32", "U64": "U64",
    "I8": "I8", "I16": "I16", "I32": "I32", "F32": "F32",
    "ENUM": "Enum", "STR": "Str",
}
ENUM_ACCESS = {"RO": "ReadOnly", "RW": "ReadWrite", "WO": "WriteOnly"}
ENUM_PERSIST = {"VOLATILE": "Volatile", "NVS": "Nvs", "STATIC": "Static"}
POLL_CLASSES = {"Realtime", "Status", "Config", "OnConnect", "Disabled"}


def parse_number(token: str) -> float:
    """dp_list.def number -> float.  'NAN' and '0.05f' are both valid."""
    token = token.strip().rstrip("fF")
    if token.upper() == "NAN":
        return math.nan
    return float(token)


def parse_annotation(line: str) -> dict:
    """/*@ unit=bar dec=2 map=state fmt=datetime @*/ -> dict."""
    match = ANNOTATION_RE.search(line)
    if not match:
        return {}
    out = {}
    for token in match.group("body").split():
        if "=" not in token:
            continue
        key, _, value = token.partition("=")
        out[key] = value
    return out


def parse_dp_list(path: Path) -> list[dict]:
    points = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        line = raw.strip()
        if not line.startswith("DP("):
            continue
        match = DP_RE.match(line)
        if not match:
            raise SystemExit(f"dp_list.def: unparsable line: {line}")
        annotation = parse_annotation(line)
        if match.group("type") not in CPP_TYPE:
            raise SystemExit(f"dp_list.def: unknown type {match.group('type')!r}")
        points.append(
            {
                "name": match.group("name"),
                "type": match.group("type"),
                "access": match.group("access"),
                "persist": match.group("persist"),
                "nvs_id": int(match.group("id")),
                "default": parse_number(match.group("default")),
                "min": parse_number(match.group("min")),
                "max": parse_number(match.group("max")),
                "deadband": parse_number(match.group("deadband")),
                "unit": annotation.get("unit", ""),
                "decimals": int(annotation["dec"]) if "dec" in annotation else -1,
                "map": annotation.get("map", ""),
                "format": annotation.get("fmt", ""),
            }
        )
    return points


def schema_hash(points: list[dict]) -> str:
    """Stable over everything that affects the WIRE contract (without poll policy
    and without UI annotations — those are client cosmetics)."""
    hasher = hashlib.sha256()
    for point in points:
        fields = (
            point["name"], point["type"], point["access"], point["persist"],
            str(point["nvs_id"]), repr(point["min"]), repr(point["max"]),
        )
        hasher.update(("\x1f".join(fields) + "\x1e").encode("utf-8"))
    return hasher.hexdigest()[:32]


def cpp_double(value: float) -> str:
    if math.isnan(value):
        return "kDatapointUnbounded"
    if value == int(value) and abs(value) < 1e15:
        return f"{int(value)}.0"
    return repr(value)


def cpp_string(value: str) -> str:
    escaped = value.replace("\\", "\\\\").replace('"', '\\"')
    return f'"{escaped}"'


def render(points: list[dict], policy: dict, source: str) -> str:
    default_class = policy.get("default", "Status")
    overrides = policy.get("datapoints", {})
    unknown = set(overrides) - {p["name"] for p in points}
    if unknown:
        raise SystemExit(
            "client_poll_policy.json names unknown datapoints: "
            + ", ".join(sorted(unknown))
        )

    lines: list[str] = []
    out = lines.append
    out("// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA")
    out("// SPDX-License-Identifier: MIT")
    out("//")
    out("// GENERATED — DO NOT EDIT BY HAND.")
    out(f"// Source:  {source}")
    out("// Overlay: tools/client_poll_policy.json (client poll policy)")
    out("// Regenerate: python3 tools/generate_datapoints.py --dp-list <path>")
    out("#pragma once")
    out("")
    out("#include <array>")
    out("#include <cstddef>")
    out("#include <cstdint>")
    out("#include <limits>")
    out("#include <string_view>")
    out("")
    out("#include <fountainer/datapoints/datapoint.hpp>")
    out("")
    out("namespace fountainer {")
    out("")
    out("// Changes whenever a datapoint is added, removed, or changes its")
    out("// type/access/value range (UI annotations do not count).")
    out(f'inline constexpr std::string_view kDatapointSchemaHash = "{schema_hash(points)}";')
    out(f"inline constexpr std::size_t kDatapointCount = {len(points)};")
    out("")
    out("inline constexpr double kDatapointUnbounded =")
    out("    std::numeric_limits<double>::quiet_NaN();")
    out("")
    out("// Stable order == order in dp_list.def == index into the")
    out("// descriptor table. Do NOT use as a persistent key —")
    out("// that is what the wire name is for.")
    out("enum class DatapointId : std::uint16_t {")
    for index, point in enumerate(points):
        out(f"    {point['name']} = {index},")
    out("};")
    out("")
    out("inline constexpr std::array<DatapointDescriptor, kDatapointCount>")
    out("    kDatapointDescriptors = {{")
    for index, point in enumerate(points):
        poll = overrides.get(point["name"], default_class)
        if poll not in POLL_CLASSES:
            raise SystemExit(f"unknown poll class {poll!r} for {point['name']}")
        meta = (
            f"{{{cpp_string(point['unit'])}, {point['decimals']}, "
            f"{cpp_double(point['deadband'])}, {cpp_string(point['map'])}, "
            f"{cpp_string(point['format'])}}}"
        )
        out(f"        // [{index:3d}] {point['name']}")
        out(
            f"        {{{index}, {cpp_string(point['name'])}, "
            f"DatapointType::{ENUM_TYPE[point['type']]}, "
            f"Access::{ENUM_ACCESS[point['access']]}, "
            f"Persistence::{ENUM_PERSIST[point['persist']]}, "
            f"{point['nvs_id']}, {cpp_double(point['default'])}, "
            f"{cpp_double(point['min'])}, {cpp_double(point['max'])}, "
            f"{meta}, PollClass::{poll}}},"
        )
    out("    }};")
    out("")
    out("template <typename T, Access A, Persistence P>")
    out("constexpr const DatapointDescriptor&")
    out("Datapoint<T, A, P>::descriptor() const noexcept")
    out("{")
    out("    return kDatapointDescriptors[index];")
    out("}")
    out("")
    out("// Typed constants — the default API (design concept §10.1).")
    out("//   client.datapoints().read(dp::Fon_Current_Pressure)  -> Result<float>")
    out("namespace dp {")
    out("")
    for index, point in enumerate(points):
        cpp_type = CPP_TYPE[point["type"]]
        access = ENUM_ACCESS[point["access"]]
        persist = ENUM_PERSIST[point["persist"]]
        out(
            f"inline constexpr Datapoint<{cpp_type}, Access::{access}, "
            f"Persistence::{persist}>"
        )
        out(f"    {point['name']}{{{index}, {cpp_string(point['name'])}}};")
    out("")
    out("}  // namespace dp")
    out("")
    out("}  // namespace fountainer")
    return "\n".join(lines) + "\n"


def main() -> int:
    repo = Path(__file__).resolve().parent.parent
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--dp-list",
        type=Path,
        default=repo.parent / "fountainer_firmware/src/components/datapoints/dp_list.def",
        help="path to the firmware's dp_list.def",
    )
    parser.add_argument("--policy", type=Path, default=repo / "tools/client_poll_policy.json")
    parser.add_argument(
        "--out", type=Path, default=repo / "include/fountainer/datapoints/generated.hpp"
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="only check whether the checked-in file is up to date (exit 1 otherwise)",
    )
    args = parser.parse_args()

    if not args.dp_list.is_file():
        print(f"dp_list.def not found: {args.dp_list}", file=sys.stderr)
        return 2

    points = parse_dp_list(args.dp_list)
    policy = json.loads(args.policy.read_text(encoding="utf-8"))
    rendered = render(points, policy, args.dp_list.name)

    if args.check:
        current = args.out.read_text(encoding="utf-8") if args.out.is_file() else ""
        if current != rendered:
            print(f"{args.out} is NOT up to date — regenerate.", file=sys.stderr)
            return 1
        print(f"{args.out}: up to date ({len(points)} datapoints)")
        return 0

    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(rendered, encoding="utf-8")
    print(f"{args.out}: {len(points)} datapoints, schema {schema_hash(points)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
