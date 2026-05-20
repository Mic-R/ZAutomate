#pragma once

#include <mutex>
#include <string>

namespace zautomate {

enum class LogLevel {
    kInfo,
    kWarn,
    kError,
};

class Logger {
public:
    static void log(LogLevel level, const std::string& component, const std::string& message);

private:
    static std::mutex mutex_;
};

}  // namespace zautomate
