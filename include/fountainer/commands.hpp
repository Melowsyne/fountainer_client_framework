// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Command API (design concept §15). Wire strings ("set_state", "Off", 30 s steps)
// do not belong in application code — typed operations live here.
//
// Firmware contract (command.c / task_com.c):
//   set_state{On,Off,Auto,Manual} · turn_on_duration(duration_steps of 30 s each)
//   restart · reboot — plus diagnostics (wd_fault, link_fault) in the
//   MaintenanceService, deliberately NOT here.
#pragma once

#include <chrono>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

#include <fountainer/protocol/dispatcher.hpp>
#include <fountainer/result.hpp>

namespace fountainer {

// target_state is case-sensitive ("On", not "on") — command_protocol_map.
enum class FountainState { On, Off, Auto, Manual };

std::string_view to_string(FountainState state) noexcept;

enum class CommandStatus { Applied, Rejected };

struct CommandResult {
    std::string command;
    CommandStatus status = CommandStatus::Rejected;
    // "unknown_command" (mapping failed) or "not_permitted"
    // (device logic rejects it, e.g. a latched pump fault).
    std::optional<std::string> error;

    [[nodiscard]] bool applied() const noexcept
    {
        return status == CommandStatus::Applied;
    }
};

// One turn-on duration step corresponds to 30 s on the firmware side.
inline constexpr std::chrono::seconds kTurnOnStep{30};

class CommandService {
public:
    using SubmitFn =
        std::function<void(protocol::RequestSpec, protocol::ResponseHandler)>;
    using BlockingRunner = std::function<Status(std::function<void()>)>;
    using Completion = std::function<void(Result<CommandResult>)>;

    void bind(SubmitFn submit, BlockingRunner runner);

    // --- synchronous (not from the IO thread) ---
    Result<CommandResult> set_state(FountainState state);
    // Rounded up to whole 30 s steps; 0 is an error.
    Result<CommandResult> turn_on_for(std::chrono::seconds duration);
    Result<CommandResult> restart_pump();
    Result<CommandResult> reboot();

    // --- asynchronous ---
    void async_set_state(FountainState state, Completion completion);
    void async_turn_on_for(std::chrono::seconds duration, Completion completion);
    void async_restart_pump(Completion completion);
    void async_reboot(Completion completion);

    // Raw access for tools; only validates that command is not empty.
    void async_raw(std::string command, std::optional<std::string> target_state,
                   std::optional<std::uint32_t> duration_steps,
                   protocol::OperationPriority priority, Completion completion);

    Result<CommandResult> raw(std::string command,
                              std::optional<std::string> target_state,
                              std::optional<std::uint32_t> duration_steps,
                              protocol::OperationPriority priority =
                                  protocol::OperationPriority::InteractiveControl);

private:
    Result<CommandResult> run(std::function<void(Completion)> operation);

    SubmitFn submit_;
    BlockingRunner runner_;
};

}  // namespace fountainer
