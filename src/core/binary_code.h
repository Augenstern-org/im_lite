//
// Created by Neuroil on 2026/7/16.
//

#ifndef COM_LITE_BINARY_CODE_H
#define COM_LITE_BINARY_CODE_H
#include <cstdint>

namespace core {
    enum class Opcode : uint8_t {
        ack,
        request,
        response
    };

    enum class Status : uint8_t {
        ok,
        fail
    };
} // core

#endif //COM_LITE_BINARY_CODE_H
