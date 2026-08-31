// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/maintenance.hpp"

#include <fountainer/datapoints/generated.hpp>

namespace fountainer {

void DiagnosticsService::bind(CommandService& commands, EnabledFn enabled)
{
    commands_ = &commands;
    enabled_ = std::move(enabled);
}

Result<CommandResult> DiagnosticsService::guarded(const char* command,
                                                  std::chrono::seconds duration)
{
    if (commands_ == nullptr) {
        return fail(internal_error("DiagnosticsService is not bound to a client"));
    }
    if (!enabled_ || !enabled_()) {
        return fail(make_error(
            ErrorDomain::Configuration, ErrorCode::InvalidState,
            std::string(command) +
                " is a device test command — enable it explicitly via "
                "ClientOptions::enable_test_commands"));
    }
    if (duration <= std::chrono::seconds::zero()) {
        return fail(validation_error(ErrorCode::OutOfRange,
                                     "duration must be positive"));
    }

    // Diagnostic commands carry the duration directly in seconds via
    // duration_steps (task_com_apply_command/link_fault, command.c/wd_fault).
    return commands_->raw(command, std::nullopt,
                          static_cast<std::uint32_t>(duration.count()),
                          protocol::OperationPriority::InteractiveControl);
}

Result<CommandResult> DiagnosticsService::inject_watchdog_fault(
    std::chrono::seconds duration)
{
    return guarded("wd_fault", duration);
}

Result<CommandResult> DiagnosticsService::force_poor_link(std::chrono::seconds duration)
{
    return guarded("link_fault", duration);
}

void MaintenanceService::bind(DatapointManager& manager, CommandService& commands,
                              DiagnosticsService::EnabledFn test_commands_enabled)
{
    manager_ = &manager;
    diagnostics_.bind(commands, std::move(test_commands_enabled));
}

Result<WriteResult> MaintenanceService::persist_network_config()
{
    if (manager_ == nullptr) {
        return fail(internal_error("MaintenanceService is not bound to a client"));
    }
    return manager_->write(dp::Network_Save,
                           static_cast<std::uint8_t>(NetworkSaveAction::Persist));
}

Result<WriteResult> MaintenanceService::network_save_dangerous(NetworkSaveAction action)
{
    if (manager_ == nullptr) {
        return fail(internal_error("MaintenanceService is not bound to a client"));
    }
    return manager_->write(dp::Network_Save, static_cast<std::uint8_t>(action));
}

}  // namespace fountainer
