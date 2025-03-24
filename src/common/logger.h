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

enum class LogLevel { TRACE, DEBUG, INFO, WARN, ERROR, FATAL, OFF };

inline std::string logLevelToString(LogLevel level) {
    static const char* levels[] = {"TRACE", "DEBUG", "INFO", "WARN", "ERROR", "FATAL", "OFF"};
    return levels[static_cast<int>(level)];
}

inline LogLevel stringToLogLevel(const std::string& level) {
    if (level == "TRACE") return LogLevel::TRACE;
    if (level == "DEBUG") return LogLevel::DEBUG;
    if (level == "INFO")  return LogLevel::INFO;
    if (level == "WARN")  return LogLevel::WARN;
    if (level == "ERROR") return LogLevel::ERROR;
    if (level == "FATAL") return LogLevel::FATAL;
    if (level == "OFF")   return LogLevel::OFF;
    return LogLevel::INFO;
}

inline std::string getCurrentTimeString() {
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") << '.' 
        << std::setfill('0') << std::setw(3) << ms.count();
    return oss.str();
}

class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(LogLevel level, const std::string& message) = 0;
    virtual void flush() = 0;
    virtual LogLevel getLevel() const = 0;
    virtual void setLevel(LogLevel level) = 0;
};

class FileLogger : public ILogger {
public:
    FileLogger(const std::string& filename, LogLevel level = LogLevel::INFO, size_t max_size = 10 * 1024 * 1024)
        : level_(level), max_size_(max_size), file_size_(0), filename_(filename) {
        std::filesystem::path filepath(filename);
        if (filepath.has_parent_path()) std::filesystem::create_directories(filepath.parent_path());
        open(filename);
    }

    ~FileLogger() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) { file_.flush(); file_.close(); }
    }

    void log(LogLevel level, const std::string& message) override {
        if (level < level_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_size_ >= max_size_) rotateLog();
        if (file_.is_open()) {
            std::string log_line = getCurrentTimeString() + " [" + logLevelToString(level) + "] " + message + "\n";
            file_ << log_line;
            file_size_ += log_line.size();
            if (level >= LogLevel::ERROR) file_.flush();
        }
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        if (file_.is_open()) file_.flush();
    }

    LogLevel getLevel() const override { return level_; }
    void setLevel(LogLevel level) override { level_ = level; }

private:
    void open(const std::string& filename) {
        file_.open(filename, std::ios::app);
        if (!file_.is_open()) {
            std::cerr << "无法打开日志文件: " << filename << std::endl;
            return;
        }
        file_.seekp(0, std::ios::end);
        file_size_ = file_.tellp();
        std::string separator = "\n----------------------------------------\n";
        std::string start_msg = getCurrentTimeString() + " 日志系统初始化\n";
        file_ << separator << start_msg << separator;
        file_size_ += separator.size() * 2 + start_msg.size();
    }

    void rotateLog() {
        if (!file_.is_open()) return;
        file_.close();
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
        open(filename_);
    }

    std::mutex mutex_;
    std::ofstream file_;
    std::string filename_;
    LogLevel level_;
    size_t max_size_, file_size_;
};

class ConsoleLogger : public ILogger {
public:
    ConsoleLogger(LogLevel level = LogLevel::INFO) : level_(level) {}

    void log(LogLevel level, const std::string& message) override {
        if (level < level_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        std::string log_line = getCurrentTimeString() + " [" + logLevelToString(level) + "] " + message;
        if (level >= LogLevel::ERROR) std::cerr << log_line << std::endl;
        else std::cout << log_line << std::endl;
    }

    void flush() override {
        std::lock_guard<std::mutex> lock(mutex_);
        std::cout.flush(); std::cerr.flush();
    }

    LogLevel getLevel() const override { return level_; }
    void setLevel(LogLevel level) override { level_ = level; }

private:
    std::mutex mutex_;
    LogLevel level_;
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance;
        return instance;
    }

    void addLogger(const std::string& name, std::shared_ptr<ILogger> logger) {
        std::lock_guard<std::mutex> lock(mutex_);
        loggers_[name] = logger;
    }

    void removeLogger(const std::string& name) {
        std::lock_guard<std::mutex> lock(mutex_);
        loggers_.erase(name);
    }

    void setLevel(LogLevel level) {
        std::lock_guard<std::mutex> lock(mutex_);
        level_ = level;
    }

    void log(LogLevel level, const std::string& message) {
        if (level < level_) return;
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, logger] : loggers_) logger->log(level, message);
    }

    void flush() {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& [name, logger] : loggers_) logger->flush();
    }

private:
    Logger() : level_(LogLevel::INFO) {
        addLogger("console", std::make_shared<ConsoleLogger>(LogLevel::INFO));
    }
    ~Logger() { flush(); }
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<ILogger>> loggers_;
    LogLevel level_;
};

#define LOG_TRACE(message) zero_latency::Logger::getInstance().log(zero_latency::LogLevel::TRACE, message)
#define LOG_DEBUG(message) zero_latency::Logger::getInstance().log(zero_latency::LogLevel::DEBUG, message)
#define LOG_INFO(message)  zero_latency::Logger::getInstance().log(zero_latency::LogLevel::INFO, message)
#define LOG_WARN(message)  zero_latency::Logger::getInstance().log(zero_latency::LogLevel::WARN, message)
#define LOG_ERROR(message) zero_latency::Logger::getInstance().log(zero_latency::LogLevel::ERROR, message)
#define LOG_FATAL(message) zero_latency::Logger::getInstance().log(zero_latency::LogLevel::FATAL, message)

inline void initLogger(const std::string& logfile = "logs/server.log", LogLevel fileLevel = LogLevel::INFO, LogLevel consoleLevel = LogLevel::INFO) {
    auto& logger = Logger::getInstance();
    logger.addLogger("console", std::make_shared<ConsoleLogger>(consoleLevel));
    logger.addLogger("file", std::make_shared<FileLogger>(logfile, fileLevel));
    LOG_INFO("日志系统初始化完成");
}

}