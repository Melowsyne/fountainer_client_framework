// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// DatapointRestoreGuard (design concept §25.4): saves the baseline of a
// writable selection BEFORE a test and restores it when leaving the
// scope — also on the error/exception path (best effort).
//
//   auto guard = DatapointRestoreGuard::capture(client.datapoints(),
//                                              {"Fon_Min_Pressure", ...});
//   if (!guard) ...;              // baseline read failed
//   ... test writes ...
//   auto restored = guard->restore();   // explicit, with result check
//
// Without an explicit restore() the destructor restores; dismiss() ends
// without a restore (e.g. when the values are meant to stay).
#pragma once

#include <string>
#include <vector>

#include <fountainer/datapoints/manager.hpp>

namespace fountainer {

class DatapointRestoreGuard {
public:
    // Reads the baseline IMMEDIATELY (synchronous; not from the IO thread).
    // Non-writable or unknown names are an error — a baseline that
    // cannot be restored would be worthless.
    static Result<DatapointRestoreGuard> capture(DatapointManager& manager,
                                                 std::vector<std::string> names);

    // All writable RW datapoints except the given exclusions
    // (e.g. command points such as Network_Save/Log_Command).
    static Result<DatapointRestoreGuard> capture_all_writable(
        DatapointManager& manager, std::vector<std::string> exclude = {});

    ~DatapointRestoreGuard();

    DatapointRestoreGuard(DatapointRestoreGuard&& other) noexcept;
    DatapointRestoreGuard& operator=(DatapointRestoreGuard&& other) noexcept;
    DatapointRestoreGuard(const DatapointRestoreGuard&) = delete;
    DatapointRestoreGuard& operator=(const DatapointRestoreGuard&) = delete;

    // Write the baseline back atomically; afterwards the guard is disarmed.
    Result<WriteResult> restore();

    // Disarm without a restore.
    void dismiss() noexcept { armed_ = false; }

    [[nodiscard]] bool armed() const noexcept { return armed_; }
    [[nodiscard]] const DatapointSnapshot& baseline() const noexcept
    {
        return baseline_;
    }

private:
    DatapointRestoreGuard(DatapointManager& manager, DatapointSnapshot baseline)
        : manager_(&manager), baseline_(std::move(baseline)), armed_(true)
    {
    }

    DatapointManager* manager_ = nullptr;
    DatapointSnapshot baseline_;
    bool armed_ = false;
};

}  // namespace fountainer
