#include "app/cli/line_editor.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

#if defined(__APPLE__) || defined(__linux__) || defined(__unix__)
#include <termios.h>
#include <unistd.h>
#define QUERY_ENGINE_POSIX_TTY 1
#endif

namespace
{

    class RawModeGuard
    {
    public:
        RawModeGuard()
        {
#if defined(QUERY_ENGINE_POSIX_TTY)
            if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO))
            {
                return;
            }

            if (tcgetattr(STDIN_FILENO, &original_) != 0)
            {
                return;
            }

            struct termios raw = original_;
            raw.c_lflag &= static_cast<unsigned long>(~(ICANON | ECHO));
            raw.c_iflag &= static_cast<unsigned long>(~(ICRNL | IXON));
            raw.c_cc[VMIN] = 1;
            raw.c_cc[VTIME] = 0;

            if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) == 0)
            {
                enabled_ = true;
            }
#endif
        }

        ~RawModeGuard()
        {
#if defined(QUERY_ENGINE_POSIX_TTY)
            if (enabled_)
            {
                tcsetattr(STDIN_FILENO, TCSANOW, &original_);
            }
#endif
        }

        [[nodiscard]] bool enabled() const noexcept { return enabled_; }

    private:
        bool enabled_{false};
#if defined(QUERY_ENGINE_POSIX_TTY)
        struct termios original_{};
#endif
    };

} // namespace

ReplLineEditor::ReplLineEditor()
{
#if defined(QUERY_ENGINE_POSIX_TTY)
    interactive_ = isatty(STDIN_FILENO) && isatty(STDOUT_FILENO);
#else
    interactive_ = false;
#endif

    historyFilePath_ = defaultHistoryPath();
    loadHistory();
}

bool ReplLineEditor::readLine(const std::string &prompt, std::string &outLine)
{
    if (interactive_)
    {
        return readLineInteractive(prompt, outLine);
    }
    return readLineStream(prompt, outLine);
}

void ReplLineEditor::addHistoryEntry(const std::string &entry)
{
    const std::string normalized = trim(entry);
    if (normalized.empty())
    {
        return;
    }
    if (!history_.empty() && history_.back() == normalized)
    {
        return;
    }

    history_.push_back(normalized);
    appendHistoryEntry(normalized);
}

std::string ReplLineEditor::defaultHistoryPath()
{
    const char *home = std::getenv("HOME");
    if (home == nullptr || *home == '\0')
    {
        return ".query_engine_history";
    }
    return std::string(home) + "/.query_engine_history";
}

std::string ReplLineEditor::trim(const std::string &value)
{
    const auto isWs = [](unsigned char ch)
    { return std::isspace(ch) != 0; };
    const auto begin = std::find_if_not(value.begin(), value.end(), isWs);
    const auto end = std::find_if_not(value.rbegin(), value.rend(), isWs).base();
    if (begin >= end)
    {
        return {};
    }
    return std::string(begin, end);
}

void ReplLineEditor::loadHistory()
{
    std::ifstream in(historyFilePath_);
    if (!in.is_open())
    {
        return;
    }

    std::string line;
    std::string legacyStatementAccumulator;
    while (std::getline(in, line))
    {
        const std::string normalized = trim(line);
        if (normalized.empty())
        {
            continue;
        }

        // New format: explicit history entry marker keeps commands and queries atomic.
        if (normalized.rfind("H|", 0) == 0)
        {
            const std::string entry = trim(normalized.substr(2));
            if (!entry.empty())
            {
                history_.push_back(entry);
            }
            continue;
        }

        // Backward compatibility: old history may have dot-commands line-by-line.
        if (!normalized.empty() && normalized.front() == '.')
        {
            history_.push_back(normalized);
            continue;
        }

        // Backward compatibility: old SQL history was stored line-by-line.
        if (!legacyStatementAccumulator.empty())
        {
            legacyStatementAccumulator.push_back(' ');
        }
        legacyStatementAccumulator += normalized;

        if (normalized.find(';') != std::string::npos)
        {
            history_.push_back(trim(legacyStatementAccumulator));
            legacyStatementAccumulator.clear();
        }
    }

    if (!legacyStatementAccumulator.empty())
    {
        history_.push_back(trim(legacyStatementAccumulator));
    }
}

void ReplLineEditor::appendHistoryEntry(const std::string &entry) const
{
    std::ofstream out(historyFilePath_, std::ios::app);
    if (!out.is_open())
    {
        return;
    }
    out << "H|" << entry << '\n';
}

bool ReplLineEditor::popPendingLine(std::string &outLine)
{
    if (pendingLines_.empty())
    {
        return false;
    }
    outLine = std::move(pendingLines_.front());
    pendingLines_.pop_front();
    return true;
}

bool ReplLineEditor::readLineStream(const std::string &prompt, std::string &outLine)
{
    if (popPendingLine(outLine))
    {
        return true;
    }

    std::cout << prompt;
    std::cout.flush();

    if (!std::getline(std::cin, outLine))
    {
        return false;
    }
    return true;
}

