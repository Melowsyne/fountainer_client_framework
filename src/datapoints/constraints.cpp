// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/datapoints/constraints.hpp"

#include <cmath>

namespace fountainer {

namespace {

Error violation(std::string_view field, std::string rule)
{
    Error error = validation_error(ErrorCode::ConstraintViolation,
                                   std::move(rule), std::string(field));
    return error;
}

}  // namespace

Status check_cross_constraints(const ConstraintValueLookup& value_of)
{
    const auto get = [&](std::string_view name) { return value_of(name); };

    const auto alert_low = get("Fon_Alert_Low_Pressure");
    const auto min_pressure = get("Fon_Min_Pressure");
    const auto max_pressure = get("Fon_Max_Pressure");
    const auto alert_high = get("Fon_Alert_High_Pressure");

    if (min_pressure && max_pressure && !(*min_pressure < *max_pressure)) {
        return fail(violation("Fon_Max_Pressure",
                              "Fon_Min_Pressure must be < Fon_Max_Pressure"));
    }
    if (max_pressure && alert_high && !(*max_pressure < *alert_high)) {
        return fail(violation(
            "Fon_Alert_High_Pressure",
            "Fon_Max_Pressure must be < Fon_Alert_High_Pressure"));
    }
    if (alert_low && min_pressure && !(*alert_low < *min_pressure)) {
        return fail(violation(
            "Fon_Alert_Low_Pressure",
            "Fon_Alert_Low_Pressure must be < Fon_Min_Pressure"));
    }

    const auto max_on = get("Fon_Max_On_Time");
    if (max_on && *max_on < 10.0) {
        return fail(violation("Fon_Max_On_Time", "Fon_Max_On_Time must be >= 10"));
    }
    if (const auto min_on = get("Fon_Min_On_Time");
        min_on && max_on && !(*min_on < *max_on)) {
        return fail(violation("Fon_Min_On_Time",
                              "Fon_Min_On_Time must be < Fon_Max_On_Time"));
    }
    if (const auto dry_run = get("Fon_Dry_Run_Detect_Time");
        dry_run && max_on && !(*dry_run < *max_on)) {
        return fail(
            violation("Fon_Dry_Run_Detect_Time",
                      "Fon_Dry_Run_Detect_Time must be < Fon_Max_On_Time"));
    }

    if (const auto report = get("Fon_Report_Interval");
        report && (*report < 1.0 || *report > 3600.0)) {
        return fail(violation("Fon_Report_Interval",
                              "Fon_Report_Interval must be within 1..3600"));
    }
    return ok();
}

Status check_cross_constraints(const DatapointWriteSet& changes,
                               const ConstraintValueLookup& fallback)
{
    return check_cross_constraints(
        [&](std::string_view name) -> std::optional<double> {
            const auto it = changes.values().find(std::string(name));
            if (it != changes.values().end()) {
                return std::visit(
                    [](const auto& typed) -> std::optional<double> {
                        using T = std::decay_t<decltype(typed)>;
                        if constexpr (std::is_same_v<T, std::string>) {
                            return std::nullopt;
                        } else {
                            return static_cast<double>(typed);
                        }
                    },
                    it->second);
            }
            return fallback ? fallback(name) : std::nullopt;
        });
}

}  // namespace fountainer
