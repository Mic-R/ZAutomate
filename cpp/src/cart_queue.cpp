#include "zautomate/cart_queue.hpp"

#include <algorithm>
#include <cmath>
#include <ctime>
#include <exception>
#include <filesystem>
#include <future>
#include <optional>

#include "zautomate/logger.hpp"

namespace zautomate {

namespace {

bool is_fcc_cart_type(const std::string& type) {
    return type == "StationID" || type == "PSA" || type == "Underwriting";
}

std::tm localtime_safe(const std::time_t& time_value) {
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time_value);
#else
    localtime_r(&time_value, &tm);
#endif
    return tm;
}

bool file_exists(const std::string& path) {
    std::error_code ec;
    return !path.empty() && std::filesystem::exists(std::filesystem::path(path), ec);
}

}  // namespace

CartQueue::CartQueue(DatabaseProvider& db,
                     CartCallback on_cart_start,
                     CartCallback on_cart_stop,
                     std::size_t playlist_min_length,
                     std::size_t prefetch_workers)
    : db_(db),
      on_cart_start_(std::move(on_cart_start)),
      on_cart_stop_(std::move(on_cart_stop)),
      playlist_min_length_(playlist_min_length),
      show_id_(-1),
      is_playing_(false),
      shutdown_(false),
      active_track_(false),
    consecutive_empty_prefetches_(0),
      pool_(prefetch_workers == 0 ? 2 : prefetch_workers),
      worker_(&CartQueue::worker_loop, this) {}

CartQueue::~CartQueue() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
        is_playing_ = false;
    }
    cv_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void CartQueue::start() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        is_playing_ = true;
    }
    cv_.notify_all();
}

void CartQueue::stop_soft() {
    std::lock_guard<std::mutex> lock(mutex_);
    is_playing_ = false;
    cv_.notify_all();
}

std::vector<Cart> CartQueue::get_queue_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::vector<Cart>(queue_.begin(), queue_.end());
}

std::size_t CartQueue::played_count() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return played_.size();
}

std::optional<Cart> CartQueue::current_cart_snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_cart_;
}

std::chrono::system_clock::time_point CartQueue::current_started_at() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_started_at_;
}

bool CartQueue::is_playing() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return is_playing_ && active_track_;
}

bool CartQueue::wait_until_idle(std::chrono::milliseconds timeout) {
    std::unique_lock<std::mutex> lock(mutex_);
    return cv_.wait_for(lock, timeout, [this]() {
        return !active_track_ && (!is_playing_ || queue_.empty());
    });
}

std::vector<CartRule> CartQueue::default_rules() const {
    return {
        CartRule{"StationID", 0, 6000},
        CartRule{"PSA", 15, 6000},
        CartRule{"Underwriting", 30, 6000},
        CartRule{"PSA", 45, 6000},
    };
}

std::vector<Track> CartQueue::fetch_tracks_parallel(std::size_t target_count) {
    const std::size_t jobs = std::max<std::size_t>(2, std::min<std::size_t>(target_count, 4));
    std::vector<std::future<std::pair<int, std::vector<Track>>>> futures;
    futures.reserve(jobs);

    for (std::size_t i = 0; i < jobs; ++i) {
        futures.emplace_back(pool_.submit([this]() {
            try {
                const int previous = show_id_.load();
                const int new_show = db_.get_new_show_id(previous);
                auto playlist = db_.get_playlist(new_show);
                return std::make_pair(new_show, std::move(playlist));
            } catch (const std::exception&) {
                return std::make_pair(-1, std::vector<Track>{});
            }
        }));
    }

    std::vector<Track> merged;
    for (auto& f : futures) {
        auto result = f.get();
        if (result.first >= 0) {
            show_id_.store(result.first);
        }
        for (auto& track : result.second) {
            merged.push_back(std::move(track));
        }
    }
    return merged;
}

bool CartQueue::parse_track_id(const std::string& track_id, std::string& album_id, std::string& track_num) {
    const auto dash = track_id.find('-');
    if (dash == std::string::npos || dash == 0 || dash == track_id.size() - 1) {
        return false;
    }
    album_id = track_id.substr(0, dash);
    track_num = track_id.substr(dash + 1);
    return true;
}

bool CartQueue::is_artist_in_queue_locked(const Cart& cart) const {
    return std::any_of(queue_.begin(), queue_.end(), [&cart](const Cart& q) {
        return q.issuer == cart.issuer;
    });
}

bool CartQueue::is_artist_in_played_locked(const Cart& cart) const {
    return std::any_of(played_.begin(), played_.end(), [&cart](const Cart& p) {
        return p.issuer == cart.issuer;
    });
}

void CartQueue::generate_start_times_locked(std::size_t begin_index) {
    auto start_time = std::chrono::system_clock::now();
    if (begin_index > 0 && begin_index < queue_.size()) {
        const Cart& prev = queue_[begin_index - 1];
        start_time = prev.start_time + std::chrono::milliseconds(prev.length_ms);
    }

    for (std::size_t i = begin_index; i < queue_.size(); ++i) {
        if (i == 0) {
            queue_[i].start_time = std::chrono::system_clock::now();
        } else if (i == begin_index) {
            queue_[i].start_time = start_time;
        } else {
            queue_[i].start_time = queue_[i - 1].start_time + std::chrono::milliseconds(queue_[i - 1].length_ms);
        }
    }
}

