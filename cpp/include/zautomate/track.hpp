#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "zautomate/cart.hpp"

namespace zautomate {

struct Track {
    std::string track_id;
    std::string title;
    std::string artist;
    std::string rotation;
    std::string filename;
    int length_ms{0};
    std::chrono::system_clock::time_point start_time{};

    [[nodiscard]] Cart to_cart_like() const {
        Cart out;
        out.cart_id = track_id;
        out.title = title;
        out.issuer = artist;
        out.cart_type = rotation.empty() ? "rotation" : rotation;
        out.filename = filename;
        out.length_ms = length_ms;
        out.start_time = start_time;
        return out;
    }
};

struct LibrarySearchResult {
    std::vector<Cart> carts;
    std::vector<Track> tracks;
};

}  // namespace zautomate
