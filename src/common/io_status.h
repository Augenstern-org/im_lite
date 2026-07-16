//
// Created by Neuroil on 2026/7/15.
//

#ifndef COM_LITE_IO_STATUS_H
#define COM_LITE_IO_STATUS_H

namespace types {
    enum class IoStatus {
        Ok,
        Closed,
        WouldBlock,
        Interrupted,
        FrameTooLong,
        Timeout,
        Error,
    };

    // 面向日志 / 诊断的可读名称。永不返回 nullptr。
    inline const char* toString(IoStatus s) noexcept {
        switch (s) {
            case IoStatus::Ok:          return "Ok";
            case IoStatus::Closed:      return "Closed";
            case IoStatus::WouldBlock:  return "WouldBlock";
            case IoStatus::Interrupted: return "Interrupted";
            case IoStatus::FrameTooLong: return "FrameTooLong";
            case IoStatus::Timeout:     return "Timeout";
            case IoStatus::Error:       return "Error";
        }
        return "Unknown";
    }
}

#endif //COM_LITE_IO_STATUS_H
