#include "zautomate/update_manager.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "zautomate/logger.hpp"

namespace zautomate {

namespace {

std::size_t write_callback(void* data, std::size_t size, std::size_t nmemb, void* userp) {
    const std::size_t len = size * nmemb;
    auto* buffer = static_cast<std::string*>(userp);
    buffer->append(static_cast<const char*>(data), len);
    return len;
}

std::string http_get(const std::string& url) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl init failed");
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ZAutomateCpp/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        throw std::runtime_error("update check failed");
    }
    return response;
}

std::vector<int> parse_semver(const std::string& version) {
    std::vector<int> parts;
    std::stringstream ss(version);
    std::string token;
    while (std::getline(ss, token, '.')) {
        int val = 0;
        for (char c : token) {
            if (std::isdigit(static_cast<unsigned char>(c))) {
                val = val * 10 + (c - '0');
            } else {
                break;
            }
        }
        parts.push_back(val);
    }
    while (parts.size() < 3) {
        parts.push_back(0);
    }
    return parts;
}

}  // namespace

UpdateManager::UpdateManager(std::string owner, std::string repo, std::string current_version)
    : owner_(std::move(owner)), repo_(std::move(repo)), current_version_(std::move(current_version)) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

std::string UpdateManager::normalize_version(const std::string& raw) {
    if (!raw.empty() && raw.front() == 'v') {
        return raw.substr(1);
    }
    return raw;
}

bool UpdateManager::is_newer_version(const std::string& latest, const std::string& current) {
    const auto l = parse_semver(latest);
    const auto c = parse_semver(current);

    for (std::size_t i = 0; i < 3; ++i) {
        if (l[i] > c[i]) {
            return true;
        }
        if (l[i] < c[i]) {
            return false;
        }
    }
    return false;
}

void UpdateManager::check_and_auto_update() const {
    try {
        const std::string url = "https://api.github.com/repos/" + owner_ + "/" + repo_ + "/releases/latest";
        const auto body = http_get(url);
        auto json = nlohmann::json::parse(body);

        const std::string tag = json.value("tag_name", "");
        if (tag.empty()) {
            Logger::log(LogLevel::kWarn, "Updater", "Update check: no release tag found.");
            return;
        }

        const std::string latest = normalize_version(tag);
        const std::string current = normalize_version(current_version_);

        if (!is_newer_version(latest, current)) {
            return;
        }

        Logger::log(LogLevel::kWarn,
                "Updater",
                "Update available: " + latest + " (current " + current + ")");

        std::string download_url;
#if defined(_WIN32)
        const std::string expected_ext = ".zip";
#else
        const std::string expected_ext = ".deb";
#endif

        if (json.contains("assets") && json["assets"].is_array()) {
            for (const auto& asset : json["assets"]) {
                const std::string name = asset.value("name", "");
                if (name.size() >= expected_ext.size() &&
                    name.compare(name.size() - expected_ext.size(), expected_ext.size(), expected_ext) == 0) {
                    download_url = asset.value("browser_download_url", "");
                    break;
                }
            }
        }

        if (download_url.empty()) {
            Logger::log(LogLevel::kWarn, "Updater", "Update asset not found for this platform.");
            return;
        }

        const auto payload = http_get(download_url);
        const auto temp_dir = std::filesystem::temp_directory_path() / "zautomate-update";
        std::filesystem::create_directories(temp_dir);

#if defined(_WIN32)
        const auto package_path = temp_dir / "zautomate-update.zip";
#else
        const auto package_path = temp_dir / "zautomate-update.deb";
#endif

        std::ofstream out(package_path, std::ios::binary);
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        out.close();

#if defined(_WIN32)
        Logger::log(LogLevel::kWarn,
                    "Updater",
                    "Update package downloaded to " + package_path.string() +
                        ". Install will be applied on next manual deployment.");
#else
        const std::string cmd = "dpkg -i " + package_path.string() + " >/tmp/zautomate-update.log 2>&1";
        const int rc = std::system(cmd.c_str());
        if (rc == 0) {
            Logger::log(LogLevel::kWarn, "Updater", "Auto-update installed successfully. Restart application.");
        } else {
            Logger::log(LogLevel::kWarn,
                        "Updater",
                        "Auto-update download complete but install failed (permissions likely required).");
        }
#endif
    } catch (const std::exception& ex) {
        Logger::log(LogLevel::kWarn, "Updater", "Update check failed: " + std::string(ex.what()));
    }
}

}  // namespace zautomate
