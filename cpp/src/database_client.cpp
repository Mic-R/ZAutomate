#include "zautomate/database_client.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>
#include "zautomate/logger.hpp"

#include <stdexcept>
#include <utility>
#include <algorithm>
#include <cctype>
#include <string>
#include <regex>

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
    try {
        static const std::regex re(R"((\d{1,2}):(\d{2}))");
        std::smatch m;
        if (std::regex_search(title, m, re) && m.size() >= 3) {
            const int minutes = std::stoi(m[1].str());
            const int seconds = std::stoi(m[2].str());
            return minutes * 60 * 1000 + seconds * 1000;
        }
    } catch (...) {
        // fall through to default
    }
    // Fallback default length: 3 minutes
    return 180000;
}

std::string json_string_or_empty(const nlohmann::json& value) {
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<int>());
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned int>());
    }
    if (value.is_number_float()) {
        return std::to_string(value.get<double>());
    }
    return {};
}

std::string json_string_or_default(const nlohmann::json& object, const char* key, const std::string& fallback) {
    if (!object.contains(key) || object.at(key).is_null()) {
        return fallback;
    }
    const auto& value = object.at(key);
    const auto parsed = json_string_or_empty(value);
    return parsed.empty() ? fallback : parsed;
}

// Trim helper
static std::string trim_copy(const std::string& s) {
    auto b = s.find_first_not_of(" \t\n\r");
    if (b == std::string::npos) return {};
    auto e = s.find_last_not_of(" \t\n\r");
    return s.substr(b, e - b + 1);
}

// Attempt to locate JSON inside a response and parse robustly.
static bool safe_parse_json(const std::string& body, nlohmann::json& out) {
    if (body.empty()) return false;
    // Quick check for JSON start
    auto s = trim_copy(body);
    if (s.empty()) return false;
    try {
        out = nlohmann::json::parse(s);
        return true;
    } catch (const nlohmann::json::parse_error&) {
        // attempt to find first '{' or '[' and parse from there
        const auto pos = body.find_first_of("[{\"");
        if (pos == std::string::npos) return false;
        try {
            out = nlohmann::json::parse(body.substr(pos));
            return true;
        } catch (const nlohmann::json::parse_error&) {
            // as a last resort, try to extract a numeric value
            std::string t = trim_copy(body);
            // remove non-numeric leading/trailing
            std::string num;
            for (char c : t) if ((c >= '0' && c <= '9') || c == '-' || c == '+') num.push_back(c);
            if (!num.empty()) {
                try {
                    out = std::stoi(num);
                    return true;
                } catch (...) {
                }
            }
        }
    }
    return false;
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
    if (res != CURLE_OK) {
        const char* err = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        Logger::log(LogLevel::kWarn, "DBClient", std::string("HTTP GET failed: ") + (err ? err : "unknown"));
        throw std::runtime_error("HTTP GET failed");
    }
    // Log a truncated preview of the response for debugging
    if (!response.empty()) {
        const std::size_t max_len = 2000;
        const std::string preview = response.size() > max_len ? response.substr(0, max_len) + "...[truncated]" : response;
        Logger::log(LogLevel::kInfo, "DBClient",
                    std::string("HTTP GET response len=") + std::to_string(response.size()) + " body=" + preview);
    } else {
        Logger::log(LogLevel::kInfo, "DBClient", "HTTP GET response empty");
    }
    curl_easy_cleanup(curl);
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
    if (res != CURLE_OK) {
        const char* err = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        Logger::log(LogLevel::kWarn, "DBClient", std::string("HTTP POST failed: ") + (err ? err : "unknown"));
        throw std::runtime_error("HTTP POST failed");
    }
    if (!response.empty()) {
        const std::size_t max_len = 2000;
        const std::string preview = response.size() > max_len ? response.substr(0, max_len) + "...[truncated]" : response;
        Logger::log(LogLevel::kInfo, "DBClient",
                    std::string("HTTP POST response len=") + std::to_string(response.size()) + " body=" + preview);
    } else {
        Logger::log(LogLevel::kInfo, "DBClient", "HTTP POST response empty");
    }
    curl_easy_cleanup(curl);
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
    if (body.empty()) {
        Logger::log(LogLevel::kWarn, "DBClient", "Empty response for new show id");
        return -1;
    }
    nlohmann::json json;
    if (safe_parse_json(body, json)) {
        if (json.is_number_integer()) {
            return json.get<int>();
        }
        if (json.is_string()) {
            try {
                return std::stoi(json.get<std::string>());
            } catch (...) {
                return -1;
            }
        }
    } else {
        Logger::log(LogLevel::kWarn, "DBClient", "Failed to parse new show id response");
    }
    return -1;
}

