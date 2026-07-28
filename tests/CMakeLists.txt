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
