// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/datapoints/restore_guard.hpp"

#include <algorithm>

#include "fountainer/logging/logger.hpp"

namespace fountainer {

namespace {
constexpr const char* kLogCat = "RESTORE";
}

Result<DatapointRestoreGuard> DatapointRestoreGuard::capture(
    DatapointManager& manager, std::vector<std::string> names)
{
    if (names.empty()) {
        return fail(validation_error(ErrorCode::EmptySelection,
                                     "restore guard needs at least one datapoint"));
    }
    for (const auto& name : names) {
        const DatapointDescriptor* descriptor = catalog::find(name);
        if (descriptor == nullptr) {
            return fail(validation_error(ErrorCode::UnknownDatapoint,
                                         "unknown datapoint '" + name + "'", name));
        }
        if (descriptor->access != Access::ReadWrite) {
            // WO would not be readable, RO not restorable.
            return fail(validation_error(
                ErrorCode::ReadOnlyDatapoint,
                name + " is not read-write — baseline cannot be restored", name));
        }
    }

    auto baseline = manager.read_names(std::move(names));
    if (!baseline) return fail(baseline.error());
    return DatapointRestoreGuard(manager, std::move(*baseline));
}

Result<DatapointRestoreGuard> DatapointRestoreGuard::capture_all_writable(
    DatapointManager& manager, std::vector<std::string> exclude)
{
    std::vector<std::string> names;
    for (const auto& descriptor : catalog::all()) {
        if (descriptor.access != Access::ReadWrite) continue;
        const std::string name(descriptor.name);
        if (std::find(exclude.begin(), exclude.end(), name) != exclude.end()) {
            continue;
        }
        names.push_back(name);
    }
    return capture(manager, std::move(names));
}

DatapointRestoreGuard::~DatapointRestoreGuard()
{
    if (!armed_) return;
    auto result = restore();
    if (!result) {
        log::error(kLogCat, "baseline restore FAILED: " + result.error().to_string());
    } else if (!result->applied()) {
        log::error(kLogCat, "baseline restore REJECTED by the device");
    }
}

DatapointRestoreGuard::DatapointRestoreGuard(DatapointRestoreGuard&& other) noexcept
    : manager_(other.manager_),
      baseline_(std::move(other.baseline_)),
      armed_(other.armed_)
{
    other.armed_ = false;
}

DatapointRestoreGuard& DatapointRestoreGuard::operator=(
    DatapointRestoreGuard&& other) noexcept
{
    if (this != &other) {
        if (armed_) {
            auto result = restore();
            if (!result) {
                log::error(kLogCat,
                           "baseline restore FAILED: " + result.error().to_string());
            }
        }
        manager_ = other.manager_;
        baseline_ = std::move(other.baseline_);
        armed_ = other.armed_;
        other.armed_ = false;
    }
    return *this;
}

Result<WriteResult> DatapointRestoreGuard::restore()
{
    armed_ = false;
    DatapointWriteSet changes;
    for (const auto& [name, value] : baseline_.values()) {
        if (auto status = changes.set(name, value); !status) {
            return fail(status.error());
        }
    }
    return manager_->write(changes);
}

}  // namespace fountainer
