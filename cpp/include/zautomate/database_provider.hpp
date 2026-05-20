#pragma once

#include <optional>
#include <unordered_map>
#include <string>
#include <vector>

#include "zautomate/cart.hpp"
#include "zautomate/track.hpp"

namespace zautomate {

class DatabaseProvider {
public:
    virtual ~DatabaseProvider() = default;

    virtual int get_new_show_id(int previous_show_id) = 0;
    virtual std::optional<Cart> get_cart(const std::string& cart_type) = 0;
    virtual std::vector<Track> get_playlist(int show_id) = 0;
    virtual std::unordered_map<int, std::vector<Cart>> get_carts() = 0;
    virtual LibrarySearchResult search_library(const std::string& query) = 0;
    virtual void log_cart(const std::string& cart_id) = 0;
    virtual void log_track(const std::string& album_id, const std::string& track_num, int disc_num = 1) = 0;
};

}  // namespace zautomate
