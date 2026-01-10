#pragma once

#include "config.h"
#include "file_utils.h"
#include "async_io.h"
#include "memory_pool.h"
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <unordered_set>
#include <functional>
#include <memory>

class ThreadPool {
public:
    ThreadPool(size_t num_threads, std::atomic<size_t>* processed_files_ptr, 
               std::atomic<uint64_t>* copied_bytes_ptr, ConflictResolution conflict_resolution, 
               bool verbose, std::unordered_set<fs::path>* created_dirs_ptr);
    ~ThreadPool();

    void enqueue(FileTask task);

private:
    void process_task(const FileTask& task);
    void copy_file(const fs::path& src, const fs::path& dst, std::unordered_set<fs::path>& created_dirs);
    void copy_file_async(int src_fd, int dst_fd, uint64_t file_size);
    void copy_file_simd(int src_fd, int dst_fd, uint64_t file_size);
    void move_file(const fs::path& src, const fs::path& dst, std::unordered_set<fs::path>& created_dirs);

    static size_t calculate_chunk_size(uint64_t file_size);

    std::vector<std::thread> workers;
    std::queue<FileTask> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
    std::atomic<size_t>* processed_files_ptr;
    std::atomic<uint64_t>* copied_bytes_ptr;
    ConflictResolution conflict_resolution;
    bool verbose;
    std::unordered_set<fs::path>* created_dirs_ptr;
    std::thread event_processor;
    AsyncIO async_io;
    std::unique_ptr<MemoryPool> memory_pool;
    static std::mutex cout_mutex;
};
