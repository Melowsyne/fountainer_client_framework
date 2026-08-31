// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
#include "fountainer/logging/logger.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <memory>
#include <mutex>

namespace fountainer::log {

namespace {
std::atomic<Level> g_level{Level::Info};

// shared_ptr swap instead of a mutex in the write path: write() keeps the
// sink alive even if set_sink() swaps it in the meantime.
std::mutex g_sink_mutex;
std::shared_ptr<const Sink> g_sink;

const char* level_tag(Level l)
{
    switch (l) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO ";
        case Level::Warn:  return "WARN ";
        case Level::Error: return "ERROR";
    }
    return "?";
}

void write_stderr(Level lvl, const char* category, const std::string& msg)
{
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
    localtime_r(&t, &tm);
    char ts[20];
    std::strftime(ts, sizeof ts, "%H:%M:%S", &tm);
    std::fprintf(stderr, "[%s] [%s] %-10s %s\n", ts, level_tag(lvl), category,
                 msg.c_str());
}
}  // namespace

void set_level(const std::string& name)
{
    if (name == "trace") g_level = Level::Trace;
    else if (name == "debug") g_level = Level::Debug;
    else if (name == "info") g_level = Level::Info;
    else if (name == "warn") g_level = Level::Warn;
    else if (name == "error") g_level = Level::Error;
}

void set_level(Level level) { g_level = level; }

void set_sink(Sink sink)
{
    std::lock_guard lock(g_sink_mutex);
    g_sink = sink ? std::make_shared<const Sink>(std::move(sink)) : nullptr;
}

void write(Level lvl, const char* category, const std::string& msg)
{
    if (lvl < g_level.load(std::memory_order_relaxed)) return;

    std::shared_ptr<const Sink> sink;
    {
        std::lock_guard lock(g_sink_mutex);
        sink = g_sink;
    }
    if (sink) {
        (*sink)(lvl, category, msg);
        return;
    }
    write_stderr(lvl, category, msg);
}

}  // namespace fountainer::log
