#include <exception>
#include <memory>

#include "zautomate/database_client.hpp"
#include "zautomate/logger.hpp"
#include "zautomate/modules.hpp"

int main() {
    try {
        auto db = std::make_unique<zautomate::DatabaseClient>();
        zautomate::UnifiedApp app(std::move(db));
        return app.run_cli();
    } catch (const std::exception& ex) {
        zautomate::Logger::log(zautomate::LogLevel::kError, "Main", "Fatal error: " + std::string(ex.what()));
        return 1;
    }
}
