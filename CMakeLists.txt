cmake_minimum_required(VERSION 3.12)
project(com_lite LANGUAGES CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_CXX_STANDARD 20)

include_directories(include)

enable_testing()

add_subdirectory(src)
add_subdirectory(tests)