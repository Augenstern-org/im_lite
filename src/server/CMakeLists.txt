add_library(common INTERFACE)
target_include_directories(common INTERFACE ..)

add_library(core STATIC core/core.cpp core/connections.cpp)
target_include_directories(core PUBLIC ..)
target_link_libraries(core PUBLIC common)

add_executable(server main.cpp)
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/src/server ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(server PRIVATE core)