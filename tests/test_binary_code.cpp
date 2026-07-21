#include "core/binary_code.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <type_traits>

int main() {
    // Opcode 和 Status 底层类型是 uint8_t（恰好 1 字节，与线上字段等宽）
    static_assert(sizeof(core::Opcode) == 1, "Opcode must be 1 byte");
    static_assert(sizeof(core::Status) == 1, "Status must be 1 byte");
    static_assert(std::is_same_v<std::underlying_type_t<core::Opcode>, uint8_t>);
    static_assert(std::is_same_v<std::underlying_type_t<core::Status>, uint8_t>);

    // enum class 禁止隐式转换到 int
    static_assert(!std::is_convertible_v<core::Opcode, int>);
    static_assert(!std::is_convertible_v<core::Status, int>);

    // static_cast 显式转换可行
    uint8_t raw_op = static_cast<uint8_t>(core::Opcode::request);
    uint8_t raw_st = static_cast<uint8_t>(core::Status::ok);
    (void)raw_op;
    (void)raw_st;

    std::cout << "test_binary_code: PASSED\n";
    return 0;
}
