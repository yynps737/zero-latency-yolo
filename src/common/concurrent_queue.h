#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <optional>
#include <vector>

namespace zero_latency {

// 线程安全的并发队列实现
template<typename T>
class ConcurrentQueue {
public:
    // 默认构造函数
    ConcurrentQueue() : capacity_(std::numeric_limits<size_t>::max()), shutdown_(false) {}
    
    // 指定容量的构造函数
    explicit ConcurrentQueue(size_t capacity) : capacity_(capacity), shutdown_(false) {}
    
    // 析构函数
    ~ConcurrentQueue() {
        shutdown();
    }
    
    // 获取队列大小
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    // 检查队列是否为空
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
    // 获取队列容量
    size_t capacity() const {
        return capacity_;
    }
    
    // 清空队列
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        std::swap(queue_, empty);
    }
    
    // 添加元素到队列尾部
    bool push(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 检查队列是否已满
        if (queue_.size() >= capacity_) {
            return false;
        }
        
        // 检查是否已关闭
        if (shutdown_) {
            return false;
        }
        
        // 添加元素
        queue_.push(item);
        
        // 通知等待中的线程
        lock.unlock();
        cond_not_empty_.notify_one();
        
        return true;
    }
    
    // 添加元素到队列尾部 (移动语义)
    bool push(T&& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 检查队列是否已满
        if (queue_.size() >= capacity_) {
            return false;
        }
        
        // 检查是否已关闭
        if (shutdown_) {
            return false;
        }
        
        // 添加元素
        queue_.push(std::move(item));
        
        // 通知等待中的线程
        lock.unlock();
        cond_not_empty_.notify_one();
        
        return true;
    }
    
    // 尝试添加元素，如果队列已满则丢弃最老的元素
    bool push_force(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 检查是否已关闭
        if (shutdown_) {
            return false;
        }
        
        // 如果队列已满，移除最旧的元素
        if (queue_.size() >= capacity_) {
            queue_.pop();
        }
        
        // 添加元素
        queue_.push(item);
        
        // 通知等待中的线程
        lock.unlock();
        cond_not_empty_.notify_one();
        
        return true;
    }
    
    // 尝试添加元素，如果队列已满则丢弃最老的元素 (移动语义)
    bool push_force(T&& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 检查是否已关闭
        if (shutdown_) {
            return false;
        }
        
        // 如果队列已满，移除最旧的元素
        if (queue_.size() >= capacity_) {
            queue_.pop();
        }
        
        // 添加元素
        queue_.push(std::move(item));
        
        // 通知等待中的线程
        lock.unlock();
        cond_not_empty_.notify_one();
        
        return true;
    }
    
    // 取出队列头部元素，如果队列为空则阻塞
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 等待直到队列非空或关闭
        cond_not_empty_.wait(lock, [this]() { return !queue_.empty() || shutdown_; });
        
        // 检查是否已关闭且队列为空
        if (queue_.empty()) {
            return false;
        }
        
        // 取出元素
        item = std::move(queue_.front());
        queue_.pop();
        
        return true;
    }
    
    // 尝试取出队列头部元素，如果队列为空则立即返回
    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (queue_.empty()) {
            return false;
        }
        
        // 取出元素
        item = std::move(queue_.front());
        queue_.pop();
        
        return true;
    }
    
    // 尝试取出队列头部元素，带超时
    template<typename Rep, typename Period>
    bool try_pop_for(T& item, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 等待直到队列非空或超时或关闭
        bool success = cond_not_empty_.wait_for(lock, timeout, [this]() { return !queue_.empty() || shutdown_; });
        
        // 检查是否已关闭且队列为空
        if (queue_.empty()) {
            return false;
        }
        
        // 取出元素
        item = std::move(queue_.front());
        queue_.pop();
        
        return true;
    }
    
    // 尝试取出队列头部元素，带超时点
    template<typename Clock, typename Duration>
    bool try_pop_until(T& item, const std::chrono::time_point<Clock, Duration>& timeout_time) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 等待直到队列非空或超时或关闭
        bool success = cond_not_empty_.wait_until(lock, timeout_time, [this]() { return !queue_.empty() || shutdown_; });
        
        // 检查是否已关闭且队列为空
        if (queue_.empty()) {
            return false;
        }
        
        // 取出元素
        item = std::move(queue_.front());
        queue_.pop();
        
        return true;
    }
    
    // 获取队列中的所有元素
    std::vector<T> get_all() {
        std::vector<T> result;
        std::lock_guard<std::mutex> lock(mutex_);
        
        result.reserve(queue_.size());
        while (!queue_.empty()) {
            result.push_back(std::move(queue_.front()));
            queue_.pop();
        }
        
        return result;
    }
    
    // 查看队列头部元素，不移除
    std::optional<T> peek() const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (queue_.empty()) {
            return std::nullopt;
        }
        
        return queue_.front();
    }
    
    // 设置队列容量
    void set_capacity(size_t capacity) {
        std::lock_guard<std::mutex> lock(mutex_);
        capacity_ = capacity;
        
        // 如果当前队列大小超过新容量，移除多余元素
        while (queue_.size() > capacity_) {
            queue_.pop();
        }
    }
    
    // 关闭队列，唤醒所有等待线程
    void shutdown() {
        std::unique_lock<std::mutex> lock(mutex_);
        shutdown_ = true;
        lock.unlock();
        
        // 唤醒所有等待线程
        cond_not_empty_.notify_all();
    }
    
    // 重新打开队列
    void resume() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = false;
    }
    
    // 检查队列是否已关闭
    bool is_shutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_;
    }

