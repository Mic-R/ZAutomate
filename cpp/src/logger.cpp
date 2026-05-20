#include "zautomate/logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace zautomate {

std::mutex Logger::mutex_;

void Logger::log(LogLevel level, const std::string& component, const std::string& message) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm_buf{};
#if defined(_WIN32)
    localtime_s(&tm_buf, &t);
#else
    localtime_r(&t, &tm_buf);
#endif

    const char* lvl = "INFO";
    if (level == LogLevel::kWarn) {
        lvl = "WARN";
    } else if (level == LogLevel::kError) {
        lvl = "ERROR";
    }

    std::ostringstream oss;
    oss << std::put_time(&tm_buf, "%Y-%m-%d %H:%M:%S")
        << " [" << lvl << "] [" << component << "] " << message;

    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << oss.str() << '\n';
}

}  // namespace zautomate
