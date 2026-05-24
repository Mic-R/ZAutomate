#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "zautomate/cart_queue.hpp"
#include "zautomate/modules.hpp"
#include "zautomate/thread_pool.hpp"

#define ASSERT_TRUE(cond)                                                                 \
    do {                                                                                   \
        if (!(cond)) {                                                                     \
            std::cerr << "ASSERT_TRUE failed at line " << __LINE__ << ": " #cond "\n"; \
            return 1;                                                                      \
        }                                                                                  \
    } while (false)

namespace {

class MockDatabase final : public zautomate::DatabaseProvider {
public:
    static std::string playable_stub_path() {
        static const std::string path = []() {
            auto stub = std::filesystem::temp_directory_path() / "zautomate_playable_stub.dat";
            std::ofstream file(stub, std::ios::binary | std::ios::trunc);
            file << "stub";
            return stub.string();
        }();
        return path;
    }

    int get_new_show_id(int previous_show_id) override {
        return previous_show_id + 1;
    }

    std::optional<zautomate::Cart> get_cart(const std::string& cart_type) override {
        return zautomate::Cart{
            .cart_id = "cart-" + cart_type,
            .title = cart_type,
            .issuer = "CartIssuer-" + cart_type,
            .cart_type = cart_type,
            .filename = playable_stub_path(),
            .length_ms = 20,
        };
    }

    std::vector<zautomate::Track> get_playlist(int show_id) override {
        return {
            zautomate::Track{.track_id = "ALB" + std::to_string(show_id) + "-1", .title = "T1", .artist = "ArtistA", .rotation = "rotation", .filename = playable_stub_path(), .length_ms = 30},
            zautomate::Track{.track_id = "ALB" + std::to_string(show_id) + "-2", .title = "T2", .artist = "ArtistB", .rotation = "rotation", .filename = playable_stub_path(), .length_ms = 30},
            zautomate::Track{.track_id = "ALB" + std::to_string(show_id) + "-3", .title = "T3", .artist = "ArtistC", .rotation = "rotation", .filename = playable_stub_path(), .length_ms = 30},
        };
    }

    std::unordered_map<int, std::vector<zautomate::Cart>> get_carts() override {
        return {
            {0, {zautomate::Cart{.cart_id = "c0", .title = "PSA", .issuer = "WSBF", .cart_type = "PSA", .filename = playable_stub_path(), .length_ms = 20}}},
            {1, {zautomate::Cart{.cart_id = "c1", .title = "UW", .issuer = "WSBF", .cart_type = "Underwriting", .filename = playable_stub_path(), .length_ms = 20}}},
            {2, {zautomate::Cart{.cart_id = "c2", .title = "ID", .issuer = "WSBF", .cart_type = "StationID", .filename = playable_stub_path(), .length_ms = 20}}},
            {3, {zautomate::Cart{.cart_id = "c3", .title = "Promo", .issuer = "WSBF", .cart_type = "Promotion", .filename = playable_stub_path(), .length_ms = 20}}},
        };
    }

    zautomate::LibrarySearchResult search_library(const std::string& query) override {
        zautomate::LibrarySearchResult result;
        result.carts.push_back(zautomate::Cart{.cart_id = "s-2", .title = "Cart " + query, .issuer = "WSBF", .cart_type = "PSA", .filename = playable_stub_path(), .length_ms = 20});
        result.tracks.push_back(zautomate::Track{.track_id = "s-1", .title = "Result " + query, .artist = "ArtistX", .rotation = "rotation", .filename = playable_stub_path(), .length_ms = 30});
        return result;
    }

    void log_cart(const std::string& cart_id) override {
        logged_carts.push_back(cart_id);
    }

    void log_track(const std::string& album_id, const std::string& track_num, int disc_num = 1) override {
        logged_tracks.push_back(album_id + ":" + std::to_string(disc_num) + ":" + track_num);
    }

    std::vector<std::string> logged_carts;
    std::vector<std::string> logged_tracks;
};

int test_thread_pool_executes_tasks() {
    zautomate::ThreadPool pool(4);
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 16; ++i) {
        futures.push_back(pool.submit([i]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            return i * i;
        }));
    }

    int sum = 0;
    for (auto& f : futures) {
        sum += f.get();
    }

    ASSERT_TRUE(sum > 0);
    return 0;
}

