#include <iostream>
#include <fstream>
#include <filesystem>
#include <vector>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cstring>
#include <algorithm>
#include <iomanip>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/sendfile.h>

namespace fs = std::filesystem;

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

class ThreadPool {
public:
    ThreadPool(size_t num_threads, std::atomic<size_t>* processed_files_ptr, std::atomic<uint64_t>* copied_bytes_ptr, ConflictResolution conflict_resolution, bool verbose)
        : stop(false), processed_files_ptr(processed_files_ptr), copied_bytes_ptr(copied_bytes_ptr), conflict_resolution(conflict_resolution), verbose(verbose) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers.emplace_back([this] {
                for (;;) {
                    FileTask task;
                    {
                        std::unique_lock<std::mutex> lock(this->queue_mutex);
                        this->condition.wait(lock, [this] {
                            return this->stop || !this->tasks.empty();
                        });
                        if (this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }
                    process_task(task);
                }
            });
        }
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            stop = true;
        }
        condition.notify_all();
        for (std::thread &worker : workers)
            worker.join();
    }

    void enqueue(FileTask task) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex);
            tasks.push(std::move(task));
        }
        condition.notify_one();
    }

private:
    std::vector<std::thread> workers;
    std::queue<FileTask> tasks;
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
    std::atomic<size_t>* processed_files_ptr;
    std::atomic<uint64_t>* copied_bytes_ptr;
    ConflictResolution conflict_resolution;
    bool verbose;

    void process_task(const FileTask& task) {
        try {
            if (fs::exists(task.dst)) {
                switch (task.conflict_resolution) {
                    case ConflictResolution::SKIP:
                        if (verbose) {
                            std::lock_guard<std::mutex> lock(cout_mutex);
                            std::cout << "Skipping existing file: " << task.dst << std::endl;
                        }
                        (*processed_files_ptr)++;
                        return;
                    case ConflictResolution::OVERWRITE:
                        if (verbose) {
                            std::lock_guard<std::mutex> lock(cout_mutex);
                            std::cout << "Overwriting existing file: " << task.dst << std::endl;
                        }
                        break;
                    case ConflictResolution::ERROR:
                        throw std::runtime_error("Destination file already exists");
                }
            }

            uint64_t file_size = fs::file_size(task.src);
            if (task.is_move) {
                move_file(task.src, task.dst);
            } else {
                copy_file(task.src, task.dst);
            }
            (*processed_files_ptr)++;
            (*copied_bytes_ptr) += file_size;
        } catch (const std::exception& e) {
            std::lock_guard<std::mutex> lock(cout_mutex);
            std::cerr << "Error processing " << task.src << ": " << e.what() << std::endl;
        }
    }

    static void copy_file(const fs::path& src, const fs::path& dst) {
        int src_fd = open(src.c_str(), O_RDONLY);
        if (src_fd == -1) {
            throw std::runtime_error("Failed to open source file");
        }

        struct stat src_stat;
        if (fstat(src_fd, &src_stat) == -1) {
            close(src_fd);
            throw std::runtime_error("Failed to stat source file");
        }

        fs::path dst_parent = dst.parent_path();
        if (!dst_parent.empty() && !fs::exists(dst_parent)) {
            fs::create_directories(dst_parent);
        }

        int dst_fd = open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, src_stat.st_mode);
        if (dst_fd == -1) {
            close(src_fd);
            throw std::runtime_error("Failed to create destination file");
        }

        off_t offset = 0;
        size_t remaining = src_stat.st_size;
        const size_t chunk_size = 1024 * 1024;

        while (remaining > 0) {
            size_t to_copy = std::min(remaining, chunk_size);
            ssize_t sent = sendfile(dst_fd, src_fd, &offset, to_copy);
            if (sent == -1) {
                close(src_fd);
                close(dst_fd);
                throw std::runtime_error("Failed to copy file data");
            }
            remaining -= sent;
        }

        close(src_fd);
        close(dst_fd);

        struct timespec times[2];
        times[0] = src_stat.st_atim;
        times[1] = src_stat.st_mtim;
        utimensat(AT_FDCWD, dst.c_str(), times, 0);
    }

    static void move_file(const fs::path& src, const fs::path& dst) {
        fs::path dst_parent = dst.parent_path();
        if (!dst_parent.empty() && !fs::exists(dst_parent)) {
            fs::create_directories(dst_parent);
        }

        if (rename(src.c_str(), dst.c_str()) == 0) {
            return;
        }

        if (errno == EXDEV) {
            copy_file(src, dst);
            fs::remove(src);
        } else {
            throw std::runtime_error("Failed to move file");
        }
    }

    static std::mutex cout_mutex;
};

