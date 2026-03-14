#include "core/iocp_poller.h"
#include <algorithm>
#include <system_error>

namespace redis::core {
    IocpPoller::IocpPoller() : config_() {
        iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (!iocp_) {
            last_error_ = GetLastError();
            throw std::system_error(last_error_, std::system_category(),"Failed to create port");
        }
        timer_queue_ = CreateTimerQueue();
        if (!timer_queue_) {
            last_error_ = GetLastError();
            CloseHandle(iocp_);
            throw std::system_error(last_error_, std::system_category(),"Failed to create timer queue");
        }
        max_worker_threads_ = config_.worker_threads;
        if (max_worker_threads_ == 0) {
            SYSTEM_INFO sysinfo;
            GetSystemInfo(&sysinfo);
            max_worker_threads_ = sysinfo.dwNumberOfProcessors * 2;
        }
    }
    IocpPoller::IocpPoller(const EventLoopConfig& config) : config_(config) {
        iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (!iocp_) {
            last_error_ = GetLastError();
            throw std::system_error(last_error_, std::system_category(),"Failed to create port");
        }
        timer_queue_ = CreateTimerQueue();
        if (!timer_queue_) {
            last_error_ = GetLastError();
            CloseHandle(iocp_);
            throw std::system_error(last_error_, std::system_category(),"Failed to create timer queue");
        }
        max_worker_threads_ = config_.worker_threads;
        if (max_worker_threads_ == 0) {
            SYSTEM_INFO sysinfo;
            GetSystemInfo(&sysinfo);
            max_worker_threads_ = sysinfo.dwNumberOfProcessors * 2;
        }
    }
    IocpPoller::~IocpPoller() {
        stop();
        for (auto& [fd, ctx] : sockets_) {
            cleanupSocket(ctx.get());
        }
        sockets_.clear();
        for (auto& [id, timer] : timers_) {
            DeleteTimerQueueTimer(timer_queue_, timer, nullptr);
        }
        timers_.clear();
        if (timer_queue_) {
            DeleteTimerQueueEx(timer_queue_, nullptr);
        }
        if (iocp_) {
            CloseHandle(iocp_);
        }
    }
    IocpPoller::IocpPoller(IocpPoller&& other) noexcept : iocp_(other.iocp_)
        , timer_queue_(other.timer_queue_)
        , running_(other.running_.load())
        , total_events_(other.total_events_.load())
        , last_error_(other.last_error_)
        , sockets_(std::move(other.sockets_))
        , timers_(std::move(other.timers_))
        , overlapped_pool_(std::move(other.overlapped_pool_))
        , config_(std::move(other.config_))
        , max_worker_threads_(other.max_worker_threads_)
        , total_operations_(other.total_operations_)
        , failed_operations_(other.failed_operations_)
        , lpfnAcceptEx(other.lpfnAcceptEx)
        , lpfnConnectEx(other.lpfnConnectEx) {
        other.iocp_ = nullptr;
        other.timer_queue_ = nullptr;
    }
    IocpPoller& IocpPoller::operator=(IocpPoller&& other) noexcept {
        if (this != &other) {
            iocp_ = other.iocp_;
            timer_queue_ = other.timer_queue_;
            running_.store(other.running_.load());
            total_events_.store(other.total_events_.load());
            last_error_ = other.last_error_;
            sockets_ = std::move(other.sockets_);
            timers_ = std::move(other.timers_);
            overlapped_pool_ = std::move(other.overlapped_pool_);
            config_ = std::move(other.config_);
            max_worker_threads_ = other.max_worker_threads_;
            total_operations_ = other.total_operations_;
            failed_operations_ = other.failed_operations_;
            lpfnAcceptEx = other.lpfnAcceptEx;
            lpfnConnectEx = other.lpfnConnectEx;
            other.iocp_ = nullptr;
            other.timer_queue_ = nullptr;
        }
        return *this;
    }
    bool IocpPoller::loadExtensionFunctions(SOCKET fd) noexcept {
        DWORD bytes = 0;
        GUID guidAcceptEx = WSAID_ACCEPTEX;
        GUID guidConnectEx = WSAID_CONNECTEX;
        int result = WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidAcceptEx, sizeof(guidAcceptEx), &lpfnAcceptEx, sizeof(lpfnAcceptEx), &bytes, nullptr, nullptr);
        if (result != 0) {
            last_error_ = WSAGetLastError();
            return false;
        }
        result = WSAIoctl(fd, SIO_GET_EXTENSION_FUNCTION_POINTER, &guidConnectEx, sizeof(guidConnectEx), &lpfnConnectEx, sizeof(lpfnConnectEx), &bytes, nullptr, nullptr);
        if (result != 0) {
            last_error_ = WSAGetLastError();
            return false;
        }
        return true;
    }
    bool IocpPoller::associateSocket(SOCKET fd, uintptr_t completion_key) noexcept {
        HANDLE result = CreateIoCompletionPort((HANDLE)fd, iocp_, completion_key, 0);
        if (!result) {
            last_error_ = GetLastError();
            return false;
        }
        return true;
    }
    bool IocpPoller::addSocket(int fd, SocketMode mode, uintptr_t completion_key) noexcept {
        SOCKET sock = (SOCKET)fd;
        if (!associateSocket(sock, completion_key)) {
            return false;
        }
        if (!lpfnAcceptEx || !lpfnConnectEx) {
            loadExtensionFunctions(sock);
        }
        auto ctx = std::make_unique<SocketContext>(sock);
        ctx->mode = mode;
        ctx->completion_key = completion_key;
        {
            std::unique_lock lock(sockets_mutex_);
            sockets_[fd] = std::move(ctx);
        }
        auto* ctx_ptr = sockets_[fd].get();
        if (mode & SocketMode::READ) {
            OverlappedEx* ov = new OverlappedEx();
            startAsyncRead(ctx_ptr, ov, nullptr, 0);
        }
        return true;
    }
    bool IocpPoller::updateSocket(int fd, SocketMode mode) noexcept {
        std::unique_lock lock(sockets_mutex_);
        auto it = sockets_.find(fd);
        if (it == sockets_.end()) {
            last_error_ = ERROR_INVALID_HANDLE;
            return false;
        }
        SocketMode old_mode = it->second->mode;
        it->second->mode = mode;
        if (!(old_mode & SocketMode::READ) && (mode & SocketMode::READ)) {
            OverlappedEx* ov = new OverlappedEx();
            startAsyncRead(it->second.get(), ov, nullptr, 0);
        }
        return true;
    }
    bool IocpPoller::removeSocket(int fd) noexcept {
        std::unique_lock lock(sockets_mutex_);
        auto it = sockets_.find(fd);
        if (it == sockets_.end()) {
            return false;
        }
        CancelIo((HANDLE)(it->second->fd));
        cleanupSocket(it->second.get());
        sockets_.erase(it);
        return true;
    }
    SocketMode IocpPoller::getSocketMode(int fd) const noexcept {
        std::shared_lock lock(sockets_mutex_);
        auto it = sockets_.find(fd);
        if (it == sockets_.end()) {
            return SocketMode::NONE;
        }
        return it->second->mode;
    }
    bool IocpPoller::asyncRead(int fd, void* buffer, DWORD size, uintptr_t completion_key) noexcept {
        std::shared_lock lock(sockets_mutex_);
        auto it = sockets_.find(fd);
        if (it == sockets_.end()) {
            last_error_ = ERROR_INVALID_HANDLE;
            return false;
        }
        OverlappedEx* ov = new OverlappedEx();
        ov->completion_key = completion_key;
        return startAsyncRead(it->second.get(), ov, buffer, size);
    }
    bool IocpPoller::startAsyncRead(SocketContext* ctx, OverlappedEx* ov, void* buffer, DWORD size) {
        if (ctx->pending_read) {
            delete ov;
            return true;
        }
        ov->op = OpType::READ;
        ov->fd = ctx->fd;
        ov->event_type = EventType::READ;
        ov->wbuf.buf = buffer ? static_cast<char*>(buffer) : ov->buffer;
        ov->wbuf.len = size ? size : sizeof(ov->buffer);
        DWORD flags = 0;
        DWORD bytes = 0;
        int result = WSARecv(ctx->fd, &ov->wbuf, 1, &bytes, &flags, ov, nullptr);
        if (result == 0 || WSAGetLastError() == WSA_IO_PENDING) {
            ctx->pending_read = true;
            {
                std::lock_guard<std::mutex> lock(ctx->ops_mutex);
                ctx->active_ops.emplace_back(ov);
            }
            total_operations_++;
            return true;
        }
        else {
            last_error_ = WSAGetLastError();
            delete ov;
            failed_operations_++;
            return false;
        }
    }
    bool IocpPoller::asyncWrite(int fd, const void* buffer, DWORD size, uintptr_t completion_key) noexcept {
        std::shared_lock lock(sockets_mutex_);
        auto it = sockets_.find(fd);
        if (it == sockets_.end()) {
            last_error_ = ERROR_INVALID_HANDLE;
            return false;
        }
        OverlappedEx* ov = new OverlappedEx();
        ov->completion_key = completion_key;
        return startAsyncWrite(it->second.get(), ov, buffer, size);
    }
    bool IocpPoller::startAsyncWrite(SocketContext* ctx, OverlappedEx* ov, const void* buffer, DWORD size) {
        if (ctx->pending_write) {
            delete ov;
            return true;
        }
        ov->op = OpType::WRITE;
        ov->fd = ctx->fd;
        ov->event_type = EventType::WRITE;
        if (buffer) {
            ov->wbuf.buf = const_cast<char*>(static_cast<const char*>(buffer));
            ov->wbuf.len = size;
        }
        else {
            ov->wbuf.buf = ov->buffer;
            ov->wbuf.len = sizeof(ov->buffer);
        }
        DWORD bytes = 0;
        int result = WSASend(ctx->fd, &ov->wbuf, 1, &bytes, 0, ov, nullptr);
        if (result == 0 || WSAGetLastError() == WSA_IO_PENDING) {
            ctx->pending_write = true;
            {
                std::lock_guard<std::mutex> lock(ctx->ops_mutex);
                ctx->active_ops.emplace_back(ov);
            }
            total_operations_++;
            return true;
        }
        else {
            last_error_ = WSAGetLastError();
            delete ov;
            failed_operations_++;
            return false;
        }
    }
    bool IocpPoller::asyncAccept(int listen_fd, int* accept_fd, sockaddr* addr, socklen_t addrlen, uintptr_t completion_key) noexcept {
        std::shared_lock lock(sockets_mutex_);
        auto it = sockets_.find(listen_fd);
        if (it == sockets_.end()) {
            last_error_ = ERROR_INVALID_HANDLE;
            return false;
        }
        OverlappedEx* ov = new OverlappedEx();
        ov->completion_key = completion_key;
        ov->accept_fd_ptr = accept_fd;
        ov->addr_len = addrlen;
        startAsyncAccept(it->second.get(), ov);
        return true;
    }
    void IocpPoller::startAsyncAccept(SocketContext* ctx, OverlappedEx* ov) {
        ov->op = OpType::ACCEPT;
        ov->fd = ctx->fd;
        ov->event_type = EventType::ACCEPT;
        SOCKET accept_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (accept_socket == INVALID_SOCKET) {
            last_error_ = WSAGetLastError();
            delete ov;
            failed_operations_++;
            return;
        }
        *ov->accept_fd_ptr = accept_socket;
        DWORD bytes = 0;
        BOOL result = lpfnAcceptEx(ctx->fd, accept_socket, ov->buffer, 0, sizeof(sockaddr_storage) + 16, sizeof(sockaddr_storage) + 16, &bytes, ov);
        if (!result && WSAGetLastError() != WSA_IO_PENDING) {
            last_error_ = WSAGetLastError();
            closesocket(accept_socket);
            delete ov;
            failed_operations_++;
            return;
        }
        ctx->pending_accept = true;
        {
            std::lock_guard<std::mutex> lock(ctx->ops_mutex);
            ctx->active_ops.emplace_back(ov);
        }
        total_operations_++;
    }
    bool IocpPoller::asyncConnect(int fd, const sockaddr* addr, socklen_t addrlen, uintptr_t completion_key) noexcept {
        std::shared_lock lock(sockets_mutex_);
        auto it = sockets_.find(fd);
        if (it == sockets_.end()) {
            last_error_ = ERROR_INVALID_HANDLE;
            return false;
        }
        OverlappedEx* ov = new OverlappedEx();
        ov->completion_key = completion_key;
        ov->op = OpType::CONNECT;
        ov->fd = fd;
        ov->event_type = EventType::CONNECT;
        memcpy(&ov->addr, addr, addrlen);
        ov->addr_len = addrlen;
        BOOL result = lpfnConnectEx(fd, addr, addrlen, nullptr, 0, nullptr, ov);
        if (!result && WSAGetLastError() != WSA_IO_PENDING) {
            last_error_ = WSAGetLastError();
            delete ov;
            failed_operations_++;
            return false;
        }
        it->second->pending_connect = true;
        {
            std::lock_guard<std::mutex> lock(it->second->ops_mutex);
            it->second->active_ops.emplace_back(ov);
        }
        total_operations_++;
        return true;
    }
    bool IocpPoller::cancelIO(int fd) noexcept {
        std::shared_lock lock(sockets_mutex_);
        auto it = sockets_.find(fd);
        if (it == sockets_.end()) {
            return false;
        }
        return CancelIo((HANDLE)fd) != 0;
    }
    struct TimerContext {
        IocpPoller* poller;
        uint64_t timer_id;
        bool periodic;
    };
    void CALLBACK TimerCallback(PVOID param, BOOLEAN) {
        auto* ctx = static_cast<TimerContext*>(param);
        ctx->poller->postUserEvent(ctx->timer_id, ctx);
    }
    uint64_t IocpPoller::createTimer(Duration interval, bool periodic) noexcept {
        static std::atomic<uint64_t> next_timer_id{1};
        uint64_t timer_id = next_timer_id.fetch_add(1);
        auto* ctx = new TimerContext{this, timer_id, periodic};
        HANDLE timer = nullptr;
        BOOL result = CreateTimerQueueTimer(&timer, timer_queue_, TimerCallback, ctx, interval.count(), periodic ? interval.count() : 0, WT_EXECUTEDEFAULT);
        if (!result) {
            last_error_ = GetLastError();
            delete ctx;
            return 0;
        }
        {
            std::lock_guard<std::mutex> lock(timers_mutex_);
            timers_[timer_id] = timer;
        }
        return timer_id;
    }
    bool IocpPoller::cancelTimer(uint64_t timer_id) noexcept {
        std::lock_guard<std::mutex> lock(timers_mutex_);
        auto it = timers_.find(timer_id);
        if (it == timers_.end()) {
            return false;
        }
        BOOL result = DeleteTimerQueueTimer(timer_queue_, it->second, nullptr);
        timers_.erase(it);
        return result != 0;
    }
    bool IocpPoller::postUserEvent(uintptr_t completion_key, void* user_data) noexcept {
        BOOL result = PostQueuedCompletionStatus(iocp_, 0, completion_key, nullptr);
        if (!result) {
            last_error_ = GetLastError();
            return false;
        }
        return true;
    }
    void IocpPoller::processCompletion(OVERLAPPED* ov, DWORD bytes, DWORD error, EventHandler handler) {
        auto* ex = static_cast<OverlappedEx*>(ov);
        std::shared_lock lock(sockets_mutex_);
        auto it = sockets_.find(ex->fd);
        EventData event;
        event.type = ex->event_type;
        event.fd = ex->fd;
        event.completion_key = ex->completion_key;
        event.bytes_transferred = bytes;
        event.error_code = error;
        event.user_data = nullptr;
        if (it != sockets_.end()) {
            auto* ctx = it->second.get();
            switch (ex->op) {
                case OpType::READ:
                    ctx->pending_read = false;
                    break;
                case OpType::WRITE:
                    ctx->pending_write = false;
                    break;
                case OpType::ACCEPT:
                    ctx->pending_accept = false;
                    break;
                case OpType::CONNECT:
                    ctx->pending_connect = false;
                    break;
                default:
                    break;
            }
            {
                std::lock_guard<std::mutex> ops_lock(ctx->ops_mutex);
                auto ops_it = std::find_if(ctx->active_ops.begin(), ctx->active_ops.end(),
                    [ex](const auto& ptr) { return ptr.get() == ex; });
                if (ops_it != ctx->active_ops.end()) {
                    ctx->active_ops.erase(ops_it);
                }
            }
        }
        total_events_.fetch_add(1);
        if (handler) {
            handler(event);
        }
        if (it != sockets_.end() && error == 0) {
            auto* ctx = it->second.get();
            if (event.type == EventType::READ && (ctx->mode & SocketMode::READ)) {
                OverlappedEx* new_ov = new OverlappedEx();
                startAsyncRead(ctx, new_ov, nullptr, 0);
            }
        }
        delete ex;
    }
    void IocpPoller::cleanupSocket(SocketContext* ctx) {
        CancelIo((HANDLE)ctx->fd);
        std::lock_guard<std::mutex> lock(ctx->ops_mutex);
        ctx->active_ops.clear();
        closesocket(ctx->fd);
    }
    void IocpPoller::checkTimeouts() {
        auto now = std::chrono::steady_clock::now();
        auto timeout_duration = std::chrono::milliseconds(config_.operation_timeout);
        std::vector<std::pair<SocketContext*, OverlappedEx*>> timed_out_ops;
        {
            std::shared_lock lock(sockets_mutex_);
            for (auto& [fd, ctx] : sockets_) {
                std::lock_guard<std::mutex> ops_lock(ctx->ops_mutex);
                for (auto& op : ctx->active_ops) {
                    if (now - op->start_time > timeout_duration) {
                        timed_out_ops.emplace_back(ctx.get(), op.get());
                    }
                }
            }
        }
        for (auto& [ctx, op] : timed_out_ops) {
            CancelIoEx((HANDLE)ctx->fd, op);
            EventData event;
            event.type = op->event_type;
            event.fd = ctx->fd;
            event.completion_key = op->completion_key;
            event.bytes_transferred = 0;
            event.error_code = ERROR_TIMEOUT;
            event.user_data = nullptr;
            {
                std::lock_guard<std::mutex> ops_lock(ctx->ops_mutex);
                auto it = std::find_if(ctx->active_ops.begin(), ctx->active_ops.end(),
                    [op](const auto& ptr) { return ptr.get() == op; });
                if (it != ctx->active_ops.end()) {
                    ctx->active_ops.erase(it);
                }
            }
            delete op;
            failed_operations_++;
        }
        if (!timed_out_ops.empty()) {
            total_events_.fetch_add(timed_out_ops.size());
        }
    }
    EventResult IocpPoller::pollOnce(Duration timeout, EventHandler handler) noexcept {
        static size_t poll_count = 0;
        if (++poll_count % 1000 == 0) {
            checkTimeouts();
        }
        DWORD bytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* ov = nullptr;
        DWORD ms = timeout == Duration::max() ? INFINITE : static_cast<DWORD>(timeout.count());
        BOOL result = GetQueuedCompletionStatus(iocp_, &bytes, &key, &ov, ms);
        if (result) {
            if (ov) {
                processCompletion(ov, bytes, 0, handler);
            }
            else {
                EventData event;
                event.type = EventType::USER;
                event.completion_key = static_cast<uintptr_t>(key);
                total_events_.fetch_add(1);
                if (handler) {
                    handler(event);
                }
            }
            return EventResult::SUCCESS;
        }
        else {
            DWORD error = GetLastError();
            if (error == WAIT_TIMEOUT) {
                return EventResult::TIMEOUT;
            }
            if (ov) {
                processCompletion(ov, 0, error, handler);
            }
            last_error_ = error;
            return EventResult::ERROR;
        }
    }
    void IocpPoller::run(EventHandler handler) noexcept {
        running_.store(true);
        uint64_t error_count = 0;
        const uint64_t MAX_CONSECUTIVE_ERRORS = 10;
        while (running_.load()) {
            auto result = pollOnce(Duration::max(), handler);
            switch (result) {
                case EventResult::SUCCESS:
                case EventResult::TIMEOUT:
                case EventResult::ABORTED:
                    error_count = 0;
                    break;
                case EventResult::ERROR:
                    error_count++;
                    if (error_count >= MAX_CONSECUTIVE_ERRORS) {
                        running_.store(false);
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    break;
                default:
                    error_count++;
                    break;
            }
        }
    }

    void IocpPoller::stop() noexcept {
        running_.store(false);
        PostQueuedCompletionStatus(iocp_, 0, 0, nullptr);
    }
}