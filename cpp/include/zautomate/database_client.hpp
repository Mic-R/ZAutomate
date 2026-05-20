#pragma once

#include <string>
#include <utility>
#include <vector>

#include "zautomate/database_provider.hpp"

namespace zautomate {

class DatabaseClient final : public DatabaseProvider {
public:
    explicit DatabaseClient(std::string library_prefix = "/media/Jemaine/");

    int get_new_show_id(int previous_show_id) override;
    std::optional<Cart> get_cart(const std::string& cart_type) override;
    std::vector<Track> get_playlist(int show_id) override;
    std::unordered_map<int, std::vector<Cart>> get_carts() override;
    LibrarySearchResult search_library(const std::string& query) override;
    void log_cart(const std::string& cart_id) override;
    void log_track(const std::string& album_id, const std::string& track_num, int disc_num = 1) override;

private:
    [[nodiscard]] std::string http_get(const std::string& url, const std::vector<std::pair<std::string, std::string>>& query_params) const;
    [[nodiscard]] std::string http_post(const std::string& url, const std::vector<std::pair<std::string, std::string>>& query_params) const;
    static std::string cart_type_to_index(const std::string& cart_type);

    std::string library_prefix_;
};

}  // namespace zautomate
