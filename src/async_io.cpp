#include "../include/async_io.h"
#include <stdexcept>
#include <cstring>
#include <unistd.h>

AsyncIO::AsyncIO(size_t depth) : queue_depth(depth) {
    if (io_uring_queue_init(queue_depth, &ring, 0) < 0) {
        throw std::runtime_error("Failed to initialize io_uring");
    }

    buffers.reserve(BUFFER_COUNT);
    for (size_t i = 0; i < BUFFER_COUNT; ++i) {
        buffers.push_back(std::make_unique<char[]>(BUFFER_SIZE));
    }
}

AsyncIO::~AsyncIO() {
    io_uring_queue_exit(&ring);
}

AsyncIO::AsyncIO(AsyncIO&& other) noexcept {
    ring = other.ring;
    queue_depth = other.queue_depth;
    buffers = std::move(other.buffers);
    other.queue_depth = 0;
}

AsyncIO& AsyncIO::operator=(AsyncIO&& other) noexcept {
    if (this != &other) {
        io_uring_queue_exit(&ring);
        ring = other.ring;
        queue_depth = other.queue_depth;
        buffers = std::move(other.buffers);
        other.queue_depth = 0;
    }
    return *this;
}

void AsyncIO::async_copy(int src_fd, int dst_fd, uint64_t file_size,
                        std::function<void(uint64_t)> progress_cb,
                        std::function<void()> completion_cb,
                        std::function<void(const std::string&)> error_cb) {
    auto ctx = std::make_unique<AsyncCopyContext>();
    ctx->src_fd = src_fd;
    ctx->dst_fd = dst_fd;
    ctx->offset = 0;
    ctx->total_size = file_size;
    ctx->copied = 0;
    ctx->progress_callback = std::move(progress_cb);
    ctx->completion_callback = std::move(completion_cb);
    ctx->error_callback = std::move(error_cb);
    ctx->buffer = buffers[0].get();
    ctx->buffer_size = BUFFER_SIZE;
    ctx->is_reading = true;

    submit_read(std::move(ctx));
}

void AsyncIO::process_events() {
    struct io_uring_cqe* cqe;
    unsigned head;
    unsigned count = 0;

    io_uring_for_each_cqe(&ring, head, cqe) {
        count++;
        auto* ctx = reinterpret_cast<AsyncCopyContext*>(io_uring_cqe_get_data(cqe));
        
        if (cqe->res < 0) {
            if (ctx && ctx->error_callback) {
                ctx->error_callback("Async I/O operation failed: " + std::string(strerror(-cqe->res)));
            }
            delete ctx;
        } else {
            if (ctx) {
                handle_completion(ctx, cqe->res);
            }
        }
    }

    if (count > 0) {
        io_uring_cq_advance(&ring, count);
    }
}

void AsyncIO::submit_read(std::unique_ptr<AsyncCopyContext> ctx) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        ctx->error_callback("Failed to get submission queue entry");
        return;
    }

    size_t read_size = std::min(ctx->buffer_size, static_cast<size_t>(ctx->total_size - ctx->offset));
    io_uring_prep_read(sqe, ctx->src_fd, ctx->buffer, read_size, ctx->offset);
    io_uring_sqe_set_data(sqe, ctx.release());
    io_uring_submit(&ring);
}

void AsyncIO::submit_write(std::unique_ptr<AsyncCopyContext> ctx, size_t bytes_to_write) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        ctx->error_callback("Failed to get submission queue entry");
        return;
    }

    io_uring_prep_write(sqe, ctx->dst_fd, ctx->buffer, bytes_to_write, ctx->offset);
    io_uring_sqe_set_data(sqe, ctx.release());
    io_uring_submit(&ring);
}

void AsyncIO::handle_completion(AsyncCopyContext* ctx, ssize_t bytes_processed) {
    if (ctx->is_reading) {
        ctx->copied += bytes_processed;
        
        if (bytes_processed == 0 || ctx->copied >= ctx->total_size) {
            if (ctx->progress_callback) {
                ctx->progress_callback(ctx->copied);
            }
            if (ctx->completion_callback) {
                ctx->completion_callback();
            }
            delete ctx;
            return;
        }

        ctx->is_reading = false;
        submit_write(std::unique_ptr<AsyncCopyContext>(ctx), bytes_processed);
    } else {
        ctx->offset += bytes_processed;
        
        if (ctx->progress_callback) {
            ctx->progress_callback(ctx->copied);
        }

        if (ctx->copied >= ctx->total_size) {
            if (ctx->completion_callback) {
                ctx->completion_callback();
            }
            delete ctx;
            return;
        }

        ctx->is_reading = true;
        submit_read(std::unique_ptr<AsyncCopyContext>(ctx));
    }
}
