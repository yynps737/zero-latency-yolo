#pragma once
#include <vector>
#include <list>
#include <memory>
#include <mutex>
#include <atomic>
#include <cassert>
#include <algorithm>
#include <new>
#include <type_traits>
#include <functional>
#include "logger.h"

namespace zero_latency {

inline void* alignedAlloc(size_t size, size_t alignment) {
    void* ptr = nullptr;
#ifdef _WIN32
    ptr = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) ptr = nullptr;
#endif
    return ptr;
}

inline void alignedFree(void* ptr) {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

struct MemoryBlock {
    void* data;
    size_t size;
    bool in_use;
    
    MemoryBlock(void* d, size_t s) : data(d), size(s), in_use(false) {}
    
    ~MemoryBlock() {
        if (data) { alignedFree(data); data = nullptr; }
    }
    
    MemoryBlock(const MemoryBlock&) = delete;
    MemoryBlock& operator=(const MemoryBlock&) = delete;
    
    MemoryBlock(MemoryBlock&& other) noexcept : data(other.data), size(other.size), in_use(other.in_use) {
        other.data = nullptr;
        other.size = 0;
        other.in_use = false;
    }
    
    MemoryBlock& operator=(MemoryBlock&& other) noexcept {
        if (this != &other) {
            if (data) alignedFree(data);
            data = other.data;
            size = other.size;
            in_use = other.in_use;
            other.data = nullptr;
            other.size = 0;
            other.in_use = false;
        }
        return *this;
    }
};

class MemoryPool {
public:
    explicit MemoryPool(size_t blockSize, size_t initialBlocks = 8, size_t alignment = 64)
        : block_size_(blockSize), alignment_(alignment), allocated_blocks_(0) {
        assert(alignment_ >= 1 && (alignment_ & (alignment_ - 1)) == 0);
        growPool(initialBlocks);
    }
    
    ~MemoryPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (allocated_blocks_ != free_blocks_.size())
            LOG_WARN("Memory pool destroyed with " + 
                    std::to_string(allocated_blocks_ - free_blocks_.size()) + 
                    " blocks still in use");
        free_blocks_.clear();
        for (auto& block : reserved_memory_) {
            alignedFree(block.data);
            block.data = nullptr;
        }
        reserved_memory_.clear();
    }
    
    void* allocate() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (free_blocks_.empty()) growPool(allocated_blocks_);
        MemoryBlock* block = free_blocks_.back();
        free_blocks_.pop_back();
        block->in_use = true;
        return block->data;
    }
    
    void deallocate(void* ptr) {
        if (!ptr) return;
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& block : reserved_memory_) {
            if (block.data == ptr) {
                if (!block.in_use) {
                    LOG_ERROR("Double-free detected in memory pool");
                    return;
                }
                block.in_use = false;
                free_blocks_.push_back(&block);
                return;
            }
        }
        LOG_ERROR("Attempted to free memory not owned by this pool");
    }
    
    size_t getBlockSize() const { return block_size_; }
    size_t getAlignment() const { return alignment_; }
    size_t getAllocatedBlocks() const { return allocated_blocks_; }
    
    size_t getAvailableBlocks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return free_blocks_.size();
    }
    
    size_t getUsedBlocks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return allocated_blocks_ - free_blocks_.size();
    }
    
    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        free_blocks_.clear();
        for (auto& block : reserved_memory_) {
            block.in_use = false;
            free_blocks_.push_back(&block);
        }
        LOG_INFO("Memory pool reset, all blocks marked as free");
    }

private:
    void growPool(size_t additional_blocks) {
        if (additional_blocks == 0) additional_blocks = 1;
        size_t old_size = reserved_memory_.size();
        reserved_memory_.resize(old_size + additional_blocks);
        for (size_t i = old_size; i < reserved_memory_.size(); ++i) {
            void* data = alignedAlloc(block_size_, alignment_);
            if (!data) {
                LOG_ERROR("Failed to allocate memory for pool");
                throw std::bad_alloc();
            }
            reserved_memory_[i] = MemoryBlock(data, block_size_);
            free_blocks_.push_back(&reserved_memory_[i]);
            allocated_blocks_++;
        }
        LOG_DEBUG("Memory pool grown by " + std::to_string(additional_blocks) + 
                 " blocks, now has " + std::to_string(allocated_blocks_) + " total blocks");
    }

    size_t block_size_, alignment_;
    std::atomic<size_t> allocated_blocks_;
    mutable std::mutex mutex_;
    std::vector<MemoryBlock> reserved_memory_;
    std::vector<MemoryBlock*> free_blocks_;
};

