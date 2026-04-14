#pragma once

#include <string>
#include <utility>

namespace ansi {

    inline constexpr const char *kReset = "\x1b[0m";
    inline constexpr const char *kRed = "\x1b[31m";
    inline constexpr const char *kGreen = "\x1b[32m";
    inline constexpr const char *kYellow = "\x1b[33m";
    inline constexpr const char *kBlue = "\x1b[34m";
    inline constexpr const char *kGray = "\x1b[90m";
    inline constexpr const char *kOrange = "\x1b[38;5;208m";
    inline constexpr const char *kBrinjal = "\033[38;5;90m";

    inline std::string colorize(std::string text, const char *code) {
        return std::string(code) + std::move(text) + kReset;
    }

}
