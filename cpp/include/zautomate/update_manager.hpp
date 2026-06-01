#pragma once

#include <string>

namespace zautomate {

class UpdateManager {
public:
    UpdateManager(std::string cloud_root_url, std::string current_version);

    void check_and_auto_update() const;
    void install_latest_release() const;

private:
    [[nodiscard]] static bool is_newer_version(const std::string& latest, const std::string& current);
    [[nodiscard]] static std::string normalize_version(const std::string& raw);
    [[nodiscard]] static std::string normalize_base_url(std::string raw);
    [[nodiscard]] static std::string join_url(const std::string& base, const std::string& path);
    [[nodiscard]] std::string latest_release_package_url(std::string* latest_version = nullptr) const;

    std::string cloud_root_url_;
    std::string current_version_;
};

}  // namespace zautomate
