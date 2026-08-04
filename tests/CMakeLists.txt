# 纯头文件测试（无平台依赖）
# 头文件搜索路径与 /utf-8 均由 com_lite_base 提供，各测试不再单独声明。

function(add_com_lite_test test_name target_name)
    add_executable(${target_name} ${target_name}.cpp)
    target_link_libraries(${target_name} PRIVATE com_lite_base)
    add_test(NAME ${test_name} COMMAND ${target_name})
endfunction()

add_com_lite_test(IoStatus   test_io_status)
add_com_lite_test(BinaryCode test_binary_code)
add_com_lite_test(Message    test_message)
add_com_lite_test(Encoder    test_encoder)
add_com_lite_test(Decoder    test_decoder)
add_com_lite_test(RoundTrip  test_roundtrip)
add_com_lite_test(UsersGroup  test_users_group)
add_com_lite_test(Assembler   test_assembler)
add_executable(test_controller test_controller.cpp ${CMAKE_SOURCE_DIR}/src/server/controller/controller.cpp)
target_link_libraries(test_controller PRIVATE com_lite_base)
add_test(NAME Controller COMMAND test_controller)

# 头文件自足性：src/ 下每个头文件各生成一个只包含它自己的翻译单元，
# 全部链进 test_headers。断言在编译期完成 —— 头文件若少写自己直接用到的
# #include，本目标即编不过，不会再靠包含顺序侥幸通过。
file(GLOB_RECURSE COM_LITE_HEADERS CONFIGURE_DEPENDS ${CMAKE_SOURCE_DIR}/src/*.h)

set(COM_LITE_HEADER_TUS "")
foreach(header IN LISTS COM_LITE_HEADERS)
    file(RELATIVE_PATH COM_LITE_HEADER ${CMAKE_SOURCE_DIR}/src ${header})
    string(REPLACE "/" "_" tu_name ${COM_LITE_HEADER})
    string(REPLACE "." "_" tu_name ${tu_name})
    set(tu ${CMAKE_CURRENT_BINARY_DIR}/self_contained/${tu_name}.cpp)
    configure_file(header_self_contained.cpp.in ${tu} @ONLY)
    list(APPEND COM_LITE_HEADER_TUS ${tu})
endforeach()

add_executable(test_headers test_headers.cpp ${COM_LITE_HEADER_TUS})
target_link_libraries(test_headers PRIVATE com_lite_base)
add_test(NAME Headers COMMAND test_headers)
