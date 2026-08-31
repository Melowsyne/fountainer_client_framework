// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/commands.hpp"

#include "fountainer/detail/sync_wait.hpp"

namespace fountainer {

std::string_view to_string(FountainState state) noexcept
{
    switch (state) {
    case FountainState::On:     return "On";
    case FountainState::Off:    return "Off";
    case FountainState::Auto:   return "Auto";
    case FountainState::Manual: return "Manual";
    }
    return "Off";
}

void CommandService::bind(SubmitFn submit, BlockingRunner runner)
{
    submit_ = std::move(submit);
    runner_ = std::move(runner);
}

void CommandService::async_raw(std::string command,
                               std::optional<std::string> target_state,
                               std::optional<std::uint32_t> duration_steps,
                               protocol::OperationPriority priority,
                               Completion completion)
{
    if (command.empty()) {
        completion(fail(validation_error(ErrorCode::ConstraintViolation,
                                         "command must not be empty")));
        return;
    }

    protocol::RequestSpec spec;
    spec.type = "command";
    spec.body = {{"command", command}};
    if (target_state) spec.body["target_state"] = *target_state;
    if (duration_steps) spec.body["duration_steps"] = *duration_steps;
    spec.priority = priority;

    submit_(std::move(spec),
            [command, completion = std::move(completion)](
                Result<nlohmann::json> response) {
                if (!response) {
                    completion(fail(response.error()));
                    return;
                }
                CommandResult result;
                result.command = command;
                result.status = response->value("status", std::string{}) == "applied"
                                    ? CommandStatus::Applied
                                    : CommandStatus::Rejected;
                if (const auto it = response->find("error");
                    it != response->end() && it->is_string()) {
                    result.error = it->get<std::string>();
                }
                completion(std::move(result));
            });
}

void CommandService::async_set_state(FountainState state, Completion completion)
{
    // An Off is a safety operation and must never queue behind polling.
    const auto priority = state == FountainState::Off
                              ? protocol::OperationPriority::SafetyControl
                              : protocol::OperationPriority::InteractiveControl;
    async_raw("set_state", std::string(to_string(state)), std::nullopt, priority,
              std::move(completion));
}

void CommandService::async_turn_on_for(std::chrono::seconds duration,
                                       Completion completion)
{
    if (duration <= std::chrono::seconds::zero()) {
        completion(fail(validation_error(ErrorCode::OutOfRange,
                                         "turn_on duration must be positive")));
        return;
    }
    // Round up to whole 30 s steps (design concept §15.1).
    const auto steps = static_cast<std::uint32_t>(
        (duration + kTurnOnStep - std::chrono::seconds(1)) / kTurnOnStep);
    async_raw("turn_on_duration", std::nullopt, steps,
              protocol::OperationPriority::InteractiveControl, std::move(completion));
}

void CommandService::async_restart_pump(Completion completion)
{
    async_raw("restart", std::nullopt, std::nullopt,
              protocol::OperationPriority::InteractiveControl, std::move(completion));
}

void CommandService::async_reboot(Completion completion)
{
    async_raw("reboot", std::nullopt, std::nullopt,
              protocol::OperationPriority::InteractiveControl, std::move(completion));
}

Result<CommandResult> CommandService::run(std::function<void(Completion)> operation)
{
    return detail::sync_wait<CommandResult>(runner_, std::move(operation));
}

Result<CommandResult> CommandService::raw(std::string command,
                                          std::optional<std::string> target_state,
                                          std::optional<std::uint32_t> duration_steps,
                                          protocol::OperationPriority priority)
{
    return run([this, command = std::move(command), target_state, duration_steps,
                priority](Completion done) mutable {
        async_raw(std::move(command), std::move(target_state), duration_steps,
                  priority, std::move(done));
    });
}

Result<CommandResult> CommandService::set_state(FountainState state)
{
    return run([this, state](Completion done) { async_set_state(state, std::move(done)); });
}

Result<CommandResult> CommandService::turn_on_for(std::chrono::seconds duration)
{
    return run([this, duration](Completion done) {
        async_turn_on_for(duration, std::move(done));
    });
}

Result<CommandResult> CommandService::restart_pump()
{
    return run([this](Completion done) { async_restart_pump(std::move(done)); });
}

Result<CommandResult> CommandService::reboot()
{
    return run([this](Completion done) { async_reboot(std::move(done)); });
}

}  // namespace fountainer