std::optional<Cart> DatabaseClient::get_cart(const std::string& cart_type) {
    for (int i = 0; i < 5; ++i) {
        const auto body = http_get(URL_AUTOCART, {{"type", cart_type_to_index(cart_type)}});
        if (body.empty()) {
            continue;
        }
        nlohmann::json json;
        if (!safe_parse_json(body, json)) {
            Logger::log(LogLevel::kWarn, "DBClient", "Failed to parse get_cart response");
            continue;
        }

        if (json.is_null()) {
            return std::nullopt;
        }

        Cart cart;
        cart.cart_id = json_string_or_default(json, "cartID", "0");
        cart.title = json_string_or_default(json, "title", "");
        cart.issuer = json_string_or_default(json, "issuer", "");
        cart.cart_type = json_string_or_default(json, "type", cart_type);
        cart.filename = library_prefix_ + "carts/" + json_string_or_default(json, "filename", "");
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
    if (body.empty()) {
        Logger::log(LogLevel::kWarn, "DBClient", "Empty playlist response");
        return playlist;
    }
    nlohmann::json json;
    if (!safe_parse_json(body, json)) {
        Logger::log(LogLevel::kWarn, "DBClient", "Failed to parse playlist response");
        return playlist;
    }
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
        if (body.empty()) {
            Logger::log(LogLevel::kWarn, "DBClient", "Empty cart load response");
            continue;
        }
        nlohmann::json json;
        if (!safe_parse_json(body, json)) {
            Logger::log(LogLevel::kWarn, "DBClient", "Failed to parse cart load response");
            continue;
        }
        if (!json.is_array()) {
            continue;
        }

        for (const auto& item : json) {
            Cart cart;
            cart.cart_id = json_string_or_default(item, "cartID", "0");
            cart.title = json_string_or_default(item, "title", "");
            cart.issuer = json_string_or_default(item, "issuer", "");
            cart.cart_type = json_string_or_default(item, "type", "");
            cart.filename = library_prefix_ + "carts/" + json_string_or_default(item, "filename", "");
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
    if (body.empty()) {
        Logger::log(LogLevel::kWarn, "DBClient", "Empty studio search response");
        return out;
    }
    nlohmann::json json;
    if (!safe_parse_json(body, json)) {
        Logger::log(LogLevel::kWarn, "DBClient", "Failed to parse studio search response");
        return out;
    }
    const auto& carts = json.value("carts", nlohmann::json::array());
    if (carts.is_array()) {
        for (const auto& item : carts) {
            Cart c;
            c.cart_id = json_string_or_default(item, "cartID", "0");
            c.title = json_string_or_default(item, "title", "");
            c.issuer = json_string_or_default(item, "issuer", "");
            c.cart_type = json_string_or_default(item, "type", "");
            c.filename = library_prefix_ + "carts/" + json_string_or_default(item, "filename", "");
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
            t.track_id = json_string_or_default(item, "album_code", "") + "-" + json_string_or_default(item, "track_num", "");
            t.title = json_string_or_default(item, "track_name", "");
            t.artist = json_string_or_default(item, "artist_name", "");
            t.rotation = json_string_or_default(item, "rotation", "rotation");
            t.filename = library_prefix_ + json_string_or_default(item, "file_name", "");
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
