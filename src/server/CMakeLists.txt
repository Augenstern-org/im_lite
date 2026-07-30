add_library(core STATIC core/core.cpp core/connections.cpp)
target_link_libraries(core PUBLIC com_lite_base)

add_executable(server main.cpp controller/controller.cpp)
target_link_libraries(server PRIVATE core)