std::mutex ThreadPool::cout_mutex;

class FileCopier {
public:
    FileCopier(size_t num_threads, bool move_mode, bool verbose, ConflictResolution conflict_resolution)
        : pool(num_threads, &processed_files, &copied_bytes, conflict_resolution, verbose), move_mode(move_mode), verbose(verbose), conflict_resolution(conflict_resolution),
          total_files(0), processed_files(0), total_bytes(0), copied_bytes(0),
          start_time(std::chrono::high_resolution_clock::now()), last_progress_update(start_time) {}

    void process(const std::vector<fs::path>& sources, const fs::path& dst) {
        start_time = std::chrono::high_resolution_clock::now();
        last_progress_update = start_time;

        for (const auto& src : sources) {
            if (fs::is_directory(src)) {
                process_directory(src, dst / src.filename());
            } else if (fs::is_regular_file(src)) {
                process_single_file(src, dst / src.filename());
            } else {
                std::cerr << "Warning: Skipping non-regular file: " << src << std::endl;
            }
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "\r" << std::string(100, ' ') << "\r";
        print_stats(duration);
    }

    void process(const fs::path& src, const fs::path& dst) {
        start_time = std::chrono::high_resolution_clock::now();
        last_progress_update = start_time;

        if (fs::is_directory(src)) {
            process_directory(src, dst);
        } else {
            process_single_file(src, dst);
        }

        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

        std::cout << "\r" << std::string(100, ' ') << "\r";
        print_stats(duration);
    }

private:
    ThreadPool pool;
    bool move_mode;
    bool verbose;
    ConflictResolution conflict_resolution;
    std::atomic<size_t> total_files;
    std::atomic<size_t> processed_files;
    std::atomic<uint64_t> total_bytes;
    std::atomic<uint64_t> copied_bytes;
    std::chrono::high_resolution_clock::time_point start_time;
    std::chrono::high_resolution_clock::time_point last_progress_update;

    void update_progress() {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_progress_update);

        if (elapsed.count() < 100) {
            return;
        }

        last_progress_update = now;

        size_t processed = processed_files.load();
        size_t total = total_files.load();
        uint64_t copied = copied_bytes.load();
        uint64_t total_b = total_bytes.load();

        double file_progress = total > 0 ? (processed * 100.0 / total) : 0.0;
        double byte_progress = total_b > 0 ? (copied * 100.0 / total_b) : 0.0;

        auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
        double seconds = total_elapsed.count() / 1000.0;
        double speed = copied / (1024.0 * 1024.0) / seconds;

        int bar_width = 40;
        int filled = static_cast<int>(file_progress * bar_width / 100.0);

        std::cout << "\r[";
        for (int i = 0; i < bar_width; ++i) {
            if (i < filled) {
                std::cout << "=";
            } else if (i == filled) {
                std::cout << ">";
            } else {
                std::cout << " ";
            }
        }
        std::cout << "] ";
        std::cout << std::fixed << std::setprecision(1) << file_progress << "% ";
        std::cout << "(" << processed << "/" << total << " files, ";
        std::cout << std::setprecision(2) << (copied / (1024.0 * 1024.0)) << "/";
        std::cout << std::setprecision(2) << (total_b / (1024.0 * 1024.0)) << " MB, ";
        std::cout << std::setprecision(1) << speed << " MB/s)";
        std::cout.flush();
    }

