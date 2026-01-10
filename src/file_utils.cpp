#include "../include/file_utils.h"
#include <fcntl.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>

FileDescriptor::FileDescriptor(const char* path, int flags, mode_t mode) : fd(open(path, flags, mode)) {
    if (fd == -1) {
        throw std::runtime_error(std::string("Failed to open file: ") + path + 
                               " (errno: " + std::to_string(errno) + 
                               " - " + strerror(errno) + ")");
    }
}

FileDescriptor::~FileDescriptor() {
    if (fd != -1) {
        close(fd);
    }
}

FileDescriptor::FileDescriptor(FileDescriptor&& other) noexcept : fd(other.fd) {
    other.fd = -1;
}

FileDescriptor& FileDescriptor::operator=(FileDescriptor&& other) noexcept {
    if (this != &other) {
        if (fd != -1) {
            close(fd);
        }
        fd = other.fd;
        other.fd = -1;
    }
    return *this;
}

FileDescriptor::operator int() const { 
    return fd; 
}

int FileDescriptor::get() const { 
    return fd; 
}

MappedFile::MappedFile(void* addr, size_t length, int fd) 
    : addr(addr), length(length), fd(fd) {}

MappedFile::~MappedFile() {
    if (addr != MAP_FAILED) {
        msync(addr, length, MS_SYNC);
        munmap(addr, length);
    }
    if (fd != -1) {
        close(fd);
    }
}

MappedFile::MappedFile(MappedFile&& other) noexcept 
    : addr(other.addr), length(other.length), fd(other.fd) {
    other.addr = MAP_FAILED;
    other.length = 0;
    other.fd = -1;
}

MappedFile& MappedFile::operator=(MappedFile&& other) noexcept {
    if (this != &other) {
        if (addr != MAP_FAILED) {
            msync(addr, length, MS_SYNC);
            munmap(addr, length);
        }
        if (fd != -1) {
            close(fd);
        }
        addr = other.addr;
        length = other.length;
        fd = other.fd;
        other.addr = MAP_FAILED;
        other.length = 0;
        other.fd = -1;
    }
    return *this;
}

void* MappedFile::data() const { 
    return addr; 
}

size_t MappedFile::size() const { 
    return length; 
}
