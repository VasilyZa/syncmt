#pragma once

#include <string>
#include <filesystem>

namespace fs = std::filesystem;

constexpr size_t DEFAULT_THREADS = 4;
constexpr size_t MIN_CHUNK_SIZE = 64 * 1024;
constexpr size_t MAX_CHUNK_SIZE = 16 * 1024 * 1024;
constexpr size_t SMALL_FILE_THRESHOLD = 1024 * 1024;
constexpr size_t LARGE_FILE_THRESHOLD = 100 * 1024 * 1024;
constexpr size_t PROGRESS_UPDATE_INTERVAL_MS = 100;

extern bool use_chinese;

enum class ConflictResolution {
    OVERWRITE,
    SKIP,
    ERROR
};

struct FileTask {
    fs::path src;
    fs::path dst;
    bool is_move;
    ConflictResolution conflict_resolution;
};
