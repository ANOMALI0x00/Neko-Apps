#pragma once

#include <string>
#include <vector>
#include <functional>

class Proc {
public:
    static bool exists(const std::string &binary);
    static std::string capture(const std::string &cmd, const std::vector<std::string> &args = {});
    static void runOnMainThread(std::function<void()> fn);

    // Real-time streaming execution with line-by-line callback
    static int executeStreaming(const std::string &cmd,
                                const std::vector<std::string> &args,
                                std::function<void(const std::string &line, bool isStderr)> onLine);
};
