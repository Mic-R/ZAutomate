#pragma once

#include <atomic>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "zautomate/cart_queue.hpp"
#include "zautomate/database_provider.hpp"
#include "zautomate/thread_pool.hpp"
#include "zautomate/update_manager.hpp"

namespace zautomate {

class AutomationModule {
public:
    explicit AutomationModule(DatabaseProvider& db);

    void start();
    void stop();
    void enqueue_cart(const Cart& cart);
    void append_cart(const Cart& cart);
    void clear_queue();
    bool remove_cart_by_id(const std::string& cart_id);
    void set_on_cart_start(std::function<void(const Cart&)> callback);
    [[nodiscard]] std::size_t played_count() const;
    [[nodiscard]] std::vector<Cart> queue_snapshot() const;
    [[nodiscard]] std::optional<Cart> current_cart_snapshot() const;
    [[nodiscard]] std::chrono::system_clock::time_point current_started_at() const;
    [[nodiscard]] bool is_playing() const;

private:
    CartQueue queue_;
    std::function<void(const Cart&)> on_cart_start_callback_;
};

class StudioModule {
public:
    explicit StudioModule(DatabaseProvider& db, std::size_t workers = 2);

    std::future<LibrarySearchResult> search_async(const std::string& query);

private:
    DatabaseProvider& db_;
    ThreadPool pool_;
};

class CartMachineModule {
public:
    explicit CartMachineModule(DatabaseProvider& db, bool initial_sync = true);
    ~CartMachineModule();

    void refresh();
    [[nodiscard]] std::unordered_map<int, std::vector<Cart>> carts_by_type() const;

private:
    void hourly_refresh_loop();

    DatabaseProvider& db_;
    mutable std::mutex carts_mutex_;
    std::unordered_map<int, std::vector<Cart>> carts_by_type_;
    std::atomic<bool> stop_refresh_{false};
    std::thread refresh_thread_;
};

class UnifiedApp {
public:
    explicit UnifiedApp(std::unique_ptr<DatabaseProvider> db_client);

    int run_cli();

private:
    void render_header() const;
    void run_automation_loop();
    void run_studio_loop();
    void run_cart_machine_loop();

    std::unique_ptr<DatabaseProvider> db_;
    AutomationModule automation_;
    StudioModule studio_;
    CartMachineModule cart_machine_;
    UpdateManager updater_;
};

}  // namespace zautomate