bool ReplLineEditor::readLineInteractive(const std::string &prompt, std::string &outLine)
{
#if !defined(QUERY_ENGINE_POSIX_TTY)
    return readLineStream(prompt, outLine);
#else
    if (popPendingLine(outLine))
    {
        return true;
    }

    RawModeGuard raw;
    if (!raw.enabled())
    {
        return readLineStream(prompt, outLine);
    }

    std::string line;
    std::size_t cursor = 0;
    std::size_t historyIndex = history_.size();

    auto redraw = [&]()
    {
        std::cout << "\r" << prompt << line << "\x1b[K";
        if (cursor < line.size())
        {
            std::cout << "\r" << prompt;
            std::cout.write(line.data(), static_cast<std::streamsize>(cursor));
        }
        std::cout.flush();
    };

    std::cout << prompt;
    std::cout.flush();

    while (true)
    {
        char ch = '\0';
        const ssize_t n = ::read(STDIN_FILENO, &ch, 1);
        if (n <= 0)
        {
            std::cout << "\n";
            return false;
        }

        if (ch == '\r' || ch == '\n')
        {
            std::cout << "\n";
            outLine = line;
            return true;
        }

        if (ch == 4)
        {
            if (line.empty())
            {
                std::cout << "\n";
                return false;
            }
            continue;
        }

        if (ch == 3)
        {
            std::cout << "^C\n";
            outLine.clear();
            return true;
        }

        if (ch == 127 || ch == 8)
        {
            if (cursor > 0)
            {
                line.erase(cursor - 1, 1);
                --cursor;
                redraw();
            }
            continue;
        }

        if (ch == 27)
        {
            char next = '\0';
            if (::read(STDIN_FILENO, &next, 1) <= 0)
            {
                continue;
            }
            if (next != '[')
            {
                continue;
            }

            std::string seq;
            seq.push_back('[');
            while (true)
            {
                char part = '\0';
                if (::read(STDIN_FILENO, &part, 1) <= 0)
                {
                    break;
                }
                seq.push_back(part);
                if ((part >= 'A' && part <= 'Z') || part == '~')
                {
                    break;
                }
            }

            if (seq == "[A")
            {
                if (!history_.empty() && historyIndex > 0)
                {
                    --historyIndex;
                    line = history_[historyIndex];
                    cursor = line.size();
                    redraw();
                }
                continue;
            }
            if (seq == "[B")
            {
                if (historyIndex < history_.size())
                {
                    ++historyIndex;
                    if (historyIndex == history_.size())
                    {
                        line.clear();
                    }
                    else
                    {
                        line = history_[historyIndex];
                    }
                    cursor = line.size();
                    redraw();
                }
                continue;
            }
            if (seq == "[C")
            {
                if (cursor < line.size())
                {
                    ++cursor;
                    redraw();
                }
                continue;
            }
            if (seq == "[D")
            {
                if (cursor > 0)
                {
                    --cursor;
                    redraw();
                }
                continue;
            }
            if (seq == "[H" || seq == "[1~")
            {
                cursor = 0;
                redraw();
                continue;
            }
            if (seq == "[F" || seq == "[4~")
            {
                cursor = line.size();
                redraw();
                continue;
            }
            if (seq == "[3~")
            {
                if (cursor < line.size())
                {
                    line.erase(cursor, 1);
                    redraw();
                }
                continue;
            }
            if (seq == "[200~")
            {
                std::string pasted;
                while (true)
                {
                    char pc = '\0';
                    if (::read(STDIN_FILENO, &pc, 1) <= 0)
                    {
                        break;
                    }

                    if (pc == 27)
                    {
                        char pn = '\0';
                        if (::read(STDIN_FILENO, &pn, 1) <= 0)
                        {
                            break;
                        }
                        if (pn == '[')
                        {
                            std::string pseq;
                            pseq.push_back('[');
                            while (true)
                            {
                                char pp = '\0';
                                if (::read(STDIN_FILENO, &pp, 1) <= 0)
                                {
                                    break;
                                }
                                pseq.push_back(pp);
                                if ((pp >= 'A' && pp <= 'Z') || pp == '~')
                                {
                                    break;
                                }
                            }
                            if (pseq == "[201~")
                            {
                                break;
                            }
                            continue;
                        }
                        continue;
                    }

                    pasted.push_back(pc);
                }

                if (!pasted.empty())
                {
                    std::string normalizedPaste;
                    normalizedPaste.reserve(pasted.size());
                    for (std::size_t i = 0; i < pasted.size(); ++i)
                    {
                        const char c = pasted[i];
                        if (c == '\r')
                        {
                            if (i + 1 < pasted.size() && pasted[i + 1] == '\n')
                            {
                                continue;
                            }
                            normalizedPaste.push_back('\n');
                            continue;
                        }
                        normalizedPaste.push_back(c);
                    }

                    const std::string expanded =
                        line.substr(0, cursor) + normalizedPaste + line.substr(cursor);

                    std::size_t start = 0;
                    bool emittedFirst = false;
                    while (start <= expanded.size())
                    {
                        const std::size_t nl = expanded.find('\n', start);
                        if (nl == std::string::npos)
                        {
                            const std::string tail = expanded.substr(start);
                            if (!emittedFirst)
                            {
                                line = tail;
                                cursor = line.size();
                                redraw();
                            }
                            else
                            {
                                pendingLines_.push_back(tail);
                            }
                            break;
                        }

                        const std::string piece = expanded.substr(start, nl - start);
                        if (!emittedFirst)
                        {
                            outLine = piece;
                            emittedFirst = true;
                        }
                        else
                        {
                            pendingLines_.push_back(piece);
                        }
                        start = nl + 1;
                    }

                    if (emittedFirst)
                    {
                        std::cout << "\n";
                        return true;
                    }
                }
                continue;
            }
            if (seq == "[201~")
            {
                continue;
            }

            continue;
        }

        if (std::isprint(static_cast<unsigned char>(ch)))
        {
            line.insert(line.begin() + static_cast<std::ptrdiff_t>(cursor), ch);
            ++cursor;
            redraw();
        }
    }
#endif
}
