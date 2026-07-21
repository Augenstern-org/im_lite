#include "common/io_status.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    // 每个枚举值都有对应的字符串表示
    const char* s;

    s = types::to_string(types::IoStatus::ok);
    assert(s != nullptr);
    assert(std::strcmp(s, "Ok") == 0);

    s = types::to_string(types::IoStatus::closed);
    assert(s != nullptr);
    assert(std::strcmp(s, "Closed") == 0);

    s = types::to_string(types::IoStatus::would_block);
    assert(s != nullptr);
    assert(std::strcmp(s, "WouldBlock") == 0);

    s = types::to_string(types::IoStatus::interrupted);
    assert(s != nullptr);
    assert(std::strcmp(s, "Interrupted") == 0);

    s = types::to_string(types::IoStatus::frame_too_long);
    assert(s != nullptr);
    assert(std::strcmp(s, "FrameTooLong") == 0);

    s = types::to_string(types::IoStatus::timeout);
    assert(s != nullptr);
    assert(std::strcmp(s, "Timeout") == 0);

    s = types::to_string(types::IoStatus::error);
    assert(s != nullptr);
    assert(std::strcmp(s, "Error") == 0);

    std::cout << "test_io_status: PASSED\n";
    return 0;
}
