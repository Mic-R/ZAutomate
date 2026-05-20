#pragma once

#include <string>

namespace zautomate {

class UpdateManager {
public:
    UpdateManager(std::string owner, std::string repo, std::string current_version);

    void check_and_auto_update() const;

private:
    [[nodiscard]] static bool is_newer_version(const std::string& latest, const std::string& current);
    [[nodiscard]] static std::string normalize_version(const std::string& raw);

    std::string owner_;
    std::string repo_;
    std::string current_version_;
};

}  // namespace zautomate
