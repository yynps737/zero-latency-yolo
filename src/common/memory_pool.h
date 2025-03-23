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

// 对齐内存分配
inline void* alignedAlloc(size_t size, size_t alignment) {
    void* ptr = nullptr;
#ifdef _WIN32
    ptr = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&ptr, alignment, size) != 0) {
        ptr = nullptr;
    }
#endif
    return ptr;
}

// 释放对齐内存
inline void alignedFree(void* ptr) {
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}

// 内存块结构
struct MemoryBlock {
    void* data;
    size_t size;
    bool in_use;
    
    MemoryBlock(void* d, size_t s) : data(d), size(s), in_use(false) {}
    
    ~MemoryBlock() {
        if (data) {
            alignedFree(data);
            data = nullptr;
        }
    }
    
    // 不允许复制
    MemoryBlock(const MemoryBlock&) = delete;
    MemoryBlock& operator=(const MemoryBlock&) = delete;
    
    // 允许移动
    MemoryBlock(MemoryBlock&& other) noexcept
        : data(other.data), size(other.size), in_use(other.in_use) {
        other.data = nullptr;
        other.size = 0;
        other.in_use = false;
    }
    
    MemoryBlock& operator=(MemoryBlock&& other) noexcept {
        if (this != &other) {
            if (data) {
                alignedFree(data);
            }
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

// 内存池类
class MemoryPool {
public:
    // 创建指定大小的内存池
    explicit MemoryPool(size_t blockSize, size_t initialBlocks = 8, size_t alignment = 64)
        : block_size_(blockSize), alignment_(alignment), allocated_blocks_(0) {
        
        // 检查对齐要求
        assert(alignment_ >= 1 && (alignment_ & (alignment_ - 1)) == 0); // 必须是2的幂
        
        // 初始分配块
        growPool(initialBlocks);
    }
    
    ~MemoryPool() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (allocated_blocks_ != free_blocks_.size()) {
            LOG_WARN("Memory pool destroyed with " + 
                    std::to_string(allocated_blocks_ - free_blocks_.size()) + 
                    " blocks still in use");
        }
        
        // 释放所有块
        free_blocks_.clear();
        
        // 清空所有储备内存
        for (auto& block : reserved_memory_) {
            alignedFree(block.data);
            block.data = nullptr;
        }
        reserved_memory_.clear();
    }
    
    // 分配内存块
    void* allocate() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 如果没有可用块，增长池
        if (free_blocks_.empty()) {
            growPool(allocated_blocks_);
        }
        
        // 获取空闲块
        MemoryBlock* block = free_blocks_.back();
        free_blocks_.pop_back();
        block->in_use = true;
        
        return block->data;
    }
    
    // 释放内存块
    void deallocate(void* ptr) {
        if (!ptr) return;
        
        std::lock_guard<std::mutex> lock(mutex_);
        
        // 查找对应的内存块
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
    
    // 获取块大小
    size_t getBlockSize() const {
        return block_size_;
    }
    
    // 获取对齐值
    size_t getAlignment() const {
        return alignment_;
    }
    
    // 获取已分配的块数
    size_t getAllocatedBlocks() const {
        return allocated_blocks_;
    }
    
    // 获取可用块数
    size_t getAvailableBlocks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return free_blocks_.size();
    }
    
    // 获取当前使用的块数
    size_t getUsedBlocks() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return allocated_blocks_ - free_blocks_.size();
    }
    
    // 重置内存池 (危险操作，仅用于确保所有内存都已释放的场景)
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
    // 增加内存池大小
    void growPool(size_t additional_blocks) {
        if (additional_blocks == 0) {
            additional_blocks = 1;
        }
        
        size_t old_size = reserved_memory_.size();
        reserved_memory_.resize(old_size + additional_blocks);
        
        // 分配新块
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

private:
    size_t block_size_;
    size_t alignment_;
    std::atomic<size_t> allocated_blocks_;
    
    mutable std::mutex mutex_;
    std::vector<MemoryBlock> reserved_memory_;
    std::vector<MemoryBlock*> free_blocks_;
};

// 带类型的内存池分配器
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
    
    template<typename U>
    struct rebind {
        using other = PoolAllocator<U>;
    };
    
    // 构造函数
    explicit PoolAllocator(size_t blockSize = sizeof(T), size_t initialBlocks = 8)
        : memory_pool_(std::make_shared<MemoryPool>(
              std::max(blockSize, sizeof(T)), 
              initialBlocks, 
              alignof(T))) {
    }
    
    // 复制构造函数
    PoolAllocator(const PoolAllocator& other) noexcept : memory_pool_(other.memory_pool_) {}
    
