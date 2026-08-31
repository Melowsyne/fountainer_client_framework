// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Cross-field constraints — a MIRROR of the firmware's dp_constraints_ok()
// (datapoints.c §9, spec §11.2). Pure pre-validation for fast feedback in
// editors; the firmware ALWAYS remains authoritative (§11.7).
// When rules change, reconcile with the firmware FIRST.
//
//   Fon_Alert_Low_Pressure < Fon_Min_Pressure < Fon_Max_Pressure
//                                             < Fon_Alert_High_Pressure
//   Fon_Max_On_Time >= 10
//   Fon_Min_On_Time        < Fon_Max_On_Time
//   Fon_Dry_Run_Detect_Time < Fon_Max_On_Time
//   1 <= Fon_Report_Interval <= 3600
#pragma once

#include <functional>
#include <optional>
#include <string_view>

#include <fountainer/datapoints/write_set.hpp>

namespace fountainer {

// Returns the EFFECTIVE numeric value of a datapoint for the check
// (planned value, otherwise the current one). nullopt = unknown — rules
// with unknown operands are skipped (the firmware checks them anyway).
using ConstraintValueLookup =
    std::function<std::optional<double>(std::string_view name)>;

// First rule violation as ConstraintViolation (Error.datapoint = the field
// the firmware would name).
[[nodiscard]] Status check_cross_constraints(const ConstraintValueLookup& value_of);

// Convenience: planned changes + fallback (e.g. the manager cache).
[[nodiscard]] Status check_cross_constraints(const DatapointWriteSet& changes,
                                             const ConstraintValueLookup& fallback);

}  // namespace fountainer
