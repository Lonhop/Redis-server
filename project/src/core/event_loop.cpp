#include "core/event_loop.h"
#include <stdexcept>
#include <map>

namespace redis::core {
    namespace {
        thread_local DWORD tls_last_error = 0;
        EventResult win32ErrorToEventResult(DWORD error) {
            switch (error) {
                case ERROR_SUCCESS:
                    return EventResult::SUCCESS;
                case WAIT_TIMEOUT:
                    return EventResult::TIMEOUT;
                case ERROR_OPERATION_ABORTED:
                    return EventResult::ABORTED;
                default:
                    return EventResult::ERROR;
            }
        }

        std::string getErrorMessage(DWORD error) {
            LPSTR messageBuffer = nullptr;
            size_t size = FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, error, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPSTR)&messageBuffer, 0, nullptr);
            std::string message(messageBuffer, size);
            LocalFree(messageBuffer);
            return message;
        }
    }
    std::unique_ptr<EventLoop> createEventLoop(LoopType type) {
        switch (type) {
            case LoopType::IOCP:
                throw std::runtime_error("IOCP not implemented");
            case LoopType::SELECT:
                throw std::runtime_error("SELECT not implemented");
            default:
                throw std::invalid_argument("Unknown type");
        }
    }

}