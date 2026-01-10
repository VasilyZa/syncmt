#pragma once

#include <memory>
#include <vector>
#include <cstddef>
#include <cstring>
#include <immintrin.h>
#include <atomic>
#include <mutex>
#include <cpuid.h>

class MemoryPool {
public:
    explicit MemoryPool(size_t block_size = 64 * 1024, size_t initial_blocks = 128);
    ~MemoryPool();

    MemoryPool(const MemoryPool&) = delete;
    MemoryPool& operator=(const MemoryPool&) = delete;

    void* allocate();
    void deallocate(void* ptr);

    size_t get_block_size() const { return block_size_; }
    size_t get_allocated_count() const { return allocated_count_.load(); }

private:
    struct Block {
        Block* next;
        alignas(64) char data[1];
    };

    void expand_pool(size_t blocks_to_add = 32);

    size_t block_size_;
    std::vector<std::unique_ptr<char[]>> chunks_;
    Block* free_list_;
    std::atomic<size_t> allocated_count_;
    std::mutex mutex_;
};

inline void* operator new(size_t, MemoryPool& pool) {
    return pool.allocate();
}

inline void operator delete(void* ptr, MemoryPool& pool) {
    pool.deallocate(ptr);
}

namespace simd_utils {

inline void memcpy_sse2(void* dst, const void* src, size_t size) {
    auto* d = static_cast<char*>(dst);
    auto* s = static_cast<const char*>(src);

    size_t remaining = size;

    while (remaining >= 16) {
        __m128i data = _mm_loadu_si128(reinterpret_cast<const __m128i*>(s));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(d), data);
        d += 16;
        s += 16;
        remaining -= 16;
    }

    if (remaining >= 8) {
        *reinterpret_cast<uint64_t*>(d) = *reinterpret_cast<const uint64_t*>(s);
        d += 8;
        s += 8;
        remaining -= 8;
    }

    if (remaining >= 4) {
        *reinterpret_cast<uint32_t*>(d) = *reinterpret_cast<const uint32_t*>(s);
        d += 4;
        s += 4;
        remaining -= 4;
    }

    if (remaining >= 2) {
        *reinterpret_cast<uint16_t*>(d) = *reinterpret_cast<const uint16_t*>(s);
        d += 2;
        s += 2;
        remaining -= 2;
    }

    if (remaining >= 1) {
        *d = *s;
    }
}

inline void memcpy_avx2(void* dst, const void* src, size_t size) {
    auto* d = static_cast<char*>(dst);
    auto* s = static_cast<const char*>(src);

    size_t remaining = size;

    while (remaining >= 32) {
        __m256i data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        _mm256_storeu_si256(reinterpret_cast<__m256i*>(d), data);
        d += 32;
        s += 32;
        remaining -= 32;
    }

    memcpy_sse2(d, s, remaining);
}

inline void memcpy_nt(void* dst, const void* src, size_t size) {
    auto* d = static_cast<char*>(dst);
    auto* s = static_cast<const char*>(src);

    size_t remaining = size;

    while (remaining >= 32) {
        __m256i data = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s));
        _mm256_stream_si256(reinterpret_cast<__m256i*>(d), data);
        d += 32;
        s += 32;
        remaining -= 32;
    }

    _mm_sfence();

    memcpy_sse2(d, s, remaining);
}

inline bool has_avx2() {
    unsigned int eax, ebx, ecx, edx;
    
    __get_cpuid(0, &eax, &ebx, &ecx, &edx);
    if (eax < 7) return false;
    
    __get_cpuid_count(7, 0, &eax, &ebx, &ecx, &edx);
    return (ebx & (1 << 5)) != 0;
}

inline void optimized_memcpy(void* dst, const void* src, size_t size) {
    static const bool use_avx2 = has_avx2();

    if (size < 256) {
        std::memcpy(dst, src, size);
    } else if (use_avx2) {
        memcpy_avx2(dst, src, size);
    } else {
        memcpy_sse2(dst, src, size);
    }
}

}
