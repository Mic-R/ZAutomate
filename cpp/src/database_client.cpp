#include "zautomate/database_client.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <stdexcept>
#include <utility>

namespace zautomate {

namespace {

constexpr const char* URL_CARTLOAD = "https://wsbf.net/api/zautomate/cartmachine_load.php";
constexpr const char* URL_AUTOLOAD = "https://wsbf.net/api/zautomate/automation_generate_showplist.php";
constexpr const char* URL_AUTOSTART = "https://wsbf.net/api/zautomate/automation_generate_showid.php";
constexpr const char* URL_AUTOCART = "https://wsbf.net/api/zautomate/automation_add_carts.php";
constexpr const char* URL_STUDIOSEARCH = "https://wsbf.net/api/zautomate/studio_search.php";
constexpr const char* URL_LOG_CART = "https://wsbf.net/api/zautomate/log_cart.php";
constexpr const char* URL_LOG_TRACK = "https://wsbf.net/api/zautomate/log_track.php";

std::size_t write_callback(void* data, std::size_t size, std::size_t nmemb, void* userp) {
    const std::size_t len = size * nmemb;
    auto* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<const char*>(data), len);
    return len;
}

std::string build_query(CURL* curl, const std::vector<std::pair<std::string, std::string>>& query_params) {
    std::string query;
    bool first = true;
    for (const auto& [k, v] : query_params) {
        char* escaped = curl_easy_escape(curl, v.c_str(), static_cast<int>(v.size()));
        if (!escaped) {
            continue;
        }
        query += (first ? "?" : "&");
        query += k + "=" + escaped;
        curl_free(escaped);
        first = false;
    }
    return query;
}

int estimate_length_ms_from_title(const std::string& title) {
    return 180000 + static_cast<int>(title.size() % 3000);
}

}  // namespace

DatabaseClient::DatabaseClient(std::string library_prefix)
    : library_prefix_(std::move(library_prefix)) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

std::string DatabaseClient::http_get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& query_params) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init failed");
    }

    std::string response;
    const std::string full_url = url + build_query(curl, query_params);

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ZAutomateCpp/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error("HTTP GET failed");
    }
    return response;
}

std::string DatabaseClient::http_post(const std::string& url, const std::vector<std::pair<std::string, std::string>>& query_params) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl_easy_init failed");
    }

    std::string response;
    const std::string query = build_query(curl, query_params);
    const std::string full_url = url + query;

    curl_easy_setopt(curl, CURLOPT_URL, full_url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ZAutomateCpp/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);

    const CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) {
        throw std::runtime_error("HTTP POST failed");
    }
    return response;
}

std::string DatabaseClient::cart_type_to_index(const std::string& cart_type) {
    if (cart_type == "PSA") {
        return "0";
    }
    if (cart_type == "Underwriting") {
        return "1";
    }
    if (cart_type == "StationID") {
        return "2";
    }
    if (cart_type == "Promotion") {
        return "3";
    }
    return cart_type;
}

int DatabaseClient::get_new_show_id(int previous_show_id) {
    const auto body = http_get(URL_AUTOSTART, {{"showid", std::to_string(previous_show_id)}});
    auto json = nlohmann::json::parse(body);
    if (json.is_number_integer()) {
        return json.get<int>();
    }
    return -1;
}

std::optional<Cart> DatabaseClient::get_cart(const std::string& cart_type) {
    for (int i = 0; i < 5; ++i) {
        const auto body = http_get(URL_AUTOCART, {{"type", cart_type_to_index(cart_type)}});
        auto json = nlohmann::json::parse(body);

        if (json.is_null()) {
            return std::nullopt;
        }

        Cart cart;
        cart.cart_id = std::to_string(json.value("cartID", 0));
        cart.title = json.value("title", "");
        cart.issuer = json.value("issuer", "");
        cart.cart_type = json.value("type", cart_type);
        cart.filename = library_prefix_ + "carts/" + json.value("filename", "");
        cart.length_ms = estimate_length_ms_from_title(cart.title);

        if (!cart.filename.empty()) {
            return cart;
        }
    }
    return std::nullopt;
}