template<typename T>
class PoolAllocator {
public:
    using value_type = T;
    using pointer = T*;
    using const_pointer = const T*;
    using reference = T&;
    using const_reference = const T&;
    using size_type = std::size_t;
    using difference_type = std::ptrdiff_t;
    
    template<typename U> struct rebind { using other = PoolAllocator<U>; };
    
    explicit PoolAllocator(size_t blockSize = sizeof(T), size_t initialBlocks = 8)
        : memory_pool_(std::make_shared<MemoryPool>(std::max(blockSize, sizeof(T)), initialBlocks, alignof(T))) {}
    
    PoolAllocator(const PoolAllocator& other) noexcept : memory_pool_(other.memory_pool_) {}
    
    template<typename U>
    PoolAllocator(const PoolAllocator<U>& other) noexcept : memory_pool_(other.memory_pool_) {}
    
    ~PoolAllocator() = default;
    
    T* allocate(size_t n) {
        if (n != 1) return static_cast<T*>(::operator new(n * sizeof(T)));
        return static_cast<T*>(memory_pool_->allocate());
    }
    
    void deallocate(T* p, size_t n) {
        if (n != 1) { ::operator delete(p); return; }
        memory_pool_->deallocate(p);
    }
    
    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) { new(p) U(std::forward<Args>(args)...); }
    
    template<typename U> void destroy(U* p) { p->~U(); }
    
    bool operator==(const PoolAllocator& other) const { return memory_pool_ == other.memory_pool_; }
    bool operator!=(const PoolAllocator& other) const { return memory_pool_ != other.memory_pool_; }
    
    std::shared_ptr<MemoryPool> getPool() const { return memory_pool_; }

private:
    std::shared_ptr<MemoryPool> memory_pool_;
    
    template<typename U> friend class PoolAllocator;
};

template<typename T>
class ReusableBuffer {
public:
    explicit ReusableBuffer(size_t initial_capacity = 0) {
        if (initial_capacity > 0) buffer_.reserve(initial_capacity);
    }
    
    void reset() { buffer_.clear(); }
    void reserve(size_t capacity) { buffer_.reserve(capacity); }
    size_t size() const { return buffer_.size(); }
    size_t capacity() const { return buffer_.capacity(); }
    bool empty() const { return buffer_.empty(); }
    
    void push_back(const T& value) { buffer_.push_back(value); }
    void push_back(T&& value) { buffer_.push_back(std::move(value)); }
    
    T& operator[](size_t index) { return buffer_[index]; }
    const T& operator[](size_t index) const { return buffer_[index]; }
    
    T* data() { return buffer_.data(); }
    const T* data() const { return buffer_.data(); }
    
    typename std::vector<T>::iterator begin() { return buffer_.begin(); }
    typename std::vector<T>::iterator end() { return buffer_.end(); }
    typename std::vector<T>::const_iterator begin() const { return buffer_.begin(); }
    typename std::vector<T>::const_iterator end() const { return buffer_.end(); }
    
    void resize(size_t new_size) { buffer_.resize(new_size); }
    void resize(size_t new_size, const T& value) { buffer_.resize(new_size, value); }
    
    std::vector<T>& getBuffer() { return buffer_; }
    const std::vector<T>& getBuffer() const { return buffer_; }

private:
    std::vector<T> buffer_;
};

template<typename T>
class ThreadLocalBufferPool {
public:
    explicit ThreadLocalBufferPool(size_t initialSize = 1024) : initial_size_(initialSize) {}
    
    ReusableBuffer<T>& getBuffer() {
        thread_local ReusableBuffer<T> buffer(initial_size_);
        return buffer;
    }
    
    template<typename Func>
    auto withBuffer(Func&& func) -> decltype(func(std::declval<ReusableBuffer<T>&>())) {
        auto& buffer = getBuffer();
        buffer.reset();
        return func(buffer);
    }

private:
    size_t initial_size_;
};

template<typename T>
ThreadLocalBufferPool<T>& getThreadLocalBufferPool() {
    static ThreadLocalBufferPool<T> pool;
    return pool;
}

}