    // 转换构造函数
    template<typename U>
    PoolAllocator(const PoolAllocator<U>& other) noexcept : memory_pool_(other.memory_pool_) {}
    
    // 析构函数
    ~PoolAllocator() = default;
    
    // 分配内存
    T* allocate(size_t n) {
        if (n != 1) {
            // 如果请求多个对象，退回到标准分配器
            return static_cast<T*>(::operator new(n * sizeof(T)));
        }
        
        return static_cast<T*>(memory_pool_->allocate());
    }
    
    // 释放内存
    void deallocate(T* p, size_t n) {
        if (n != 1) {
            // 如果是多个对象，使用标准释放
            ::operator delete(p);
            return;
        }
        
        memory_pool_->deallocate(p);
    }
    
    // 构造对象
    template<typename U, typename... Args>
    void construct(U* p, Args&&... args) {
        new(p) U(std::forward<Args>(args)...);
    }
    
    // 销毁对象
    template<typename U>
    void destroy(U* p) {
        p->~U();
    }
    
    // 比较运算符
    bool operator==(const PoolAllocator& other) const {
        return memory_pool_ == other.memory_pool_;
    }
    
    bool operator!=(const PoolAllocator& other) const {
        return memory_pool_ != other.memory_pool_;
    }
    
    // 获取内存池
    std::shared_ptr<MemoryPool> getPool() const {
        return memory_pool_;
    }

private:
    std::shared_ptr<MemoryPool> memory_pool_;
    
    // 为转换构造函数访问私有成员
    template<typename U>
    friend class PoolAllocator;
};

// 重用缓冲区类，用于减少内存分配
template<typename T>
class ReusableBuffer {
public:
    explicit ReusableBuffer(size_t initial_capacity = 0) {
        if (initial_capacity > 0) {
            buffer_.reserve(initial_capacity);
        }
    }
    
    // 重置缓冲区以重用
    void reset() {
        buffer_.clear();
    }
    
    // 预分配容量
    void reserve(size_t capacity) {
        buffer_.reserve(capacity);
    }
    
    // 获取当前大小
    size_t size() const {
        return buffer_.size();
    }
    
    // 获取当前容量
    size_t capacity() const {
        return buffer_.capacity();
    }
    
    // 判断是否为空
    bool empty() const {
        return buffer_.empty();
    }
    
    // 添加元素
    void push_back(const T& value) {
        buffer_.push_back(value);
    }
    
    void push_back(T&& value) {
        buffer_.push_back(std::move(value));
    }
    
    // 访问元素
    T& operator[](size_t index) {
        return buffer_[index];
    }
    
    const T& operator[](size_t index) const {
        return buffer_[index];
    }
    
    // 获取缓冲区首地址
    T* data() {
        return buffer_.data();
    }
    
    const T* data() const {
        return buffer_.data();
    }
    
    // STL兼容接口
    typename std::vector<T>::iterator begin() { return buffer_.begin(); }
    typename std::vector<T>::iterator end() { return buffer_.end(); }
    typename std::vector<T>::const_iterator begin() const { return buffer_.begin(); }
    typename std::vector<T>::const_iterator end() const { return buffer_.end(); }
    
    // 调整大小
    void resize(size_t new_size) {
        buffer_.resize(new_size);
    }
    
    // 调整大小并填充值
    void resize(size_t new_size, const T& value) {
        buffer_.resize(new_size, value);
    }
    
    // 获取原始缓冲区
    std::vector<T>& getBuffer() {
        return buffer_;
    }
    
    const std::vector<T>& getBuffer() const {
        return buffer_;
    }

private:
    std::vector<T> buffer_;
};

// 线程局部存储缓冲池
template<typename T>
class ThreadLocalBufferPool {
public:
    // 创建指定初始大小的缓冲池
    explicit ThreadLocalBufferPool(size_t initialSize = 1024) : initial_size_(initialSize) {}
    
    // 获取或创建缓冲区
    ReusableBuffer<T>& getBuffer() {
        thread_local ReusableBuffer<T> buffer(initial_size_);
        return buffer;
    }
    
    // 使用缓冲区执行操作
    template<typename Func>
    auto withBuffer(Func&& func) -> decltype(func(std::declval<ReusableBuffer<T>&>())) {
        auto& buffer = getBuffer();
        buffer.reset();
        return func(buffer);
    }

private:
    size_t initial_size_;
};

// 静态实例，用于简化使用
template<typename T>
ThreadLocalBufferPool<T>& getThreadLocalBufferPool() {
    static ThreadLocalBufferPool<T> pool;
    return pool;
}

} // namespace zero_latency