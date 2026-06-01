#include <exception>
#include <memory>
#include <thread>

#include <QApplication>
#include <QFont>
#include <QIcon>
#include <QSettings>
#include <QTimer>

#include "zautomate/database_client.hpp"
#include "zautomate/gui/main_window.hpp"
#include "zautomate/logger.hpp"
#include "zautomate/update_manager.hpp"

int main(int argc, char* argv[]) {
    try {
        QApplication app(argc, argv);
        app.setApplicationName("ZAutomate");
        app.setOrganizationName("Michael Reimchen");
        app.setApplicationDisplayName("ZAutomate");
        app.setDesktopFileName("zautomate");
        app.setStyle("Fusion");
        app.setWindowIcon(QIcon(":/assets/app-icon.svg"));

        QSettings settings;

        QFont font = app.font();
        font.setPointSizeF(font.pointSizeF() > 0 ? font.pointSizeF() + 1.0 : 12.0);
        app.setFont(font);

        const char* libenv = std::getenv("ZAUTOMATE_LIBRARY");
        const auto library_prefix = settings.value("storage/libraryPrefix", libenv && libenv[0] != '\0' ? libenv : "/media/Jemaine/").toString().toStdString();
        const auto api_base_url = settings.value("network/baseSearchUrl", "https://wsbf.net").toString().toStdString();
        if (libenv && libenv[0] != '\0') {
            auto db = std::make_unique<zautomate::DatabaseClient>(library_prefix, api_base_url);
            zautomate::MainWindow window(std::move(db));
            return app.exec();
        }
        auto db = std::make_unique<zautomate::DatabaseClient>(library_prefix, api_base_url);
        zautomate::MainWindow window(std::move(db));
        auto updater = std::make_shared<zautomate::UpdateManager>("https://cloud.mic-r.eu/zautomate", ZAUTOMATE_VERSION);
        QTimer::singleShot(1500, &app, [updater]() {
            std::thread([updater]() {
                updater->check_and_auto_update();
            }).detach();
        });
        return app.exec();
    } catch (const std::exception& ex) {
        zautomate::Logger::log(zautomate::LogLevel::kError, "Main", "Fatal error: " + std::string(ex.what()));
        return 1;
    }
}