    void process_directory(const fs::path& src_dir, const fs::path& dst_dir) {
        std::vector<FileTask> tasks;

        for (const auto& entry : fs::recursive_directory_iterator(src_dir)) {
            if (entry.is_regular_file()) {
                fs::path rel_path = fs::relative(entry.path(), src_dir);
                fs::path dst_path = dst_dir / rel_path;

                total_files++;
                total_bytes += entry.file_size();

                tasks.push_back({entry.path(), dst_path, move_mode, conflict_resolution});
            }
        }

        for (auto& task : tasks) {
            pool.enqueue(std::move(task));
        }

        while (processed_files.load() < total_files.load()) {
            update_progress();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void process_single_file(const fs::path& src, const fs::path& dst) {
        total_files = 1;
        total_bytes = fs::file_size(src);

        pool.enqueue({src, dst, move_mode, conflict_resolution});

        while (processed_files.load() < total_files.load()) {
            update_progress();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void print_stats(const std::chrono::milliseconds& duration) {
        double seconds = duration.count() / 1000.0;
        double speed = copied_bytes.load() / (1024.0 * 1024.0) / seconds;

        std::cout << "\n=== Statistics ===" << std::endl;
        std::cout << "Total files: " << total_files.load() << std::endl;
        std::cout << "Processed files: " << processed_files.load() << std::endl;
        std::cout << "Total bytes: " << total_bytes.load() << std::endl;
        std::cout << "Copied bytes: " << copied_bytes.load() << std::endl;
        std::cout << "Time elapsed: " << seconds << " seconds" << std::endl;
        std::cout << "Speed: " << speed << " MB/s" << std::endl;
    }
};

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [OPTIONS] <source>... <destination>" << std::endl;
    std::cout << "\nOptions:" << std::endl;
    std::cout << "  -t, --threads <num>   Number of threads (default: 4)" << std::endl;
    std::cout << "  -m, --move            Move files instead of copying" << std::endl;
    std::cout << "  -v, --verbose         Enable verbose output" << std::endl;
    std::cout << "  -o, --overwrite       Overwrite existing files" << std::endl;
    std::cout << "  -s, --skip            Skip existing files" << std::endl;
    std::cout << "  -h, --help            Show this help message" << std::endl;
}

int main(int argc, char* argv[]) {
    size_t num_threads = 4;
    bool move_mode = false;
    bool verbose = false;
    ConflictResolution conflict_resolution = ConflictResolution::ERROR;
    std::vector<std::string> src_paths;
    std::string dst_path;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "-t" || arg == "--threads") {
            if (i + 1 < argc) {
                num_threads = std::stoul(argv[++i]);
            }
        } else if (arg == "-m" || arg == "--move") {
            move_mode = true;
        } else if (arg == "-v" || arg == "--verbose") {
            verbose = true;
        } else if (arg == "-o" || arg == "--overwrite") {
            conflict_resolution = ConflictResolution::OVERWRITE;
        } else if (arg == "-s" || arg == "--skip") {
            conflict_resolution = ConflictResolution::SKIP;
        } else if (arg == "-h" || arg == "--help") {
            print_usage(argv[0]);
            return 0;
        } else {
            src_paths.push_back(arg);
        }
    }

    if (src_paths.size() < 2) {
        print_usage(argv[0]);
        return 1;
    }

    dst_path = src_paths.back();
    src_paths.pop_back();

    for (const auto& src : src_paths) {
        if (!fs::exists(src)) {
            std::cerr << "Error: Source path does not exist: " << src << std::endl;
            return 1;
        }
    }

    try {
        std::cout << "Starting " << (move_mode ? "move" : "copy") << " operation..." << std::endl;
        std::cout << "Source(s): ";
        for (size_t i = 0; i < src_paths.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << src_paths[i];
        }
        std::cout << std::endl;
        std::cout << "Destination: " << dst_path << std::endl;
        std::cout << "Threads: " << num_threads << std::endl;
        std::cout << "Conflict resolution: ";
        switch (conflict_resolution) {
            case ConflictResolution::OVERWRITE:
                std::cout << "overwrite" << std::endl;
                break;
            case ConflictResolution::SKIP:
                std::cout << "skip" << std::endl;
                break;
            case ConflictResolution::ERROR:
                std::cout << "error on conflict" << std::endl;
                break;
        }

        FileCopier copier(num_threads, move_mode, verbose, conflict_resolution);

        std::vector<fs::path> sources;
        for (const auto& src : src_paths) {
            sources.push_back(src);
        }

        copier.process(sources, dst_path);

        std::cout << "\nOperation completed successfully!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}
