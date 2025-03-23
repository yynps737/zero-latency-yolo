#pragma once

#include <string>
#include <fstream>
#include <iostream>
#include <mutex>
#include <memory>
#include <chrono>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <ctime>
#include <functional>
#include <unordered_map>

namespace zero_latency {

// 日志级别枚举
enum class LogLevel {
    TRACE,
    DEBUG,
    INFO,
    WARN,
    ERROR,
    FATAL,
    OFF
};

// 将日志级别转换为字符串
inline std::string logLevelToString(LogLevel level) {
    switch (level) {
        case LogLevel::TRACE: return "TRACE";
        case LogLevel::DEBUG: return "DEBUG";
        case LogLevel::INFO:  return "INFO";
        case LogLevel::WARN:  return "WARN";
        case LogLevel::ERROR: return "ERROR";
        case LogLevel::FATAL: return "FATAL";
        case LogLevel::OFF:   return "OFF";
        default:              return "UNKNOWN";
    }
}

// 将字符串转换为日志级别
inline LogLevel stringToLogLevel(const std::string& level) {
    if (level == "TRACE") return LogLevel::TRACE;
    if (level == "DEBUG") return LogLevel::DEBUG;
    if (level == "INFO")  return LogLevel::INFO;
    if (level == "WARN")  return LogLevel::WARN;
    if (level == "ERROR") return LogLevel::ERROR;
    if (level == "FATAL") return LogLevel::FATAL;
    if (level == "OFF")   return LogLevel::OFF;
    return LogLevel::INFO; // 默认
}

// 获取当前时间戳字符串
inline std::string getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S");
    oss << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

// 日志接口
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(LogLevel level, const std::string& message) = 0;
    virtual void flush() = 0;
    // 获取最小记录日志级别
    virtual LogLevel getLevel() const = 0;
    virtual void setLevel(LogLevel level) = 0;
};

// 文件日志实现
class FileLogger : public ILogger {
public:
    FileLogger(const std::string& filename, LogLevel level = LogLevel::INFO, size_t max_size = 10 * 1024 * 1024)
        : level_(level), max_size_(max_size), file_size_(0) {
        
        // 创建目录
        std::filesystem::path filepath(filename);
        if (filepath.has_parent_path()) {
            std::filesystem::create_directories(filepath.parent_path());
        }

        open(filename);
    }

    ~FileLogger() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.flush();
            file_.close();
        }
    }

    void log(LogLevel level, const std::string& message) override {
        if (level < level_) return;

        std::lock_guard<std::mutex> lock(mutex_);

        // 检查文件是否需要轮转
        if (file_size_ >= max_size_) {
            rotateLog();
        }

        if (file_.is_open()) {
            std::string log_line = getCurrentTimeString() + " [" + logLevelToString(level) + "] " + message + "\n";
            file_ << log_line;
            file_size_ += log_line.size();
            
            // 对于错误和致命错误，立即刷新
            if (level >= LogLevel::ERROR) {
                file_.flush();
            }
        }
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) {
            file_.flush();
        }
    }

    LogLevel getLevel() const override {
        return level_;
    }

    void setLevel(LogLevel level) override {
        level_ = level;
    }

private:
    void open(const std::string& filename) {
        file_.open(filename, std::ios::app);
        if (!file_.is_open()) {
            std::cerr << "无法打开日志文件: " << filename << std::endl;
            return;
        }

        // 获取当前文件大小
        file_.seekp(0, std::ios::end);
        file_size_ = file_.tellp();
        
        std::string separator = "\n----------------------------------------\n";
        std::string start_msg = getCurrentTimeString() + " 日志系统初始化\n";
        file_ << separator << start_msg << separator;
        file_size_ += separator.size() * 2 + start_msg.size();
    }

    void rotateLog() {
        if (!file_.is_open()) return;

        // 关闭当前文件
        file_.close();

        // 重命名文件
        std::filesystem::path current_file(filename_);
        std::string basename = current_file.stem().string();
        std::string extension = current_file.extension().string();
        std::string directory = current_file.parent_path().string();

        std::string timestamp = getCurrentTimeString();
        std::replace(timestamp.begin(), timestamp.end(), ' ', '_');
        std::replace(timestamp.begin(), timestamp.end(), ':', '-');

        std::string new_filename = directory + "/" + basename + "_" + timestamp + extension;
        
        try {
            std::filesystem::rename(filename_, new_filename);
        } catch (const std::exception& e) {
            std::cerr << "无法重命名日志文件: " << e.what() << std::endl;
        }

        // 重新打开新文件
        open(filename_);
    }

    std::mutex mutex_;
    std::ofstream file_;
    std::string filename_;
    LogLevel level_;
    size_t max_size_; // 最大文件大小 (字节)
    size_t file_size_; // 当前文件大小
};

