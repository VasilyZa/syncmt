#include "../include/file_copier.h"
#include <iostream>
#include <iomanip>
#include <dirent.h>
#include <sys/stat.h>
#include <cerrno>
#include <cstring>

FileCopier::FileCopier(size_t num_threads, bool move_mode, bool verbose, ConflictResolution conflict_resolution)
    : pool(num_threads, &processed_files, &copied_bytes, conflict_resolution, verbose, &created_dirs), 
      move_mode(move_mode), verbose(verbose), 
      conflict_resolution(conflict_resolution),
      total_files(0), processed_files(0), total_bytes(0), copied_bytes(0),
      start_time(std::chrono::high_resolution_clock::now()), 
      last_progress_update(start_time) {}

void FileCopier::process(const std::vector<fs::path>& sources, const fs::path& dst) {
    start_time = std::chrono::high_resolution_clock::now();
    last_progress_update = start_time;

    for (const auto& src : sources) {
        if (fs::is_directory(src)) {
            process_directory(src, dst / src.filename());
        } else if (fs::is_regular_file(src)) {
            process_single_file(src, dst / src.filename());
        } else {
            if (use_chinese) {
                std::cerr << "警告: 跳过非常规文件: " << src << std::endl;
            } else {
                std::cerr << "Warning: Skipping non-regular file: " << src << std::endl;
            }
        }
    }

    auto end_time = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);

    std::cout << "\r" << std::string(100, ' ') << "\r";
    print_stats(duration);
}

void FileCopier::process(const fs::path& src, const fs::path& dst) {
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

void FileCopier::scan_directory_native(const fs::path& src_dir, const fs::path& dst_dir, 
                                     std::vector<FileTask>& tasks, size_t& scan_counter, bool pipeline) {
    DIR* dir = opendir(src_dir.c_str());
    if (!dir) {
        throw std::runtime_error(std::string("Failed to open directory: ") + src_dir.string() +
                               " (errno: " + std::to_string(errno) + 
                               " - " + strerror(errno) + ")");
    }

    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        fs::path entry_path = src_dir / entry->d_name;
        struct stat st;
        if (stat(entry_path.c_str(), &st) == -1) {
            closedir(dir);
            throw std::runtime_error(std::string("Failed to stat: ") + entry_path.string() +
                                   " (errno: " + std::to_string(errno) + 
                                   " - " + strerror(errno) + ")");
        }

        if (S_ISREG(st.st_mode)) {
            fs::path rel_path = fs::relative(entry_path, src_dir);
            fs::path dst_path = dst_dir / rel_path;

            total_files++;
            total_bytes += st.st_size;

            FileTask task{entry_path, dst_path, move_mode, conflict_resolution};
            
            if (pipeline) {
                pool.enqueue(task);
            } else {
                tasks.push_back(task);
            }

            scan_counter++;
            if (scan_counter % 1000 == 0) {
                update_scan_progress(total_files.load(), total_bytes.load());
                
                if (!pipeline && tasks.size() >= 1000) {
                    for (auto& t : tasks) {
                        pool.enqueue(std::move(t));
                    }
                    tasks.clear();
                }
            }
        } else if (S_ISDIR(st.st_mode)) {
            scan_directory_native(entry_path, dst_dir / entry->d_name, tasks, scan_counter, pipeline);
        }
    }

    closedir(dir);
}

void FileCopier::update_progress() {
    auto now = std::chrono::high_resolution_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_progress_update);

    if (elapsed.count() < PROGRESS_UPDATE_INTERVAL_MS) {
        return;
    }

    last_progress_update = now;

    size_t processed = processed_files.load();
    size_t total = total_files.load();
    uint64_t copied = copied_bytes.load();
    uint64_t total_b = total_bytes.load();

    double file_progress = total > 0 ? (processed * 100.0 / total) : 0.0;

    auto total_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time);
    double seconds = total_elapsed.count() / 1000.0;
    double speed = copied / (1024.0 * 1024.0) / seconds;

    double remaining_bytes = total_b - copied;
    double eta = speed > 0 ? remaining_bytes / (1024.0 * 1024.0) / speed : 0;

    int bar_width = 40;
    int filled = static_cast<int>(file_progress * bar_width / 100.0);

    std::lock_guard<std::mutex> lock(cout_mutex);
    std::cout << "\r[";
    for (int i = 0; i < bar_width; ++i) {
        if (i < filled) {
            std::cout << "█";
        } else if (i == filled) {
            std::cout << "▓";
        } else {
            std::cout << "░";
        }
    }
    std::cout << "] ";
    std::cout << std::fixed << std::setprecision(1) << file_progress << "% ";
    if (use_chinese) {
        std::cout << "(" << processed << "/" << total << " 个文件, ";
    } else {
        std::cout << "(" << processed << "/" << total << " files, ";
    }
    std::cout << std::setprecision(2) << (copied / (1024.0 * 1024.0)) << "/";
    std::cout << std::setprecision(2) << (total_b / (1024.0 * 1024.0)) << " MB, ";
    std::cout << std::setprecision(1) << speed << " MB/s, ";
    if (use_chinese) {
        std::cout << "预计剩余时间: ";
    } else {
        std::cout << "ETA: ";
    }
    
    if (eta < 60) {
        std::cout << std::setprecision(0) << eta << "s";
    } else if (eta < 3600) {
        std::cout << std::setprecision(0) << (eta / 60) << "m" 
                  << std::setprecision(0) << (static_cast<int>(eta) % 60) << "s";
    } else {
        int hours = static_cast<int>(eta) / 3600;
        int minutes = (static_cast<int>(eta) % 3600) / 60;
        std::cout << hours << "h" << minutes << "m";
    }
    
    std::cout << ")";
    std::cout.flush();
}

