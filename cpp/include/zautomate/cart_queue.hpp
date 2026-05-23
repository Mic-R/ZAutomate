#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "zautomate/cart.hpp"
#include "zautomate/database_provider.hpp"
#include "zautomate/track.hpp"
#include "zautomate/thread_pool.hpp"

namespace zautomate {

struct CartRule {
    std::string type;
    int minute;
    int max_delta_seconds;
};

class CartQueue {
public:
    using CartCallback = std::function<void(const Cart&)>;

    CartQueue(DatabaseProvider& db,
              CartCallback on_cart_start,
              CartCallback on_cart_stop,
              std::size_t playlist_min_length = 10,
              std::size_t prefetch_workers = 4);

    ~CartQueue();

    void start();
    void stop_soft();
    void enqueue_cart(const Cart& cart);
    void append_cart(const Cart& cart);
    void clear_queue();
    bool remove_cart_by_id(const std::string& cart_id);

    [[nodiscard]] std::vector<Cart> get_queue_snapshot() const;
    [[nodiscard]] std::size_t played_count() const;
    [[nodiscard]] std::optional<Cart> current_cart_snapshot() const;
    [[nodiscard]] std::chrono::system_clock::time_point current_started_at() const;
    [[nodiscard]] bool is_playing() const;

    bool wait_until_idle(std::chrono::milliseconds timeout);

private:
    [[nodiscard]] std::vector<CartRule> default_rules() const;
    [[nodiscard]] std::vector<Track> fetch_tracks_parallel(std::size_t target_count);
    static bool parse_track_id(const std::string& track_id, std::string& album_id, std::string& track_num);

    void worker_loop();
    void generate_start_times_locked(std::size_t begin_index);
    void insert_carts_locked();
    void insert_cart_locked(const CartRule& rule);
    void remove_carts_locked();

    [[nodiscard]] bool is_artist_in_queue_locked(const Cart& cart) const;
    [[nodiscard]] bool is_artist_in_played_locked(const Cart& cart) const;

    DatabaseProvider& db_;
    CartCallback on_cart_start_;
    CartCallback on_cart_stop_;
    std::size_t playlist_min_length_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;

    std::deque<Cart> queue_;
    std::vector<Cart> played_;
    std::atomic<int> show_id_;
    std::optional<Cart> current_cart_;
    std::chrono::system_clock::time_point current_started_at_{};

    bool is_playing_;
    bool shutdown_;
    bool active_track_;
    std::size_t consecutive_empty_prefetches_;

    ThreadPool pool_;
    std::thread worker_;
};

}  // namespace zautomate