void CartQueue::insert_cart_locked(const CartRule& rule) {
    if (queue_.empty()) {
        return;
    }

    const auto now = std::chrono::system_clock::now();
    const std::time_t now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm target_tm = localtime_safe(now_time_t);
    target_tm.tm_min = rule.minute;
    target_tm.tm_sec = 0;

    const auto target = std::chrono::system_clock::from_time_t(std::mktime(&target_tm));
    const auto max_delta = std::chrono::seconds(rule.max_delta_seconds);

    if (target < now) {
        return;
    }

    const auto& last = queue_.back();
    if (last.start_time + std::chrono::milliseconds(last.length_ms) < target - max_delta) {
        return;
    }

    std::optional<Cart> cart;
    try {
        cart = db_.get_cart(rule.type);
    } catch (const std::exception&) {
        return;
    }
    if (!cart.has_value()) {
        return;
    }

    std::size_t min_index = 1;
    auto min_delta = std::chrono::system_clock::duration::max();

    for (std::size_t i = 1; i < queue_.size(); ++i) {
        const auto delta = (queue_[i].start_time > target) ? (queue_[i].start_time - target) : (target - queue_[i].start_time);
        if (delta < min_delta) {
            min_delta = delta;
            min_index = i;
        } else {
            break;
        }
    }

    queue_.insert(queue_.begin() + static_cast<std::ptrdiff_t>(min_index), cart.value());
    generate_start_times_locked(min_index);
}

void CartQueue::insert_carts_locked() {
    for (const auto& rule : default_rules()) {
        insert_cart_locked(rule);
    }
}

void CartQueue::remove_carts_locked() {
    queue_.erase(std::remove_if(queue_.begin(), queue_.end(), [](const Cart& cart) {
                    return is_fcc_cart_type(cart.cart_type);
                }),
                queue_.end());
}

void CartQueue::worker_loop() {
    while (true) {
        Cart current;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this]() { return shutdown_ || is_playing_; });
            if (shutdown_) {
                return;
            }

            if (queue_.size() < playlist_min_length_) {
                lock.unlock();
                auto fetched = fetch_tracks_parallel(playlist_min_length_);
                lock.lock();

                std::size_t begin_index = queue_.size();
                for (auto& track : fetched) {
                    Cart queued_track = track.to_cart_like();
                    if (!is_artist_in_played_locked(queued_track) && !is_artist_in_queue_locked(queued_track)) {
                        queue_.push_back(std::move(queued_track));
                    }
                    if (queue_.size() >= playlist_min_length_) {
                        break;
                    }
                }
                if (begin_index < queue_.size()) {
                    consecutive_empty_prefetches_ = 0;
                    generate_start_times_locked(begin_index);
                    insert_carts_locked();
                } else {
                    ++consecutive_empty_prefetches_;
                }
            }

            if (queue_.empty()) {
                const auto empty_attempts = consecutive_empty_prefetches_;
                lock.unlock();
                if (empty_attempts % 5 == 0) {
                    Logger::log(LogLevel::kWarn,
                                "CartQueue",
                                "Playlist prefetch returned no playable tracks for " + std::to_string(empty_attempts) + " attempts.");
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
                lock.lock();
                cv_.notify_all();
                continue;
            }

            current = queue_.front();
            queue_.pop_front();

            if (!file_exists(current.filename)) {
                Logger::log(LogLevel::kWarn,
                            "CartQueue",
                            "Skipping missing file for " + current.cart_id + ": " + current.filename);
                continue;
            }

            active_track_ = true;
            current_cart_ = current;
            current_started_at_ = std::chrono::system_clock::now();
        }

        if (on_cart_start_) {
            on_cart_start_(current);
        }
        try {
            if (current.is_cart_item()) {
                db_.log_cart(current.cart_id);
            } else {
                std::string album_id;
                std::string track_num;
                if (parse_track_id(current.cart_id, album_id, track_num)) {
                    db_.log_track(album_id, track_num, 1);
                }
            }
        } catch (const std::exception&) {
            // Logging failure should not interrupt playout.
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(std::max(current.length_ms, 1)));

        if (on_cart_stop_) {
            on_cart_stop_(current);
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            played_.push_back(current);
            active_track_ = false;
            current_cart_.reset();

            if (queue_.size() < playlist_min_length_) {
                played_.clear();
                remove_carts_locked();
            }

            const bool has_fcc_carts = std::any_of(queue_.begin(), queue_.end(), [](const Cart& cart) {
                return is_fcc_cart_type(cart.cart_type);
            });
            if (!has_fcc_carts) {
                insert_carts_locked();
            }

            if (!is_playing_) {
                remove_carts_locked();
            }
        }
        cv_.notify_all();
    }
}

void CartQueue::enqueue_cart(const Cart& cart) {
    std::lock_guard<std::mutex> lock(mutex_);
    // push to front so it plays next
    queue_.push_front(cart);
    // recalculate start times
    generate_start_times_locked(0);
    cv_.notify_all();
}

void CartQueue::append_cart(const Cart& cart) {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.push_back(cart);
    generate_start_times_locked(queue_.empty() ? 0 : queue_.size() - 1);
    cv_.notify_all();
}

void CartQueue::clear_queue() {
    std::lock_guard<std::mutex> lock(mutex_);
    queue_.clear();
    cv_.notify_all();
}

bool CartQueue::remove_cart_by_id(const std::string& cart_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = std::find_if(queue_.begin(), queue_.end(), [&cart_id](const Cart& cart) {
        return cart.cart_id == cart_id;
    });
    if (it == queue_.end()) {
        return false;
    }

    const std::size_t index = static_cast<std::size_t>(std::distance(queue_.begin(), it));
    queue_.erase(it);
    if (!queue_.empty()) {
        generate_start_times_locked(index == 0 ? 0 : index - 1);
    }
    cv_.notify_all();
    return true;
}

}  // namespace zautomate
