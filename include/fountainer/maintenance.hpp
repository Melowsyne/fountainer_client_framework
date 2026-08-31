// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Maintenance and diagnostics (design concept §15.2/§17). Dangerous operations
// deliberately do NOT sit next to the normal pump control and carry explicit
// names. Diagnostic commands (wd_fault, link_fault) must be enabled via
// ClientOptions::enable_test_commands.
#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

#include <fountainer/commands.hpp>
#include <fountainer/datapoints/manager.hpp>
#include <fountainer/result.hpp>

namespace fountainer {

// Network_Save values (command datapoint, task_com.c):
//   1 = persist, 2 = reboot, 3 = restore backup, 4 = trial reboot.
enum class NetworkSaveAction : std::uint8_t {
    Persist = 1,
    Reboot = 2,
    RestoreBackup = 3,
    TrialReboot = 4,
};

class DiagnosticsService {
public:
    using EnabledFn = std::function<bool()>;

    void bind(CommandService& commands, EnabledFn enabled);

    // Injects a watchdog fault (blocks the measurement cycle for the
    // given duration). ONLY for device tests.
    Result<CommandResult> inject_watchdog_fault(std::chrono::seconds duration);

    // Forces POOR link for the given duration. ONLY for device tests.
    Result<CommandResult> force_poor_link(std::chrono::seconds duration);

private:
    Result<CommandResult> guarded(const char* command,
                                  std::chrono::seconds duration);

    CommandService* commands_ = nullptr;
    EnabledFn enabled_;
};

class MaintenanceService {
public:
    void bind(DatapointManager& manager, CommandService& commands,
              DiagnosticsService::EnabledFn test_commands_enabled);

    DiagnosticsService& diagnostics() noexcept { return diagnostics_; }

    // Persist the network configuration (Network_Save = 1). The
    // destructive variants (2/3/4) are deliberately named individually.
    Result<WriteResult> persist_network_config();

    // DESTRUCTIVE: the device reboots or runs the trial/restore path.
    Result<WriteResult> network_save_dangerous(NetworkSaveAction action);

private:
    DatapointManager* manager_ = nullptr;
    DiagnosticsService diagnostics_;
};

}  // namespace fountainer
