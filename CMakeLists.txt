cmake_minimum_required(VERSION 3.12)
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

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)
