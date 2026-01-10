#include "include/config.h"
#include "include/file_copier.h"
#include <iostream>
#include <vector>
#include <string>

bool use_chinese = false;

void print_usage(const char* program_name) {
    if (use_chinese) {
        std::cout << "用法: " << program_name << " [选项] <源路径>... <目标路径>" << std::endl;
        std::cout << "\n选项:" << std::endl;
        std::cout << "  -t, --threads <数量>   线程数 (默认: " << DEFAULT_THREADS << ")" << std::endl;
        std::cout << "  -m, --move            移动文件而非复制" << std::endl;
        std::cout << "  -v, --verbose         启用详细输出" << std::endl;
        std::cout << "  -o, --overwrite       覆盖已存在的文件" << std::endl;
        std::cout << "  -s, --skip            跳过已存在的文件" << std::endl;
        std::cout << "  --zh                  使用中文界面" << std::endl;
        std::cout << "  -V, --version         显示版本信息" << std::endl;
        std::cout << "  -h, --help            显示此帮助信息" << std::endl;
    } else {
        std::cout << "Usage: " << program_name << " [OPTIONS] <source>... <destination>" << std::endl;
        std::cout << "\nOptions:" << std::endl;
        std::cout << "  -t, --threads <num>   Number of threads (default: " << DEFAULT_THREADS << ")" << std::endl;
        std::cout << "  -m, --move            Move files instead of copying" << std::endl;
        std::cout << "  -v, --verbose         Enable verbose output" << std::endl;
        std::cout << "  -o, --overwrite       Overwrite existing files" << std::endl;
        std::cout << "  -s, --skip            Skip existing files" << std::endl;
        std::cout << "  --zh                  Use Chinese interface" << std::endl;
        std::cout << "  -V, --version         Print version information" << std::endl;
        std::cout << "  -h, --help            Show this help message" << std::endl;
    }
}

void print_version() {
    std::cout << "syncmt version " << VERSION << std::endl;
    std::cout << "Git commit: " << GIT_COMMIT << std::endl;
    std::cout << "Build date: " << BUILD_DATE << std::endl;
}

int main(int argc, char* argv[]) {
    size_t num_threads = DEFAULT_THREADS;
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
        } else if (arg == "--zh") {
            use_chinese = true;
        } else if (arg == "-V" || arg == "--version") {
            print_version();
            return 0;
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
            if (use_chinese) {
                std::cerr << "错误: 源路径不存在: " << src << std::endl;
            } else {
                std::cerr << "Error: Source path does not exist: " << src << std::endl;
            }
            return 1;
        }
    }

    try {
        if (use_chinese) {
            std::cout << "开始" << (move_mode ? "移动" : "复制") << "操作..." << std::endl;
        } else {
            std::cout << "Starting " << (move_mode ? "move" : "copy") << " operation..." << std::endl;
        }
        if (use_chinese) {
            std::cout << "源路径: ";
        } else {
            std::cout << "Source(s): ";
        }
        for (size_t i = 0; i < src_paths.size(); ++i) {
            if (i > 0) std::cout << ", ";
            std::cout << src_paths[i];
        }
        std::cout << std::endl;
        if (use_chinese) {
            std::cout << "目标路径: " << dst_path << std::endl;
            std::cout << "线程数: " << num_threads << std::endl;
            std::cout << "冲突处理方式: ";
        } else {
            std::cout << "Destination: " << dst_path << std::endl;
            std::cout << "Threads: " << num_threads << std::endl;
            std::cout << "Conflict resolution: ";
        }
        switch (conflict_resolution) {
            case ConflictResolution::OVERWRITE:
                if (use_chinese) {
                    std::cout << "覆盖" << std::endl;
                } else {
                    std::cout << "overwrite" << std::endl;
                }
                break;
            case ConflictResolution::SKIP:
                if (use_chinese) {
                    std::cout << "跳过" << std::endl;
                } else {
                    std::cout << "skip" << std::endl;
                }
                break;
            case ConflictResolution::ERROR:
                if (use_chinese) {
                    std::cout << "冲突时报错" << std::endl;
                } else {
                    std::cout << "error on conflict" << std::endl;
                }
                break;
        }

        FileCopier copier(num_threads, move_mode, verbose, conflict_resolution);

        std::vector<fs::path> sources;
        for (const auto& src : src_paths) {
            sources.push_back(src);
        }

        copier.process(sources, dst_path);

        if (use_chinese) {
            std::cout << "\n操作成功完成!" << std::endl;
        } else {
            std::cout << "\nOperation completed successfully!" << std::endl;
        }
    } catch (const std::exception& e) {
        if (use_chinese) {
            std::cerr << "错误: " << e.what() << std::endl;
        } else {
            std::cerr << "Error: " << e.what() << std::endl;
        }
        return 1;
    }

    return 0;
}
