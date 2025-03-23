#pragma once

#include <variant>
#include <string>
#include <optional>
#include <utility>
#include <type_traits>
#include <system_error>
#include "logger.h"

namespace zero_latency {

// 错误码枚举
enum class ErrorCode {
    // 通用错误
    OK = 0,
    UNKNOWN_ERROR = 1,
    INVALID_ARGUMENT = 2,
    NOT_INITIALIZED = 3,
    TIMEOUT = 4,
    
    // 网络错误
    NETWORK_ERROR = 100,
    CONNECTION_FAILED = 101,
    SOCKET_ERROR = 102,
    INVALID_PACKET = 103,
    PACKET_TOO_LARGE = 104,
    PROTOCOL_ERROR = 105,
    
    // 推理错误
    INFERENCE_ERROR = 200,
    MODEL_NOT_FOUND = 201,
    MODEL_LOAD_FAILED = 202,
    INVALID_INPUT = 203,
    INFERENCE_TIMEOUT = 204,
    
    // 系统错误
    SYSTEM_ERROR = 300,
    FILE_NOT_FOUND = 301,
    FILE_ACCESS_DENIED = 302,
    INSUFFICIENT_RESOURCES = 303,
    
    // 配置错误
    CONFIG_ERROR = 400,
    CONFIG_NOT_FOUND = 401,
    CONFIG_PARSE_ERROR = 402,
    CONFIG_INVALID = 403
};

// 错误对象
struct Error {
    ErrorCode code;
    std::string message;
    
    Error() : code(ErrorCode::OK), message("") {}
    Error(ErrorCode c, std::string msg) : code(c), message(std::move(msg)) {}
    
    bool isOk() const { return code == ErrorCode::OK; }
    
    // 转换为布尔值，当没有错误时为true
    explicit operator bool() const { return isOk(); }
    
    // 转换为std::error_code以便与标准库兼容
    std::error_code toStdErrorCode() const {
        // 简单的实现，实际应用中可能需要更详细的error_category
        return std::error_code(static_cast<int>(code), std::generic_category());
    }
    
    std::string toString() const {
        return "Error " + std::to_string(static_cast<int>(code)) + ": " + message;
    }
};

// Result模板类，用于函数返回结果或错误
template<typename T = void>
class Result {
public:
    // 成功结果构造函数
    template<typename U = T, 
             typename = typename std::enable_if<!std::is_void<U>::value>::type>
    static Result<U> ok(const U& value) {
        return Result<U>(value);
    }
    
    template<typename U = T, 
             typename = typename std::enable_if<!std::is_void<U>::value>::type>
    static Result<U> ok(U&& value) {
        return Result<U>(std::forward<U>(value));
    }
    
    // void特化的成功构造函数
    template<typename U = T, 
             typename = typename std::enable_if<std::is_void<U>::value>::type>
    static Result<void> ok() {
        return Result<void>();
    }
    
    // 错误结果构造函数
    template<typename U = T>
    static Result<U> error(ErrorCode code, const std::string& message) {
        return Result<U>(Error(code, message));
    }
    
    template<typename U = T>
    static Result<U> error(const Error& error) {
        return Result<U>(error);
    }
    
    // 判断是否成功
    bool isOk() const {
        return !hasError();
    }
    
    // 判断是否有错误
    bool hasError() const {
        if constexpr (std::is_void<T>::value) {
            return error_.has_value();
        } else {
            return std::holds_alternative<Error>(result_);
        }
    }
    
    // 获取错误对象
    const Error& error() const {
        if constexpr (std::is_void<T>::value) {
            static const Error default_error;
            return error_.value_or(default_error);
        } else {
            if (std::holds_alternative<Error>(result_)) {
                return std::get<Error>(result_);
            }
            static const Error default_error;
            return default_error;
        }
    }
    
    // 获取结果值（非void类型）
    template<typename U = T, 
             typename = typename std::enable_if<!std::is_void<U>::value>::type>
    const U& value() const {
        if (hasError()) {
            LOG_ERROR("Attempted to get value from an error result: " + error().toString());
            throw std::runtime_error("Result contains an error, not a value");
        }
        return std::get<U>(result_);
    }
    
    template<typename U = T, 
             typename = typename std::enable_if<!std::is_void<U>::value>::type>
    U& value() {
        if (hasError()) {
            LOG_ERROR("Attempted to get value from an error result: " + error().toString());
            throw std::runtime_error("Result contains an error, not a value");
        }
        return std::get<U>(result_);
    }
    
    // 转换为bool，当结果成功时为true
    explicit operator bool() const {
        return isOk();
    }
    
    // 打印错误并继续执行
    void logErrorAndContinue(const std::string& context = "") const {
        if (hasError()) {
            if (context.empty()) {
                LOG_ERROR(error().toString());
            } else {
                LOG_ERROR(context + ": " + error().toString());
            }
        }
    }
    
    // 打印错误并抛出异常
    void logErrorAndThrow(const std::string& context = "") const {
        if (hasError()) {
            std::string errorMsg;
            if (context.empty()) {
                errorMsg = error().toString();
            } else {
                errorMsg = context + ": " + error().toString();
            }
            LOG_ERROR(errorMsg);
            throw std::runtime_error(errorMsg);
        }
    }

private:
    // 构造函数为private，通过静态工厂方法创建
    Result() = default;  // void特化的成功结果
    
    template<typename U = T, 
             typename = typename std::enable_if<!std::is_void<U>::value>::type>
    explicit Result(const U& value) : result_(value) {}
    
    template<typename U = T, 
             typename = typename std::enable_if<!std::is_void<U>::value>::type>
    explicit Result(U&& value) : result_(std::forward<U>(value)) {}
    
    explicit Result(const Error& error) {
        if constexpr (std::is_void<T>::value) {
            error_ = error;
        } else {
            result_ = error;
        }
    }

private:
    // 根据T类型选择存储方式
    using ResultType = std::conditional_t<std::is_void<T>::value, 
                                         void, 
                                         std::variant<T, Error>>;
    
    // void特化使用optional<Error>
    std::conditional_t<std::is_void<T>::value, 
                      std::optional<Error>, 
                      ResultType> result_;
    
    // 对于void类型，单独存储error_
    std::optional<Error> error_ = std::nullopt;
};

// 用于链式调用的管道操作符
template<typename T, typename Func>
auto operator|(Result<T> result, Func&& func) -> 
    decltype(func(std::declval<T>())) {
    if (result.hasError()) {
        return decltype(func(std::declval<T>()))::error(result.error());
    }
    
    if constexpr (std::is_void<T>::value) {
        return func();
    } else {
        return func(result.value());
    }
}

} // namespace zero_latency