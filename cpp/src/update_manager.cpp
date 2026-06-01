#include "zautomate/update_manager.hpp"

#include <curl/curl.h>

#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <regex>
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

std::string http_get(const std::string& url, long timeout_seconds = 15L) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("curl init failed");
    }

    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "ZAutomateCpp/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout_seconds);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if (res != CURLE_OK) {
        throw std::runtime_error("update check failed");
    }
    return response;
}

bool ends_with(const std::string& value, const std::string& suffix) {
    return value.size() >= suffix.size() &&
        value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string shell_quote(const std::string& value) {
    std::string quoted = "'";
    for (char c : value) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += "'";
    return quoted;
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

std::string platform_package_dir() {
#if defined(_WIN32)
    return "packages-Windows";
#elif defined(__APPLE__)
    return "packages-macOS";
#else
    return "packages-Linux";
#endif
}

std::string platform_expected_extension() {
#if defined(_WIN32)
    return ".zip";
#elif defined(__APPLE__)
    return ".dmg";
#else
    return ".deb";
#endif
}

}  // namespace

UpdateManager::UpdateManager(std::string cloud_root_url, std::string current_version)
    : cloud_root_url_(normalize_base_url(std::move(cloud_root_url))), current_version_(std::move(current_version)) {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

std::string UpdateManager::normalize_version(const std::string& raw) {
    if (!raw.empty() && raw.front() == 'v') {
        return raw.substr(1);
    }
    return raw;
}

std::string UpdateManager::normalize_base_url(std::string raw) {
    while (!raw.empty() && raw.back() == '/') {
        raw.pop_back();
    }
    return raw;
}

std::string UpdateManager::join_url(const std::string& base, const std::string& path) {
    if (base.empty()) {
        return path;
    }
    if (path.empty()) {
        return base;
    }
    return base + (path.front() == '/' ? "" : "/") + path;
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

std::string UpdateManager::latest_release_package_url(std::string* latest_version) const {
    if (cloud_root_url_.empty()) {
        return {};
    }

    const auto root_index = http_get(cloud_root_url_ + "/");
    std::regex release_dir_re(R"(href="(v([0-9]+(?:\.[0-9]+)+))/")");

    std::string latest_dir;
    std::string release_version;
    for (std::sregex_iterator it(root_index.begin(), root_index.end(), release_dir_re), end; it != end; ++it) {
        const std::string dir = (*it)[1].str();
        const std::string version = normalize_version((*it)[2].str());
        if (release_version.empty() || is_newer_version(version, release_version)) {
            release_version = version;
            latest_dir = dir;
        }
    }

    if (latest_dir.empty()) {
        return {};
    }

    if (latest_version) {
        *latest_version = release_version;
    }

    const std::string package_dir = platform_package_dir();
    const std::string expected_ext = platform_expected_extension();
    const auto package_index = http_get(join_url(cloud_root_url_, latest_dir + "/" + package_dir + "/"));
    std::regex asset_href_re(R"re(href="([^"]+)")re");

    for (std::sregex_iterator it(package_index.begin(), package_index.end(), asset_href_re), end; it != end; ++it) {
        const std::string href = (*it)[1].str();
        if (href == "../") {
            continue;
        }
        if (ends_with(href, expected_ext)) {
            return join_url(cloud_root_url_, latest_dir + "/" + package_dir + "/" + href);
        }
    }

    return {};
}

void UpdateManager::check_and_auto_update() const {
    try {
        std::string latest_version;
        const auto download_url = latest_release_package_url(&latest_version);
        if (download_url.empty()) {
            Logger::log(LogLevel::kWarn, "Updater", "Update check: no release directories found on cloud.");
            return;
        }

        const std::string current = normalize_version(current_version_);

        if (!is_newer_version(latest_version, current)) {
            return;
        }

        Logger::log(LogLevel::kWarn,
                "Updater",
                "Update available: " + latest_version + " (current " + current + ")");

        const auto payload = http_get(download_url, 300L);
        const auto temp_dir = std::filesystem::temp_directory_path() / "zautomate-update";
        std::filesystem::create_directories(temp_dir);

#if defined(_WIN32)
        const auto package_path = temp_dir / "zautomate-update.zip";
#elif defined(__APPLE__)
        const auto package_path = temp_dir / "zautomate-update.dmg";
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
#elif defined(__APPLE__)
        Logger::log(LogLevel::kWarn,
                    "Updater",
                    "Update package downloaded to " + package_path.string() +
                        ". Open the DMG to install the new version.");
#else
        const std::string cmd = "pkexec /usr/bin/dpkg -i " + shell_quote(package_path.string());
        const int rc = std::system(cmd.c_str());
        if (rc == 0) {
            Logger::log(LogLevel::kWarn, "Updater", "Auto-update installed successfully. Restart application.");
        } else {
            Logger::log(LogLevel::kWarn,
                        "Updater",
                        "Auto-update download complete but install failed. Run the downloaded .deb with admin privileges.");
        }
#endif
    } catch (const std::exception& ex) {
        Logger::log(LogLevel::kWarn, "Updater", "Update check failed: " + std::string(ex.what()));
    }
}

void UpdateManager::install_latest_release() const {
    try {
        std::string latest_version;
        const auto download_url = latest_release_package_url(&latest_version);
        if (download_url.empty()) {
            Logger::log(LogLevel::kWarn, "Updater", "Installer not found in cloud releases.");
            return;
        }

        Logger::log(LogLevel::kWarn, "Updater", "Installing latest release " + latest_version + " from cloud.");
        const auto payload = http_get(download_url, 300L);
        const auto temp_dir = std::filesystem::temp_directory_path() / "zautomate-install";
        std::filesystem::create_directories(temp_dir);

#if defined(_WIN32)
        const auto package_path = temp_dir / "zautomate-install.zip";
#elif defined(__APPLE__)
        const auto package_path = temp_dir / "zautomate-install.dmg";
#else
        const auto package_path = temp_dir / "zautomate-install.deb";
#endif

        std::ofstream out(package_path, std::ios::binary);
        out.write(payload.data(), static_cast<std::streamsize>(payload.size()));
        out.close();

#if defined(_WIN32)
        Logger::log(LogLevel::kWarn, "Updater", "Installer downloaded to " + package_path.string() + ". Open it to install ZAutomate.");
#elif defined(__APPLE__)
        Logger::log(LogLevel::kWarn, "Updater", "Installer downloaded to " + package_path.string() + ". Open the DMG to install ZAutomate.");
#else
        const std::string cmd = "pkexec /usr/bin/dpkg -i " + shell_quote(package_path.string());
        const int rc = std::system(cmd.c_str());
        if (rc == 0) {
            Logger::log(LogLevel::kWarn, "Updater", "Permanent install completed successfully.");
        } else {
            Logger::log(LogLevel::kWarn, "Updater", "Permanent install failed. Run the downloaded .deb with admin privileges.");
        }
#endif
    } catch (const std::exception& ex) {
        Logger::log(LogLevel::kWarn, "Updater", "Installer failed: " + std::string(ex.what()));
    }
}

}  // namespace zautomate
