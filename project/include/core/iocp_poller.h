#pragma once

#include "event_loop.h"

#ifdef _WIN32 // Проверка на windows
#include <winsock2.h>
#include <windows.h>
#include <mswsock.h>
#include <vector>
#include <unordered_map>
#include <memory>
#include <mutex>
#include <atomic>

namespace redis::core {
    // Типы операций
    enum class OpType{
        NONE = 0,
        ACCEPT = 1,
        READ = 2,
        WRITE = 3,
        CONNECT = 4,
        TIMER = 5,
        USER = 6
    };

    // Расширенная структура для IOCP
    struct OverlappedEx : public OVERLAPPED // from minwinbase.h встроена в windows kits
    {
        OpType op = OpType::NONE;   // тип операции
        int fd = -1;    // файловый дескриптор
        uintptr_t completion_key = 0;   // пользовательский ключ
        WSABUF wbuf{};  // буфер для WSASend/WSARecv
        char buffer[8192];  // встроенный буфер
        sockaddr_storage addr{};    // для accept/connect
        int addr_len = 0;   // длина адреса
        int* accept_fd_ptr = nullptr;   // указатель на новый сокет для accept
        EventType event_type = EventType::NONE; // тип события для callback

        OverlappedEx() {
            Internal = 0;
            InternalHigh = 0;
            Offset = 0;
            OffsetHigh = 0;
            hEvent = nullptr;
        }
    };


    // Контекст сокета для iocp
    struct SocketContext {
        SOCKET fd = INVALID_SOCKET;
        SocketMode mode = SocketMode::NONE;
        uintptr_t completion_key = 0;
        bool pending_read = false;
        bool pending_write = false;
        bool pending_accept = false;
        bool pending connect = false;
        std::vector<std::unique_ptr<OverlappedEx>> active_ops;
        std::mutex ops_mutex;
        explicit SocketContext(SOCKET f) : fd(f) {}
    };
}