#include "zautomate/modules.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <exception>
#include <iostream>
#include <limits>
#include <thread>

#include "zautomate/logger.hpp"

namespace zautomate {

AutomationModule::AutomationModule(DatabaseProvider& db)
    : queue_(
          db,
          [](const Cart& cart) {
              Logger::log(LogLevel::kInfo, "Automation", "START  " + cart.issuer + " - " + cart.title);
          },
          [](const Cart& cart) {
              Logger::log(LogLevel::kInfo, "Automation", "STOP   " + cart.issuer + " - " + cart.title);
          },
          10,
          4) {}

void AutomationModule::start() {
    queue_.start();
}

void AutomationModule::enqueue_cart(const Cart& cart) {
    queue_.enqueue_cart(cart);
}

void AutomationModule::stop() {
    queue_.stop_soft();
    queue_.wait_until_idle(std::chrono::seconds(5));
}

std::size_t AutomationModule::played_count() const {
    return queue_.played_count();
}

std::vector<Cart> AutomationModule::queue_snapshot() const {
    return queue_.get_queue_snapshot();
}

StudioModule::StudioModule(DatabaseProvider& db, std::size_t workers)
    : db_(db), pool_(workers == 0 ? 2 : workers) {}

std::future<LibrarySearchResult> StudioModule::search_async(const std::string& query) {
    return pool_.submit([this, query]() {
        return db_.search_library(query);
    });
}

CartMachineModule::CartMachineModule(DatabaseProvider& db, bool initial_sync) : db_(db) {
    stop_refresh_.store(false);
    if (initial_sync) {
        try {
            refresh();
        } catch (const std::exception& ex) {
            Logger::log(LogLevel::kWarn, "CartMachine", std::string("initial refresh failed: ") + ex.what());
        } catch (...) {
            Logger::log(LogLevel::kWarn, "CartMachine", "initial refresh failed: unknown error");
        }
    }
    // Start background thread which will perform periodic refreshes.
    refresh_thread_ = std::thread(&CartMachineModule::hourly_refresh_loop, this);
}

CartMachineModule::~CartMachineModule() {
    stop_refresh_.store(true);
    if (refresh_thread_.joinable()) {
        refresh_thread_.join();
    }
}

void CartMachineModule::refresh() {
    try {
        auto data = db_.get_carts();
        {
            std::lock_guard<std::mutex> lock(carts_mutex_);
            carts_by_type_ = std::move(data);
        }
        Logger::log(LogLevel::kInfo, "CartMachine", "Cart cache refreshed.");
    } catch (const std::exception& ex) {
        Logger::log(LogLevel::kWarn, "CartMachine", "Cart refresh failed: " + std::string(ex.what()));
    }
}

std::unordered_map<int, std::vector<Cart>> CartMachineModule::carts_by_type() const {
    std::lock_guard<std::mutex> lock(carts_mutex_);
    return carts_by_type_;
}

void CartMachineModule::hourly_refresh_loop() {
    using namespace std::chrono;
    // Do an initial refresh immediately in the background thread, then wait until next hour
    if (stop_refresh_.load()) return;
    refresh();

    while (!stop_refresh_.load()) {
        const auto now = system_clock::now();
        const std::time_t now_time = system_clock::to_time_t(now);
        std::tm tm = *std::localtime(&now_time);
        tm.tm_min = 0;
        tm.tm_sec = 0;
        tm.tm_hour += 1;
        const auto next_hour = system_clock::from_time_t(std::mktime(&tm));
        const auto wait_dur = duration_cast<milliseconds>(next_hour - now);

        auto slept = milliseconds(0);
        while (slept < wait_dur && !stop_refresh_.load()) {
            const auto step = std::min(milliseconds(1000), wait_dur - slept);
            std::this_thread::sleep_for(step);
            slept += step;
        }
        if (stop_refresh_.load()) {
            return;
        }
        refresh();
    }
}

UnifiedApp::UnifiedApp(std::unique_ptr<DatabaseProvider> db_client)
    : db_(std::move(db_client)),
      automation_(*db_),
      studio_(*db_),
    cart_machine_(*db_),
    updater_("wsbf", "ZAutomate", ZAUTOMATE_VERSION) {}

void UnifiedApp::render_header() const {
    std::cout << "\nZAutomate C++ Fork\n";
    std::cout << "MIE\n";
    std::cout << "Copyright Michael Reimchen, 61138 Niederdorfelden, michael@reimchen.org\n";
    std::cout << "----------------------------------------\n";
}

void UnifiedApp::run_automation_loop() {
    std::cout << "[AUTO] start/stop/status/back\n";
    std::string cmd;
    while (true) {
        std::cout << "auto> ";
        if (!std::getline(std::cin, cmd)) {
            return;
        }
        if (cmd == "start") {
            automation_.start();
            Logger::log(LogLevel::kInfo, "Automation", "Automation started.");
        } else if (cmd == "stop") {
            automation_.stop();
            Logger::log(LogLevel::kInfo, "Automation", "Automation stopped softly.");
        } else if (cmd == "status") {
            const auto q = automation_.queue_snapshot();
            std::cout << "Queued: " << q.size() << ", Played: " << automation_.played_count() << '\n';
            if (!q.empty()) {
                std::cout << "Next: " << q.front().issuer << " - " << q.front().title << '\n';
            }
        } else if (cmd == "back") {
            return;
        } else {
            Logger::log(LogLevel::kWarn, "CLI", "Unknown command.");
        }
    }
}

void UnifiedApp::run_studio_loop() {
    std::cout << "[STUDIO] Type a query or 'back'.\n";
    std::string query;
    while (true) {
        std::cout << "studio> ";
        if (!std::getline(std::cin, query)) {
            return;
        }
        if (query == "back") {
            return;
        }
        if (query.empty()) {
            continue;
        }

        LibrarySearchResult results;
        try {
            auto fut = studio_.search_async(query);
            results = fut.get();
        } catch (const std::exception& ex) {
            Logger::log(LogLevel::kWarn, "Studio", "Studio search failed: " + std::string(ex.what()));
            continue;
        }

        std::cout << "Results: " << (results.carts.size() + results.tracks.size()) << '\n';
        std::size_t count = 0;
        for (const auto& item : results.carts) {
            std::cout << " - [" << item.cart_type << "] " << item.issuer << " - " << item.title << '\n';
            if (++count >= 15) {
                break;
            }
        }
        for (const auto& item : results.tracks) {
            if (count >= 15) {
                break;
            }
            std::cout << " - [" << (item.rotation.empty() ? "rotation" : item.rotation) << "] "
                      << item.artist << " - " << item.title << '\n';
            ++count;
        }
    }
}

void UnifiedApp::run_cart_machine_loop() {
    std::cout << "[CART] refresh/list/back\n";
    std::string cmd;
    while (true) {
        std::cout << "cart> ";
        if (!std::getline(std::cin, cmd)) {
            return;
        }
        if (cmd == "refresh") {
            cart_machine_.refresh();
        } else if (cmd == "list") {
            const auto groups = cart_machine_.carts_by_type();
            for (const auto& [type, carts] : groups) {
                std::cout << "Type " << type << ": " << carts.size() << " items\n";
            }
        } else if (cmd == "back") {
            return;
        } else {
            Logger::log(LogLevel::kWarn, "CLI", "Unknown command.");
        }
    }
}

int UnifiedApp::run_cli() {
    updater_.check_and_auto_update();
    render_header();
    std::cout << "Modules: automation | studio | cart | quit\n";

    std::string cmd;
    while (true) {
        std::cout << "main> ";
        if (!std::getline(std::cin, cmd)) {
            automation_.stop();
            return 0;
        }

        if (cmd == "automation") {
            run_automation_loop();
        } else if (cmd == "studio") {
            run_studio_loop();
        } else if (cmd == "cart") {
            run_cart_machine_loop();
        } else if (cmd == "quit") {
            automation_.stop();
            return 0;
        } else {
            Logger::log(LogLevel::kWarn, "CLI", "Unknown command.");
        }
    }
}

}  // namespace zautomate
