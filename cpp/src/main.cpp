#include <exception>
#include <memory>

#include <QApplication>
#include <QFont>
#include <QIcon>

#include "zautomate/database_client.hpp"
#include "zautomate/gui/main_window.hpp"
#include "zautomate/logger.hpp"

int main(int argc, char* argv[]) {
    try {
        QApplication app(argc, argv);
        app.setApplicationName("ZAutomate");
        app.setOrganizationName("Michael Reimchen");
        app.setApplicationDisplayName("ZAutomate");
        app.setStyle("Fusion");
        app.setWindowIcon(QIcon(":/assets/app-icon.svg"));

        QFont font = app.font();
        font.setPointSizeF(font.pointSizeF() > 0 ? font.pointSizeF() + 1.0 : 12.0);
        app.setFont(font);

        const char* libenv = std::getenv("ZAUTOMATE_LIBRARY");
        if (libenv && libenv[0] != '\0') {
            auto db = std::make_unique<zautomate::DatabaseClient>(std::string(libenv));
            zautomate::MainWindow window(std::move(db));
            return app.exec();
        }
        auto db = std::make_unique<zautomate::DatabaseClient>();
        zautomate::MainWindow window(std::move(db));
        return app.exec();
    } catch (const std::exception& ex) {
        zautomate::Logger::log(zautomate::LogLevel::kError, "Main", "Fatal error: " + std::string(ex.what()));
        return 1;
    }
}
