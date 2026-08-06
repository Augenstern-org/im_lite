cmake_minimum_required(VERSION 3.14)
project(com_lite LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 全项目仅有的两个头文件搜索根，由 com_lite_base 统一提供；
# 其余目标一律通过 target_link_libraries 继承，不再自行声明 include 路径。
#
#   src/     —— 项目自身头文件。包含时写成从 src/ 起的完整路径，
#                例如 "common/encoder.h"、"server/core/core.h"。
#                同一个头文件在任何位置都只有这一种写法，不使用相对路径。
#   include/ —— 第三方单头文件库（json.hpp）。
add_library(com_lite_base INTERFACE)
target_include_directories(com_lite_base INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)
if(MSVC)
    target_compile_options(com_lite_base INTERFACE /utf-8)
endif()

# 警告闸门：GCC / Clang 下全项目统一，不随构建选项开关。
# 这三个是 GCC / Clang 的拼写，必须按编译器分支给 —— cl.exe 会把 -Wall 映射成 /Wall
#（噪声极大），并以 D9002 拒掉 -Wextra / -Wswitch-enum，无条件下发会让上面那个
# MSVC 分支形同虚设。此处不补 MSVC 侧的等价警告开关：本项目是 epoll-only，
# 没人能在 MSVC 上编译验证，加了也是没验证过的标志。
# -Wswitch-enum 与 -Wall 里的 -Wswitch 不同 —— 即使 switch 写了 default 分支，
# 漏掉某个枚举值仍然报警告，新增 Opcode / Status 时不会被 default 悄悄吃掉。
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(com_lite_base INTERFACE -Wall -Wextra -Wswitch-enum)
endif()

# 消毒器闸门：默认关闭，常规构建完全不受影响。
# 打开后 ASan + UBSan 一并生效，用于把内存/未定义行为缺陷暴露成 ctest 失败。
option(COM_LITE_SANITIZE "Enable ASan+UBSan" OFF)
if(COM_LITE_SANITIZE)
    include(CheckCXXSourceCompiles)

    # 编译期探测不足以说明问题 —— 消毒器运行库缺失时编译照样通过、链接才失败，
    # 因此编译标志与链接标志都要带上，让探测走完整的编译 + 链接。
    set(COM_LITE_SANITIZE_FLAGS -fsanitize=address,undefined)
    set(CMAKE_REQUIRED_FLAGS ${COM_LITE_SANITIZE_FLAGS})
    set(CMAKE_REQUIRED_LINK_OPTIONS ${COM_LITE_SANITIZE_FLAGS})
    check_cxx_source_compiles("int main() { return 0; }" COM_LITE_HAVE_SANITIZE)
    unset(CMAKE_REQUIRED_FLAGS)
    unset(CMAKE_REQUIRED_LINK_OPTIONS)

    if(NOT COM_LITE_HAVE_SANITIZE)
        message(FATAL_ERROR "COM_LITE_SANITIZE=ON 但当前工具链不支持 -fsanitize=address,undefined")
    endif()

    # 一律挂在 com_lite_base 上，不写进 CMAKE_CXX_FLAGS ——
    # 后者会泄漏进 try_compile，且无法按目标限定范围。
    target_compile_options(com_lite_base INTERFACE
            ${COM_LITE_SANITIZE_FLAGS}
            # GCC 13 的 UBSan 默认「报告后继续执行」，进程仍以 0 退出，
            # ctest 会判定通过 —— 没有这一行整个闸门形同虚设。
            -fno-sanitize-recover=all
            -fno-omit-frame-pointer
            -g
            -O1
    )
    target_link_options(com_lite_base INTERFACE ${COM_LITE_SANITIZE_FLAGS})
    target_compile_definitions(com_lite_base INTERFACE _GLIBCXX_ASSERTIONS)
endif()

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