void FileCopier::update_scan_progress(size_t scanned_files, uint64_t scanned_bytes) {
    if (use_chinese) {
        std::cout << "\r扫描中: [" << scanned_files << " 个文件, " 
                  << std::fixed << std::setprecision(2) 
                  << (scanned_bytes / (1024.0 * 1024.0)) << " MB]... ";
    } else {
        std::cout << "\rScanning: [" << scanned_files << " files, " 
                  << std::fixed << std::setprecision(2) 
                  << (scanned_bytes / (1024.0 * 1024.0)) << " MB]... ";
    }
    std::cout.flush();
}

void FileCopier::process_directory(const fs::path& src_dir, const fs::path& dst_dir) {
    if (use_chinese) {
        std::cout << "扫描目录: " << src_dir << "..." << std::endl;
    } else {
        std::cout << "Scanning directory: " << src_dir << "..." << std::endl;
    }
    std::cout.flush();

    std::atomic<bool> scan_done{false};
    std::thread scan_thread([this, &src_dir, &dst_dir, &scan_done]() {
        std::vector<FileTask> tasks;
        size_t scan_counter = 0;
        scan_directory_native(src_dir, dst_dir, tasks, scan_counter, false);
        
        for (auto& task : tasks) {
            pool.enqueue(std::move(task));
        }
        
        scan_done.store(true);
    });

    auto scan_start_time = std::chrono::high_resolution_clock::now();

    while (!scan_done.load() || processed_files.load() < total_files.load()) {
        if (!scan_done.load()) {
            update_scan_progress(total_files.load(), total_bytes.load());
        } else {
            update_progress();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(PROGRESS_UPDATE_INTERVAL_MS));
    }

    scan_thread.join();

    auto scan_end_time = std::chrono::high_resolution_clock::now();
    auto scan_duration = std::chrono::duration_cast<std::chrono::milliseconds>(scan_end_time - scan_start_time);
    double scan_seconds = scan_duration.count() / 1000.0;

    std::cout << "\r" << std::string(100, ' ') << "\r";
    if (use_chinese) {
        std::cout << "扫描完成: [" << total_files.load() << " 个文件, " 
                  << std::fixed << std::setprecision(2) 
                  << (total_bytes.load() / (1024.0 * 1024.0)) << " MB] ("
                  << std::setprecision(2) << scan_seconds << "秒)" << std::endl;
    } else {
        std::cout << "Scan complete: [" << total_files.load() << " files, " 
                  << std::fixed << std::setprecision(2) 
                  << (total_bytes.load() / (1024.0 * 1024.0)) << " MB] ("
                  << std::setprecision(2) << scan_seconds << "s)" << std::endl;
    }
}

void FileCopier::process_single_file(const fs::path& src, const fs::path& dst) {
    total_files = 1;
    total_bytes = fs::file_size(src);

    pool.enqueue({src, dst, move_mode, conflict_resolution});

    while (processed_files.load() < total_files.load()) {
        update_progress();
        std::this_thread::sleep_for(std::chrono::milliseconds(PROGRESS_UPDATE_INTERVAL_MS));
    }
}

void FileCopier::print_stats(const std::chrono::milliseconds& duration) {
    double seconds = duration.count() / 1000.0;
    double speed = copied_bytes.load() / (1024.0 * 1024.0) / seconds;

    if (use_chinese) {
        std::cout << "\n=== 统计信息 ===" << std::endl;
        std::cout << "总文件数: " << total_files.load() << std::endl;
        std::cout << "已处理文件: " << processed_files.load() << std::endl;
        std::cout << "总字节数: " << total_bytes.load() << std::endl;
        std::cout << "已复制字节: " << copied_bytes.load() << std::endl;
        std::cout << "耗时: " << seconds << " 秒" << std::endl;
        std::cout << "速度: " << speed << " MB/s" << std::endl;
    } else {
        std::cout << "\n=== Statistics ===" << std::endl;
        std::cout << "Total files: " << total_files.load() << std::endl;
        std::cout << "Processed files: " << processed_files.load() << std::endl;
        std::cout << "Total bytes: " << total_bytes.load() << std::endl;
        std::cout << "Copied bytes: " << copied_bytes.load() << std::endl;
        std::cout << "Time elapsed: " << seconds << " seconds" << std::endl;
        std::cout << "Speed: " << speed << " MB/s" << std::endl;
    }
}