private:
    std::queue<T> queue_;                 // 底层队列
    mutable std::mutex mutex_;            // 互斥锁
    std::condition_variable cond_not_empty_; // 非空条件变量
    size_t capacity_;                     // 队列容量
    std::atomic<bool> shutdown_;          // 关闭标志
};

// 带优先级的并发队列实现
template<typename T, typename Priority = int>
class ConcurrentPriorityQueue {
public:
    // 用于比较优先级的结构体
    struct PriorityCompare {
        bool operator()(const std::pair<Priority, T>& lhs, const std::pair<Priority, T>& rhs) const {
            return lhs.first < rhs.first;  // 优先级高的在队列前面
        }
    };
    
    // 默认构造函数
    ConcurrentPriorityQueue() : capacity_(std::numeric_limits<size_t>::max()), shutdown_(false) {}
    
    // 指定容量的构造函数
    explicit ConcurrentPriorityQueue(size_t capacity) : capacity_(capacity), shutdown_(false) {}
    
    // 析构函数
    ~ConcurrentPriorityQueue() {
        shutdown();
    }
    
    // 获取队列大小
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }
    
    // 检查队列是否为空
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }
    
    // 获取队列容量
    size_t capacity() const {
        return capacity_;
    }
    
    // 清空队列
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) {
            queue_.pop();
        }
    }
    
    // 添加元素到队列，按优先级排序
    bool push(const T& item, Priority priority) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 检查队列是否已满
        if (queue_.size() >= capacity_) {
            return false;
        }
        
        // 检查是否已关闭
        if (shutdown_) {
            return false;
        }
        
        // 添加元素
        queue_.push(std::make_pair(priority, item));
        
        // 通知等待中的线程
        lock.unlock();
        cond_not_empty_.notify_one();
        
        return true;
    }
    
    // 添加元素到队列，按优先级排序 (移动语义)
    bool push(T&& item, Priority priority) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 检查队列是否已满
        if (queue_.size() >= capacity_) {
            return false;
        }
        
        // 检查是否已关闭
        if (shutdown_) {
            return false;
        }
        
        // 添加元素
        queue_.push(std::make_pair(priority, std::move(item)));
        
        // 通知等待中的线程
        lock.unlock();
        cond_not_empty_.notify_one();
        
        return true;
    }
    
    // 取出队列头部元素，如果队列为空则阻塞
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 等待直到队列非空或关闭
        cond_not_empty_.wait(lock, [this]() { return !queue_.empty() || shutdown_; });
        
        // 检查是否已关闭且队列为空
        if (queue_.empty()) {
            return false;
        }
        
        // 取出元素
        item = std::move(queue_.top().second);
        queue_.pop();
        
        return true;
    }
    
    // 尝试取出队列头部元素，如果队列为空则立即返回
    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (queue_.empty()) {
            return false;
        }
        
        // 取出元素
        item = std::move(queue_.top().second);
        queue_.pop();
        
        return true;
    }
    
    // 尝试取出队列头部元素，带超时
    template<typename Rep, typename Period>
    bool try_pop_for(T& item, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // 等待直到队列非空或超时或关闭
        bool success = cond_not_empty_.wait_for(lock, timeout, [this]() { return !queue_.empty() || shutdown_; });
        
        // 检查是否已关闭且队列为空
        if (queue_.empty()) {
            return false;
        }
        
        // 取出元素
        item = std::move(queue_.top().second);
        queue_.pop();
        
        return true;
    }
    
    // 获取队列中的所有元素，按优先级顺序
    std::vector<T> get_all() {
        std::vector<T> result;
        std::lock_guard<std::mutex> lock(mutex_);
        
        result.reserve(queue_.size());
        while (!queue_.empty()) {
            result.push_back(std::move(queue_.top().second));
            queue_.pop();
        }
        
        return result;
    }
    
    // 关闭队列，唤醒所有等待线程
    void shutdown() {
        std::unique_lock<std::mutex> lock(mutex_);
        shutdown_ = true;
        lock.unlock();
        
        // 唤醒所有等待线程
        cond_not_empty_.notify_all();
    }
    
    // 重新打开队列
    void resume() {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = false;
    }
    
    // 检查队列是否已关闭
    bool is_shutdown() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return shutdown_;
    }

private:
    std::priority_queue<std::pair<Priority, T>, std::vector<std::pair<Priority, T>>, PriorityCompare> queue_; // 优先级队列
    mutable std::mutex mutex_;            // 互斥锁
    std::condition_variable cond_not_empty_; // 非空条件变量
    size_t capacity_;                     // 队列容量
    std::atomic<bool> shutdown_;          // 关闭标志
};

} // namespace zero_latency