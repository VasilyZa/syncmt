#include "../include/memory_pool.h"
#include <stdexcept>
#include <algorithm>

MemoryPool::MemoryPool(size_t block_size, size_t initial_blocks)
    : block_size_(block_size), free_list_(nullptr), allocated_count_(0) {
    expand_pool(initial_blocks);
}

MemoryPool::~MemoryPool() {
    chunks_.clear();
}

void* MemoryPool::allocate() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!free_list_) {
        expand_pool();
    }

    Block* block = free_list_;
    free_list_ = free_list_->next;
    allocated_count_++;

    return block->data;
}

void MemoryPool::deallocate(void* ptr) {
    if (!ptr) return;

    std::lock_guard<std::mutex> lock(mutex_);

    Block* block = reinterpret_cast<Block*>(
        static_cast<char*>(ptr) - offsetof(Block, data)
    );

    block->next = free_list_;
    free_list_ = block;
    allocated_count_--;
}

void MemoryPool::expand_pool(size_t blocks_to_add) {
    size_t chunk_size = blocks_to_add * (block_size_ + sizeof(Block*));

    auto chunk = std::make_unique<char[]>(chunk_size);
    char* ptr = chunk.get();

    for (size_t i = 0; i < blocks_to_add; ++i) {
        Block* block = reinterpret_cast<Block*>(ptr);
        block->next = free_list_;
        free_list_ = block;
        ptr += block_size_ + sizeof(Block*);
    }

    chunks_.push_back(std::move(chunk));
}
