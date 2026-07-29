// 头文件自足性检查。
//
// 真正的断言发生在编译期：src/ 下每个头文件各自占一个翻译单元（由
// tests/header_self_contained.cpp.in 生成），单独编译一次。任何头文件若少写了
// 自己直接用到的 #include，本目标就编不过 —— 不再靠"恰好被别的头文件先包含"蒙混。
//
// 参见 2026-07-29 日志：binary_code.h 删掉 <cstdint> 后，其余翻译单元都靠
// message.h -> json.hpp 间接引入而编过，只有直接包含它的 test_binary_code.cpp 报错。

#include <iostream>

int main() {
    std::cout << "test_headers: PASSED\n";
    return 0;
}
