#pragma once

#include <liburing.h>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <memory_pool.h>

class AsyncIO {
    struct io_uring ring;
    size_t queue_depth;
    std::unique_ptr<MemoryPool> memory_pool;
    static constexpr size_t BUFFER_SIZE = 64 * 1024;
    static constexpr size_t BUFFER_COUNT = 128;

public:
    AsyncIO(size_t depth = 256);
    ~AsyncIO();

    AsyncIO(const AsyncIO&) = delete;
    AsyncIO& operator=(const AsyncIO&) = delete;

    AsyncIO(AsyncIO&& other) noexcept;
    AsyncIO& operator=(AsyncIO&& other) noexcept;

    struct AsyncCopyContext {
        int src_fd;
        int dst_fd;
        uint64_t offset;
        uint64_t total_size;
        uint64_t copied;
        std::function<void(uint64_t)> progress_callback;
        std::function<void()> completion_callback;
        std::function<void(const std::string&)> error_callback;
        char* buffer;
        size_t buffer_size;
        bool is_reading;
        MemoryPool* pool;
        std::weak_ptr<AsyncCopyContext> self;
    };

    void async_copy(int src_fd, int dst_fd, uint64_t file_size,
                    std::function<void(uint64_t)> progress_cb,
                    std::function<void()> completion_cb,
                    std::function<void(const std::string&)> error_cb);

    void process_events();

private:
    void submit_read(std::shared_ptr<AsyncCopyContext> ctx);
    void submit_write(std::shared_ptr<AsyncCopyContext> ctx, size_t bytes_to_write);
    void handle_completion(std::shared_ptr<AsyncCopyContext> ctx, ssize_t bytes_processed);
};
