// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// detail::sync_wait — THE single implementation of the synchronous wrapper
// around the asynchronous core API (previously copied four times: Manager,
// Commands, Logs, RawProtocol).
//
// runner is the client's BlockingRunner: it executes the work on the
// IO thread and returns an error if it is called from the IO thread itself
// (deadlock protection). start receives a completion that MUST be called
// exactly once with the result (the dispatcher guarantee).
#pragma once

#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

#include <fountainer/result.hpp>

namespace fountainer::detail {

using BlockingRunner = std::function<Status(std::function<void()>)>;

template <typename T, typename Start>
Result<T> sync_wait(const BlockingRunner& runner, Start&& start)
{
    if (!runner) {
        return fail(internal_error("service is not bound to a client"));
    }

    struct Shared {
        std::mutex mutex;
        std::condition_variable cv;
        std::optional<Result<T>> result;
    };
    auto shared = std::make_shared<Shared>();

    auto status = runner([shared, start = std::forward<Start>(start)]() mutable {
        start([shared](Result<T> value) {
            std::lock_guard lock(shared->mutex);
            shared->result = std::move(value);
            shared->cv.notify_all();
        });
    });
    if (!status) return fail(status.error());

    std::unique_lock lock(shared->mutex);
    shared->cv.wait(lock, [&] { return shared->result.has_value(); });
    return std::move(*shared->result);
}

}  // namespace fountainer::detail
