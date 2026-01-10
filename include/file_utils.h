#pragma once

#include <string>
#include <stdexcept>
#include <sys/mman.h>

class FileDescriptor {
    int fd;
public:
    FileDescriptor(const char* path, int flags, mode_t mode = 0);
    ~FileDescriptor();

    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;

    FileDescriptor(FileDescriptor&& other) noexcept;
    FileDescriptor& operator=(FileDescriptor&& other) noexcept;

    operator int() const;
    int get() const;
};

class MappedFile {
    void* addr;
    size_t length;
    int fd;

public:
    MappedFile(void* addr, size_t length, int fd);
    ~MappedFile();

    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    MappedFile(MappedFile&& other) noexcept;
    MappedFile& operator=(MappedFile&& other) noexcept;

    void* data() const;
    size_t size() const;
};
