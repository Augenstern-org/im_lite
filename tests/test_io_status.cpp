#include "common/io_status.h"

#include "test_support.h"

#include <cstring>
#include <iostream>

int main() {
    // 每个枚举值逐值断言 to_string 的精确输出。
    // 这是 -Wswitch 兜底不存在（全仓无 -Wall/-Wswitch/-Werror，唯一的编译选项是 MSVC /utf-8）
    // 之后补回来的机制：to_string 若漏掉新枚举值会静默落到 "Unknown"，只有这里的逐值断言能拦住。
    const char* s;

    s = types::to_string(types::IoStatus::ok);
    CHECK(s != nullptr);
    CHECK(std::strcmp(s, "Ok") == 0);

    s = types::to_string(types::IoStatus::closed);
    CHECK(s != nullptr);
    CHECK(std::strcmp(s, "Closed") == 0);

    s = types::to_string(types::IoStatus::would_block);
    CHECK(s != nullptr);
    CHECK(std::strcmp(s, "WouldBlock") == 0);

    s = types::to_string(types::IoStatus::interrupted);
    CHECK(s != nullptr);
    CHECK(std::strcmp(s, "Interrupted") == 0);

    s = types::to_string(types::IoStatus::frame_too_long);
    CHECK(s != nullptr);
    CHECK(std::strcmp(s, "FrameTooLong") == 0);

    s = types::to_string(types::IoStatus::timeout);
    CHECK(s != nullptr);
    CHECK(std::strcmp(s, "Timeout") == 0);

    s = types::to_string(types::IoStatus::error);
    CHECK(s != nullptr);
    CHECK(std::strcmp(s, "Error") == 0);
    s = types::to_string(types::IoStatus::incomplete);
    CHECK(s != nullptr);
    CHECK(std::strcmp(s, "Incomplete") == 0);

    std::cout << "test_io_status: PASSED\n";
    return 0;
}
