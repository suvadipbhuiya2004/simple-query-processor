#pragma once

#include <string>
#include <deque>
#include <vector>

class ReplLineEditor
{
public:
    ReplLineEditor();
    ~ReplLineEditor() = default;

    bool readLine(const std::string &prompt, std::string &outLine);
    void addHistoryEntry(const std::string &entry);

private:
    bool interactive_{false};
    std::string historyFilePath_;
    std::vector<std::string> history_;
    std::deque<std::string> pendingLines_;

    [[nodiscard]] static std::string defaultHistoryPath();
    [[nodiscard]] static std::string trim(const std::string &value);

    void loadHistory();
    void appendHistoryEntry(const std::string &entry) const;
    bool popPendingLine(std::string &outLine);

    bool readLineInteractive(const std::string &prompt, std::string &outLine);
    bool readLineStream(const std::string &prompt, std::string &outLine);
};
