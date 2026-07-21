//
// Created by Neuroil on 2026/7/21.
//

#ifndef COM_LITE_ENDIAN_H
#define COM_LITE_ENDIAN_H

#include <bit>

namespace utils::endian {
    constexpr bool is_little_endian() {
        if constexpr (std::endian::native == std::endian::little) {
            return true;
        }
        return false;
    }

    // 转大端（主机序 -> 大端）
    inline uint32_t to_big(uint32_t x) noexcept {
        if constexpr (is_little_endian()) {
            return ((x >> 24) & 0xFF) | ((x >> 8) & 0xFF00) |
                   ((x << 8) & 0xFF0000) | ((x << 24) & 0xFF000000);
        }
        return x;
    }
}

#endif //COM_LITE_ENDIAN_H
