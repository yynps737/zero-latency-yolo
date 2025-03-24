#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <optional>
#include <vector>

namespace zero_latency {

template<typename T>
class ConcurrentQueue {
public:
    ConcurrentQueue() : capacity_(std::numeric_limits<size_t>::max()), shutdown_(false) {}
    explicit ConcurrentQueue(size_t capacity) : capacity_(capacity), shutdown_(false) {}
    ~ConcurrentQueue() { shutdown(); }
    
    size_t size() const { std::lock_guard<std::mutex> lock(mutex_); return queue_.size(); }
    bool empty() const { std::lock_guard<std::mutex> lock(mutex_); return queue_.empty(); }
    size_t capacity() const { return capacity_; }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::queue<T> empty;
        std::swap(queue_, empty);
    }
    
    bool push(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_ || shutdown_) return false;
        queue_.push(item);
        lock.unlock();
        cond_not_empty_.notify_one();
        return true;
    }
    
    bool push(T&& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_ || shutdown_) return false;
        queue_.push(std::move(item));
        lock.unlock();
        cond_not_empty_.notify_one();
        return true;
    }
    
    bool push_force(const T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (shutdown_) return false;
        if (queue_.size() >= capacity_) queue_.pop();
        queue_.push(item);
        lock.unlock();
        cond_not_empty_.notify_one();
        return true;
    }
    
    bool push_force(T&& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (shutdown_) return false;
        if (queue_.size() >= capacity_) queue_.pop();
        queue_.push(std::move(item));
        lock.unlock();
        cond_not_empty_.notify_one();
        return true;
    }
    
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_not_empty_.wait(lock, [this]() { return !queue_.empty() || shutdown_; });
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    
    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    
    template<typename Rep, typename Period>
    bool try_pop_for(T& item, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        bool success = cond_not_empty_.wait_for(lock, timeout, [this]() { return !queue_.empty() || shutdown_; });
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    
    template<typename Clock, typename Duration>
    bool try_pop_until(T& item, const std::chrono::time_point<Clock, Duration>& timeout_time) {
        std::unique_lock<std::mutex> lock(mutex_);
        bool success = cond_not_empty_.wait_until(lock, timeout_time, [this]() { return !queue_.empty() || shutdown_; });
        if (queue_.empty()) return false;
        item = std::move(queue_.front());
        queue_.pop();
        return true;
    }
    
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
    
    std::optional<T> peek() const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        return queue_.front();
    }
    
    void set_capacity(size_t capacity) {
        std::lock_guard<std::mutex> lock(mutex_);
        capacity_ = capacity;
        while (queue_.size() > capacity_) queue_.pop();
    }
    
    void shutdown() {
        std::unique_lock<std::mutex> lock(mutex_);
        shutdown_ = true;
        lock.unlock();
        cond_not_empty_.notify_all();
    }
    
    void resume() { std::lock_guard<std::mutex> lock(mutex_); shutdown_ = false; }
    bool is_shutdown() const { std::lock_guard<std::mutex> lock(mutex_); return shutdown_; }

private:
    std::queue<T> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_not_empty_;
    size_t capacity_;
    std::atomic<bool> shutdown_;
};

template<typename T, typename Priority = int>
class ConcurrentPriorityQueue {
public:
    struct PriorityCompare {
        bool operator()(const std::pair<Priority, T>& lhs, const std::pair<Priority, T>& rhs) const {
            return lhs.first < rhs.first;
        }
    };
    
    ConcurrentPriorityQueue() : capacity_(std::numeric_limits<size_t>::max()), shutdown_(false) {}
    explicit ConcurrentPriorityQueue(size_t capacity) : capacity_(capacity), shutdown_(false) {}
    ~ConcurrentPriorityQueue() { shutdown(); }
    
    size_t size() const { std::lock_guard<std::mutex> lock(mutex_); return queue_.size(); }
    bool empty() const { std::lock_guard<std::mutex> lock(mutex_); return queue_.empty(); }
    size_t capacity() const { return capacity_; }
    
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        while (!queue_.empty()) queue_.pop();
    }
    
    bool push(const T& item, Priority priority) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_ || shutdown_) return false;
        queue_.push(std::make_pair(priority, item));
        lock.unlock();
        cond_not_empty_.notify_one();
        return true;
    }
    
    bool push(T&& item, Priority priority) {
        std::unique_lock<std::mutex> lock(mutex_);
        if (queue_.size() >= capacity_ || shutdown_) return false;
        queue_.push(std::make_pair(priority, std::move(item)));
        lock.unlock();
        cond_not_empty_.notify_one();
        return true;
    }
    
    bool pop(T& item) {
        std::unique_lock<std::mutex> lock(mutex_);
        cond_not_empty_.wait(lock, [this]() { return !queue_.empty() || shutdown_; });
        if (queue_.empty()) return false;
        item = std::move(queue_.top().second);
        queue_.pop();
        return true;
    }
    
    bool try_pop(T& item) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return false;
        item = std::move(queue_.top().second);
        queue_.pop();
        return true;
    }
    
    template<typename Rep, typename Period>
    bool try_pop_for(T& item, const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        bool success = cond_not_empty_.wait_for(lock, timeout, [this]() { return !queue_.empty() || shutdown_; });
        if (queue_.empty()) return false;
        item = std::move(queue_.top().second);
        queue_.pop();
        return true;
    }
    
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
    
    void shutdown() {
        std::unique_lock<std::mutex> lock(mutex_);
        shutdown_ = true;
        lock.unlock();
        cond_not_empty_.notify_all();
    }
    
    void resume() { std::lock_guard<std::mutex> lock(mutex_); shutdown_ = false; }
    bool is_shutdown() const { std::lock_guard<std::mutex> lock(mutex_); return shutdown_; }

private:
    std::priority_queue<std::pair<Priority, T>, std::vector<std::pair<Priority, T>>, PriorityCompare> queue_;
    mutable std::mutex mutex_;
    std::condition_variable cond_not_empty_;
    size_t capacity_;
    std::atomic<bool> shutdown_;
};

}