#pragma once

#include <chrono>
#include <string>

namespace zautomate {

struct Cart {
    std::string cart_id;
    std::string title;
    std::string issuer;
    std::string cart_type;
    std::string filename;
    int length_ms{0};
    std::chrono::system_clock::time_point start_time{};

    [[nodiscard]] bool is_cart_item() const {
        return cart_type == "StationID" || cart_type == "PSA" || cart_type == "Underwriting" || cart_type == "Promotion";
    }
};

}  // namespace zautomate
