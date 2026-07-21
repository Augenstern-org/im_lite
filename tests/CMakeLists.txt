# 纯头文件测试（无平台依赖）

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(test_io_status test_io_status.cpp)
target_include_directories(test_io_status PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(test_io_status PRIVATE /utf-8)
endif()
add_test(NAME IoStatus COMMAND test_io_status)

add_executable(test_binary_code test_binary_code.cpp)
target_include_directories(test_binary_code PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(test_binary_code PRIVATE /utf-8)
endif()
add_test(NAME BinaryCode COMMAND test_binary_code)

add_executable(test_message test_message.cpp)
target_include_directories(test_message PRIVATE ${CMAKE_SOURCE_DIR}/src)
if(MSVC)
    target_compile_options(test_message PRIVATE /utf-8)
endif()
add_test(NAME Message COMMAND test_message)