// 控制台日志实现
class ConsoleLogger : public ILogger {
public:
    ConsoleLogger(LogLevel level = LogLevel::INFO) : level_(level) {}

    void log(LogLevel level, const std::string& message) override {
        if (level < level_) return;

        std::lock_guard<std::mutex> lock(mutex_);
        std::string log_line = getCurrentTimeString() + " [" + logLevelToString(level) + "] " + message;
        
        // 根据日志级别选择输出流
        if (level >= LogLevel::ERROR) {
            std::cerr << log_line << std::endl;
        } else {
            std::cout << log_line << std::endl;
        }
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout.flush();
        std::cerr.flush();
    }

    LogLevel getLevel() const override {
        return level_;
    }

    void setLevel(LogLevel level) override {
        level_ = level;
    }

private:
    std::mutex mutex_;
    LogLevel level_;
};

// 日志管理类 (单例)
class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    // 添加日志接口
    void addLogger(const std::string& name, std::shared_ptr<ILogger> logger) {
        std::lock_guard<std::mutex> lock(mutex_);
        loggers_[name] = logger;
    }

    // 移除日志接口
    void removeLogger(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        loggers_.erase(name);
    }

    // 设置全局最小日志级别
    void setLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
    }

    // 记录日志
    void log(LogLevel level, const std::string& message) {
        if (level < level_) return;

        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, logger] : loggers_) {
            logger->log(level, message);
        }
    }

    // 刷新所有日志
    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, logger] : loggers_) {
            logger->flush();
        }
    }

private:
    Logger() : level_(LogLevel::INFO) {
        // 默认添加控制台日志
        addLogger("console", std::make_shared<ConsoleLogger>(LogLevel::INFO));
    }
    
    ~Logger() {
        flush();
    }

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<ILogger>> loggers_;
    LogLevel level_;
};

// 便捷宏定义
#define LOG_TRACE(message) zero_latency::Logger::getInstance().log(zero_latency::LogLevel::TRACE, message)
#define LOG_DEBUG(message) zero_latency::Logger::getInstance().log(zero_latency::LogLevel::DEBUG, message)
#define LOG_INFO(message)  zero_latency::Logger::getInstance().log(zero_latency::LogLevel::INFO, message)
#define LOG_WARN(message)  zero_latency::Logger::getInstance().log(zero_latency::LogLevel::WARN, message)
#define LOG_ERROR(message) zero_latency::Logger::getInstance().log(zero_latency::LogLevel::ERROR, message)
#define LOG_FATAL(message) zero_latency::Logger::getInstance().log(zero_latency::LogLevel::FATAL, message)

// 初始化日志系统
inline void initLogger(const std::string& logfile = "logs/server.log", LogLevel fileLevel = LogLevel::INFO, LogLevel consoleLevel = LogLevel::INFO) {
    auto& logger = Logger::getInstance();
    
    // 设置控制台日志级别
    auto consoleLogger = std::make_shared<ConsoleLogger>(consoleLevel);
    logger.addLogger("console", consoleLogger);
    
    // 添加文件日志
    auto fileLogger = std::make_shared<FileLogger>(logfile, fileLevel);
    logger.addLogger("file", fileLogger);
    
    LOG_INFO("日志系统初始化完成");
}

} // namespace zero_latency