std::vector<Track> DatabaseClient::get_playlist(int show_id) {
    std::vector<Track> playlist;
    const auto body = http_get(URL_AUTOLOAD, {{"showid", std::to_string(show_id)}});
    auto json = nlohmann::json::parse(body);
    if (!json.is_array()) {
        return playlist;
    }

    for (const auto& item : json) {
        Track track;
        track.track_id = item.value("lb_album_code", std::string()) + "-" + item.value("lb_track_num", std::string());
        track.title = item.value("lb_track_name", "");
        track.artist = item.value("artist_name", "");
        track.rotation = item.value("rotation", "rotation");
        track.filename = library_prefix_ + item.value("file_name", "");
        track.length_ms = estimate_length_ms_from_title(track.title);

        if (!track.filename.empty()) {
            playlist.push_back(std::move(track));
        }
    }
    return playlist;
}

std::unordered_map<int, std::vector<Cart>> DatabaseClient::get_carts() {
    std::unordered_map<int, std::vector<Cart>> carts = {{0, {}}, {1, {}}, {2, {}}, {3, {}}};

    for (int cart_type = 0; cart_type <= 3; ++cart_type) {
        const auto body = http_get(URL_CARTLOAD, {{"type", std::to_string(cart_type)}});
        auto json = nlohmann::json::parse(body);
        if (!json.is_array()) {
            continue;
        }

        for (const auto& item : json) {
            Cart cart;
            cart.cart_id = std::to_string(item.value("cartID", 0));
            cart.title = item.value("title", "");
            cart.issuer = item.value("issuer", "");
            cart.cart_type = item.value("type", "");
            cart.filename = library_prefix_ + "carts/" + item.value("filename", "");
            cart.length_ms = estimate_length_ms_from_title(cart.title);

            if (!cart.filename.empty()) {
                carts[cart_type].push_back(std::move(cart));
            }
        }
    }

    return carts;
}

LibrarySearchResult DatabaseClient::search_library(const std::string& query) {
    LibrarySearchResult out;

    const auto body = http_get(URL_STUDIOSEARCH, {{"query", query}});
    auto json = nlohmann::json::parse(body);

    const auto& carts = json.value("carts", nlohmann::json::array());
    if (carts.is_array()) {
        for (const auto& item : carts) {
            Cart c;
            c.cart_id = std::to_string(item.value("cartID", 0));
            c.title = item.value("title", "");
            c.issuer = item.value("issuer", "");
            c.cart_type = item.value("type", "");
            c.filename = library_prefix_ + "carts/" + item.value("filename", "");
            c.length_ms = estimate_length_ms_from_title(c.title);
            if (!c.filename.empty()) {
                out.carts.push_back(std::move(c));
            }
        }
    }

    const auto& tracks = json.value("tracks", nlohmann::json::array());
    if (tracks.is_array()) {
        for (const auto& item : tracks) {
            Track t;
            t.track_id = item.value("album_code", std::string()) + "-" + item.value("track_num", std::string());
            t.title = item.value("track_name", "");
            t.artist = item.value("artist_name", "");
            t.rotation = item.value("rotation", "rotation");
            t.filename = library_prefix_ + item.value("file_name", "");
            t.length_ms = estimate_length_ms_from_title(t.title);
            if (!t.filename.empty()) {
                out.tracks.push_back(std::move(t));
            }
        }
    }

    return out;
}

void DatabaseClient::log_cart(const std::string& cart_id) {
    (void)http_post(URL_LOG_CART, {{"cartid", cart_id}});
}

void DatabaseClient::log_track(const std::string& album_id, const std::string& track_num, int disc_num) {
    (void)http_post(URL_LOG_TRACK,
                    {{"albumID", album_id}, {"disc_num", std::to_string(disc_num)}, {"track_num", track_num}});
}

}  // namespace zautomate
