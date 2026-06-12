#pragma once

#include <cstdio>
#include <string_view>

namespace cc {

// Flushed immediately so logs survive crashes and redirected pipes.
inline void logInfo(std::string_view msg) {
    std::fprintf(stdout, "[info] %.*s\n", static_cast<int>(msg.size()), msg.data());
    std::fflush(stdout);
}

inline void logError(std::string_view msg) {
    std::fprintf(stderr, "[error] %.*s\n", static_cast<int>(msg.size()), msg.data());
    std::fflush(stderr);
}

} // namespace cc
