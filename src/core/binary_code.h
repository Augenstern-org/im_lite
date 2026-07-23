//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_BINARY_CODE_H
#define COM_LITE_BINARY_CODE_H
#include <cstdint>

namespace core {
    /* -------------------------------------------------------
     * 定义转换规则
     *
     *      enum                =>         binary code
     * -------------------------------------------------------
     * Opcode::ack              =>              0
     * Opcode::request          =>              1
     * Opcode::response         =>              2
     *
     * Status::ok               =>              0
     * Status::fail             =>              1
     *
     * -------------------------------------------------------         *
     */

    enum class Opcode : uint8_t {
        _init_   = 0xff,
        ack      = 0,
        request  = 1,
        response = 2
    };

    enum class Status : uint8_t {
        _init_ = 0xff,
        ok     = 0,
        fail   = 1
    };
} // core

#endif //COM_LITE_BINARY_CODE_H
