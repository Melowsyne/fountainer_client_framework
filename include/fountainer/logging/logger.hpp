// Copyright (c) 2026 Sascha G. Eurich, Melowsyne UNIPESSOAL LDA
// SPDX-License-Identifier: MIT
//
// Minimal category logger (design concept §32/§51/§52). Never log secrets!
// The application can redirect the output (set_sink) — the default is stderr.
#pragma once

#include <functional>
#include <string>

namespace fountainer::log {

enum class Level { Trace, Debug, Info, Warn, Error };

void set_level(const std::string& name);   // "trace".."error"
void set_level(Level level);

// Injectable sink (design concept §32): receives ONLY entries >= level filter.
// nullptr restores the stderr default. The sink is called from multiple
// threads and must be thread-safe itself.
using Sink = std::function<void(Level, const char* category,
                                const std::string& message)>;
void set_sink(Sink sink);

void write(Level lvl, const char* category, const std::string& msg);

inline void trace(const char* cat, const std::string& m) { write(Level::Trace, cat, m); }
inline void debug(const char* cat, const std::string& m) { write(Level::Debug, cat, m); }
inline void info(const char* cat, const std::string& m)  { write(Level::Info, cat, m); }
inline void warn(const char* cat, const std::string& m)  { write(Level::Warn, cat, m); }
inline void error(const char* cat, const std::string& m) { write(Level::Error, cat, m); }

}  // namespace fountainer::log