int test_cart_queue_runs_and_logs() {
    MockDatabase db;
    std::atomic<int> starts{0};
    std::atomic<int> stops{0};

    zautomate::CartQueue queue(
        db,
        [&starts](const zautomate::Cart&) { ++starts; },
        [&stops](const zautomate::Cart&) { ++stops; },
        [](const std::string&) {},
        6,
        4);

    queue.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    queue.stop_soft();
    const bool idle = queue.wait_until_idle(std::chrono::seconds(2));

    ASSERT_TRUE(idle);
    ASSERT_TRUE(starts.load() > 0);
    ASSERT_TRUE(stops.load() > 0);
    ASSERT_TRUE((db.logged_carts.size() + db.logged_tracks.size()) >= static_cast<std::size_t>(starts.load()));
    return 0;
}

int test_cart_queue_deduplicates_artists() {
    MockDatabase db;

    zautomate::CartQueue queue(
        db,
        [](const zautomate::Cart&) {},
        [](const zautomate::Cart&) {},
        [](const std::string&) {},
        10,
        4);

    queue.start();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    auto snapshot = queue.get_queue_snapshot();
    std::set<std::string> artists;
    for (const auto& cart : snapshot) {
        if (cart.cart_type == "rotation") {
            artists.insert(cart.issuer);
        }
    }

    queue.stop_soft();
    queue.wait_until_idle(std::chrono::seconds(2));

    ASSERT_TRUE(!artists.empty());
    ASSERT_TRUE(artists.size() <= 3);
    return 0;
}

int test_cart_queue_skips_missing_files() {
    MockDatabase db;
    std::atomic<int> starts{0};
    std::atomic<int> stops{0};

    zautomate::CartQueue queue(
        db,
        [&starts](const zautomate::Cart&) { ++starts; },
        [&stops](const zautomate::Cart&) { ++stops; },
        [](const std::string&) {},
        0,
        1);

    queue.append_cart(zautomate::Cart{
        .cart_id = "missing",
        .title = "Missing File",
        .issuer = "WSBF",
        .cart_type = "rotation",
        .filename = "/definitely/not/present/audio-file.wav",
        .length_ms = 20,
    });
    queue.append_cart(zautomate::Cart{
        .cart_id = "present",
        .title = "Present File",
        .issuer = "WSBF",
        .cart_type = "rotation",
        .filename = "/bin/ls",
        .length_ms = 20,
    });

    queue.start();
    const bool idle = queue.wait_until_idle(std::chrono::seconds(2));
    queue.stop_soft();

    ASSERT_TRUE(idle);
    ASSERT_TRUE(starts.load() == 1);
    ASSERT_TRUE(stops.load() == 1);
    return 0;
}

int test_integrated_modules() {
    auto db = std::make_unique<MockDatabase>();
    zautomate::UnifiedApp* app_ptr = nullptr;

    // Smoke check: modules must construct together from one DB client.
    {
        zautomate::UnifiedApp app(std::move(db));
        app_ptr = &app;
        ASSERT_TRUE(app_ptr != nullptr);
    }

    MockDatabase db2;
    zautomate::StudioModule studio(db2, 2);
    auto results = studio.search_async("mings").get();
    ASSERT_TRUE(!results.carts.empty() || !results.tracks.empty());

    zautomate::CartMachineModule cart_machine(db2);
    const auto& grouped = cart_machine.carts_by_type();
    ASSERT_TRUE(grouped.size() == 4);
    return 0;
}

}  // namespace

int main() {
    if (test_thread_pool_executes_tasks() != 0) {
        return EXIT_FAILURE;
    }
    if (test_cart_queue_runs_and_logs() != 0) {
        return EXIT_FAILURE;
    }
    if (test_cart_queue_deduplicates_artists() != 0) {
        return EXIT_FAILURE;
    }
    if (test_cart_queue_skips_missing_files() != 0) {
        return EXIT_FAILURE;
    }
    if (test_integrated_modules() != 0) {
        return EXIT_FAILURE;
    }

    std::cout << "All C++ tests passed.\n";
    return EXIT_SUCCESS;
}
