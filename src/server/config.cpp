#include "config.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <experimental/filesystem>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>





namespace zero_latency {
    namespace fs = std::experimental::filesystem;

// 简易JSON解析实现
// 注意：在生产环境中，应使用完整的JSON库，如nlohmann/json或RapidJSON
namespace json {

class Value {
public:
    enum Type {
        NULL_TYPE,
        BOOL_TYPE,
        NUMBER_TYPE,
        STRING_TYPE,
        ARRAY_TYPE,
        OBJECT_TYPE
    };

    Value() : type_(NULL_TYPE) {}
    Value(bool value) : type_(BOOL_TYPE), bool_value_(value) {}
    Value(int value) : type_(NUMBER_TYPE), number_value_(static_cast<double>(value)) {}
    Value(double value) : type_(NUMBER_TYPE), number_value_(value) {}
    Value(const std::string& value) : type_(STRING_TYPE), string_value_(value) {}
    Value(const char* value) : type_(STRING_TYPE), string_value_(value) {}

    Type type() const { return type_; }
    
    bool asBool() const {
        if (type_ != BOOL_TYPE) throw std::runtime_error("Not a boolean");
        return bool_value_;
    }
    
    int asInt() const {
        if (type_ != NUMBER_TYPE) throw std::runtime_error("Not a number");
        return static_cast<int>(number_value_);
    }
    
    double asDouble() const {
        if (type_ != NUMBER_TYPE) throw std::runtime_error("Not a number");
        return number_value_;
    }
    
    std::string asString() const {
        if (type_ != STRING_TYPE) throw std::runtime_error("Not a string");
        return string_value_;
    }
    
    void setObject() { 
        type_ = OBJECT_TYPE; 
        object_value_.clear();
    }
    
    void setArray() { 
        type_ = ARRAY_TYPE; 
        array_value_.clear();
    }
    
    void set(const std::string& key, const Value& value) {
        if (type_ != OBJECT_TYPE) setObject();
        object_value_[key] = value;
    }
    
    void add(const Value& value) {
        if (type_ != ARRAY_TYPE) setArray();
        array_value_.push_back(value);
    }
    
    bool has(const std::string& key) const {
        if (type_ != OBJECT_TYPE) return false;
        return object_value_.find(key) != object_value_.end();
    }
    
    Value get(const std::string& key) const {
        if (type_ != OBJECT_TYPE) throw std::runtime_error("Not an object");
        auto it = object_value_.find(key);
        if (it == object_value_.end()) return Value();
        return it->second;
    }
    
    Value operator[](const std::string& key) const {
        return get(key);
    }
    
    std::string toString() const {
        std::ostringstream oss;
        if (type_ == NULL_TYPE) {
            oss << "null";
        } else if (type_ == BOOL_TYPE) {
            oss << (bool_value_ ? "true" : "false");
        } else if (type_ == NUMBER_TYPE) {
            oss << number_value_;
        } else if (type_ == STRING_TYPE) {
            oss << "\"" << string_value_ << "\"";
        } else if (type_ == ARRAY_TYPE) {
            oss << "[";
            for (size_t i = 0; i < array_value_.size(); ++i) {
                if (i > 0) oss << ", ";
                oss << array_value_[i].toString();
            }
            oss << "]";
        } else if (type_ == OBJECT_TYPE) {
            oss << "{" << std::endl;
            size_t i = 0;
            for (const auto& pair : object_value_) {
                if (i > 0) oss << "," << std::endl;
                oss << "  \"" << pair.first << "\": " << pair.second.toString();
                ++i;
            }
            oss << std::endl << "}";
        }
        return oss.str();
    }

private:
    Type type_;
    bool bool_value_ = false;
    double number_value_ = 0.0;
    std::string string_value_;
    std::vector<Value> array_value_;
    std::unordered_map<std::string, Value> object_value_;
};

// 极简JSON解析器
Value parse(const std::string& json_str) {
    enum State {
        START,
        OBJECT,
        ARRAY,
        KEY,
        COLON,
        VALUE,
        COMMA,
        STRING,
        NUMBER,
        BOOLEAN,
        NULL_VALUE
    };
    
    std::vector<Value> stack;
    std::vector<State> state_stack;
    std::string current_key;
    std::string current_value;
    State state = START;
    
    Value root;
    bool in_string = false;
    bool escaped = false;
    
    for (size_t i = 0; i < json_str.size(); ++i) {
        char c = json_str[i];
        
        // 跳过空白字符
        if (!in_string && (c == ' ' || c == '\t' || c == '\n' || c == '\r')) {
            continue;
        }
        
        if (state == START) {
            if (c == '{') {
                root.setObject();
                stack.push_back(root);
                state_stack.push_back(OBJECT);
                state = OBJECT;
            } else if (c == '[') {
                root.setArray();
                stack.push_back(root);
                state_stack.push_back(ARRAY);
                state = ARRAY;
            } else {
                throw std::runtime_error("Invalid JSON: must start with { or [");
            }
        } else if (state == OBJECT) {
            if (c == '}') {
                if (stack.size() <= 1) {
                    root = stack.back();
                    stack.pop_back();
                    state_stack.pop_back();
                    if (state_stack.empty()) {
                        state = START;
                    } else {
                        state = state_stack.back();
                    }
                } else {
                    Value obj = stack.back();
                    stack.pop_back();
                    state_stack.pop_back();
                    if (state_stack.back() == OBJECT) {
                        stack.back().set(current_key, obj);
                    } else if (state_stack.back() == ARRAY) {
                        stack.back().add(obj);
                    }
                    state = state_stack.back();
                }
            } else if (c == '\"') {
                in_string = true;
                current_key.clear();
                state = KEY;
            } else if (c == ',') {
                // 允许逗号后继续
                state = OBJECT;
            } else {
                throw std::runtime_error("Invalid JSON: expected \" or } in object");
            }
        } else if (state == ARRAY) {
            if (c == ']') {
                if (stack.size() <= 1) {
                    root = stack.back();
                    stack.pop_back();
                    state_stack.pop_back();
                    if (state_stack.empty()) {
                        state = START;
                    } else {
                        state = state_stack.back();
                    }
                } else {
                    Value arr = stack.back();
                    stack.pop_back();
                    state_stack.pop_back();
                    if (state_stack.back() == OBJECT) {
                        stack.back().set(current_key, arr);
                    } else if (state_stack.back() == ARRAY) {
                        stack.back().add(arr);
                    }
                    state = state_stack.back();
                }
            } else {
                // 解析数组元素
                if (c == '{') {
                    Value obj;
                    obj.setObject();
                    stack.push_back(obj);
                    state_stack.push_back(ARRAY);
                    state_stack.push_back(OBJECT);
                    state = OBJECT;
                } else if (c == '[') {
                    Value arr;
                    arr.setArray();
                    stack.push_back(arr);
                    state_stack.push_back(ARRAY);
                    state_stack.push_back(ARRAY);
                    state = ARRAY;
                } else if (c == '\"') {
                    in_string = true;
                    current_value.clear();
                    state = STRING;
                } else if (c == 't' || c == 'f') {
                    current_value.clear();
                    current_value += c;
                    state = BOOLEAN;
                } else if (c == 'n') {
                    current_value.clear();
                    current_value += c;
                    state = NULL_VALUE;
                } else if ((c >= '0' && c <= '9') || c == '-') {
                    current_value.clear();
                    current_value += c;
                    state = NUMBER;
                } else if (c == ',') {
                    // 允许逗号后继续
                    state = ARRAY;
                } else {
                    throw std::runtime_error("Invalid JSON: unexpected character in array");
                }
            }
        } else if (state == KEY) {
            if (in_string) {
                if (c == '\\' && !escaped) {
                    escaped = true;
                } else if (c == '\"' && !escaped) {
                    in_string = false;
                    state = COLON;
                } else {
                    if (escaped) {
                        if (c == 'n') c = '\n';
                        else if (c == 'r') c = '\r';
                        else if (c == 't') c = '\t';
                        escaped = false;
                    }
                    current_key += c;
                }
            }
        } else if (state == COLON) {
            if (c == ':') {
                state = VALUE;
            } else {
                throw std::runtime_error("Invalid JSON: expected : after key");
            }
        } else if (state == VALUE) {
            if (c == '{') {
                Value obj;
                obj.setObject();
                stack.push_back(obj);
                state_stack.push_back(OBJECT);
                state = OBJECT;
            } else if (c == '[') {
                Value arr;
                arr.setArray();
                stack.push_back(arr);
                state_stack.push_back(ARRAY);
                state = ARRAY;
            } else if (c == '\"') {
                in_string = true;
                current_value.clear();
                state = STRING;
            } else if (c == 't' || c == 'f') {
                current_value.clear();
                current_value += c;
                state = BOOLEAN;
            } else if (c == 'n') {
                current_value.clear();
                current_value += c;
                state = NULL_VALUE;
            } else if ((c >= '0' && c <= '9') || c == '-') {
                current_value.clear();
                current_value += c;
                state = NUMBER;
            } else {
                throw std::runtime_error("Invalid JSON: unexpected character in value");
            }
        } else if (state == STRING) {
            if (in_string) {
                if (c == '\\' && !escaped) {
                    escaped = true;
                } else if (c == '\"' && !escaped) {
                    in_string = false;
                    if (state_stack.back() == OBJECT) {
                        stack.back().set(current_key, Value(current_value));
                    } else if (state_stack.back() == ARRAY) {
                        stack.back().add(Value(current_value));
                    }
                    state = state_stack.back();
                } else {
                    if (escaped) {
                        if (c == 'n') c = '\n';
                        else if (c == 'r') c = '\r';
                        else if (c == 't') c = '\t';
                        escaped = false;
                    }
                    current_value += c;
                }
            }
        } else if (state == NUMBER) {
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                current_value += c;
            } else {
                // 数字结束
                double num_val = std::stod(current_value);
                if (state_stack.back() == OBJECT) {
                    stack.back().set(current_key, Value(num_val));
                } else if (state_stack.back() == ARRAY) {
                    stack.back().add(Value(num_val));
                }
                state = state_stack.back();
                
                // 重新处理当前字符
                --i;
            }
        } else if (state == BOOLEAN) {
            if ((current_value == "t" && c == 'r') ||
                (current_value == "tr" && c == 'u') ||
                (current_value == "tru" && c == 'e') ||
                (current_value == "f" && c == 'a') ||
                (current_value == "fa" && c == 'l') ||
                (current_value == "fal" && c == 's') ||
                (current_value == "fals" && c == 'e')) {
                current_value += c;
            } else {
                // 布尔值结束
                if (current_value == "true") {
                    if (state_stack.back() == OBJECT) {
                        stack.back().set(current_key, Value(true));
                    } else if (state_stack.back() == ARRAY) {
                        stack.back().add(Value(true));
                    }
                } else if (current_value == "false") {
                    if (state_stack.back() == OBJECT) {
                        stack.back().set(current_key, Value(false));
                    } else if (state_stack.back() == ARRAY) {
                        stack.back().add(Value(false));
                    }
                } else {
                    throw std::runtime_error("Invalid JSON: invalid boolean value");
                }
                state = state_stack.back();
                
                // 重新处理当前字符
                --i;
            }
        } else if (state == NULL_VALUE) {
            if ((current_value == "n" && c == 'u') ||
                (current_value == "nu" && c == 'l') ||
                (current_value == "nul" && c == 'l')) {
                current_value += c;
            } else {
                // null值结束
                if (current_value == "null") {
                    if (state_stack.back() == OBJECT) {
                        stack.back().set(current_key, Value());
                    } else if (state_stack.back() == ARRAY) {
                        stack.back().add(Value());
                    }
                } else {
                    throw std::runtime_error("Invalid JSON: invalid null value");
                }
                state = state_stack.back();
                
                // 重新处理当前字符
                --i;
            }
        } else if (state == COMMA) {
            if (c == ',') {
                if (state_stack.back() == OBJECT) {
                    state = OBJECT;
                } else if (state_stack.back() == ARRAY) {
                    state = ARRAY;
                }
            } else {
                throw std::runtime_error("Invalid JSON: expected , after value");
            }
        }
    }
    
    if (stack.size() != 0) {
        throw std::runtime_error("Invalid JSON: unclosed object or array");
    }
    
    return root;
}

} // namespace json

// 配置管理类实现
ConfigManager::ConfigManager() {
}

ConfigManager::~ConfigManager() {
}

bool ConfigManager::loadServerConfig(const std::string& path, ServerConfig& config) {
    // 检查文件是否存在
    if (!fs::exists(path)) {
        std::cerr << "配置文件不存在: " << path << std::endl;
        
        // 创建默认配置
        std::cout << "创建默认配置文件..." << std::endl;
        createDefaultServerConfig(path);
        
        // 返回默认配置
        config = ServerConfig();
        std::cout << "使用默认配置" << std::endl;
        return true;  // 返回true，因为我们处理了错误
    }
    
    // 打开并读取文件
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "无法打开配置文件: " << path << std::endl;
        std::cout << "使用默认配置" << std::endl;
        config = ServerConfig();
        return true;  // 返回true，因为我们使用了默认值
    }
    
    try {
        // 读取整个文件内容
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json_content = buffer.str();
        
        // 解析JSON
        json::Value root = json::parse(json_content);
        
        // 提取配置
        if (root.has("model_path")) {
            config.model_path = root["model_path"].asString();
        }
        
        if (root.has("port")) {
            config.port = root["port"].asInt();
        }
        
        if (root.has("web_port")) {
            config.web_port = root["web_port"].asInt();
        }
        
        if (root.has("max_clients")) {
            config.max_clients = root["max_clients"].asInt();
        }
        
        if (root.has("target_fps")) {
            config.target_fps = root["target_fps"].asInt();
        }
        
        if (root.has("confidence_threshold")) {
            config.confidence_threshold = root["confidence_threshold"].asDouble();
        }
        
        if (root.has("nms_threshold")) {
            config.nms_threshold = root["nms_threshold"].asDouble();
        }
        
        if (root.has("max_queue_size")) {
            config.max_queue_size = root["max_queue_size"].asInt();
        }
        
        if (root.has("use_cpu_affinity")) {
            config.use_cpu_affinity = root["use_cpu_affinity"].asBool();
        }
        
        if (root.has("cpu_core_id")) {
            config.cpu_core_id = root["cpu_core_id"].asInt();
        }
        
        if (root.has("use_high_priority")) {
            config.use_high_priority = root["use_high_priority"].asBool();
        }
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "解析配置文件失败: " << e.what() << std::endl;
        std::cerr << "使用默认配置" << std::endl;
        config = ServerConfig();  // 使用默认配置
        file.close();
        return true;  // 返回true，因为我们处理了错误
    }
}

bool ConfigManager::saveServerConfig(const std::string& path, const ServerConfig& config) {
    // 确保目录存在
    fs::path config_path(path);
    fs::path dir_path = config_path.parent_path();
    
    if (!dir_path.empty() && !fs::exists(dir_path)) {
        try {
            fs::create_directories(dir_path);
        } catch (const std::exception& e) {
            std::cerr << "创建配置目录失败: " << e.what() << std::endl;
            return false;
        }
    }
    
    // 创建JSON对象
    json::Value root;
    
    root.set("model_path", json::Value(config.model_path));
    root.set("port", json::Value(static_cast<int>(config.port)));
    root.set("web_port", json::Value(static_cast<int>(config.web_port)));
    root.set("max_clients", json::Value(static_cast<int>(config.max_clients)));
    root.set("target_fps", json::Value(static_cast<int>(config.target_fps)));
    root.set("confidence_threshold", json::Value(config.confidence_threshold));
    root.set("nms_threshold", json::Value(config.nms_threshold));
    root.set("max_queue_size", json::Value(static_cast<int>(config.max_queue_size)));
    root.set("use_cpu_affinity", json::Value(config.use_cpu_affinity));
    root.set("cpu_core_id", json::Value(config.cpu_core_id));
    root.set("use_high_priority", json::Value(config.use_high_priority));
    
    // 添加详细子对象
    json::Value logging;
    logging.setObject();
    logging.set("enable_logging", json::Value(true));
    logging.set("log_level", json::Value("info"));
    logging.set("log_file", json::Value("logs/server.log"));
    logging.set("max_log_size_mb", json::Value(10));
    logging.set("max_log_files", json::Value(5));
    root.set("logging", logging);
    
    json::Value network;
    network.setObject();
    network.set("recv_buffer_size", json::Value(1048576));
    network.set("send_buffer_size", json::Value(1048576));
    network.set("timeout_ms", json::Value(5000));
    network.set("heartbeat_interval_ms", json::Value(1000));
    root.set("network", network);
    
    json::Value detection;
    detection.setObject();
    detection.set("model_width", json::Value(416));
    detection.set("model_height", json::Value(416));
    detection.set("enable_tracking", json::Value(true));
    detection.set("max_tracking_age_ms", json::Value(500));
    
    json::Value class_weights;
    class_weights.setObject();
    class_weights.set("player_t", json::Value(1.0));
    class_weights.set("player_ct", json::Value(1.0));
    class_weights.set("head", json::Value(1.2));
    class_weights.set("weapon", json::Value(0.8));
    detection.set("class_weights", class_weights);
    
    root.set("detection", detection);
    
    // 保存到文件
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "无法打开配置文件进行写入: " << path << std::endl;
        return false;
    }
    
    try {
        file << root.toString();
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "写入配置文件失败: " << e.what() << std::endl;
        file.close();
        return false;
    }
}

bool ConfigManager::loadClientConfig(const std::string& path, ClientConfig& config) {
    // 检查文件是否存在
    if (!fs::exists(path)) {
        std::cerr << "配置文件不存在: " << path << std::endl;
        
        // 创建默认配置
        createDefaultClientConfig(path);
        
        // 返回默认配置
        config = ClientConfig();
        return false;
    }
    
    // 打开并读取文件
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "无法打开配置文件: " << path << std::endl;
        return false;
    }
    
    try {
        // 读取整个文件内容
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string json_content = buffer.str();
        
        // 解析JSON
        json::Value root = json::parse(json_content);
        
        // 提取基本配置
        if (root.has("server_ip")) {
            config.server_ip = root["server_ip"].asString();
        }
        
        if (root.has("server_port")) {
            config.server_port = root["server_port"].asInt();
        }
        
        if (root.has("game_id")) {
            config.game_id = root["game_id"].asInt();
        }
        
        if (root.has("target_fps")) {
            config.target_fps = root["target_fps"].asInt();
        }
        
        if (root.has("screen_width")) {
            config.screen_width = root["screen_width"].asInt();
        }
        
        if (root.has("screen_height")) {
            config.screen_height = root["screen_height"].asInt();
        }
        
        if (root.has("auto_connect")) {
            config.auto_connect = root["auto_connect"].asBool();
        }
        
        if (root.has("auto_start")) {
            config.auto_start = root["auto_start"].asBool();
        }
        
        if (root.has("enable_aim_assist")) {
            config.enable_aim_assist = root["enable_aim_assist"].asBool();
        }
        
        if (root.has("enable_esp")) {
            config.enable_esp = root["enable_esp"].asBool();
        }
        
        if (root.has("enable_recoil_control")) {
            config.enable_recoil_control = root["enable_recoil_control"].asBool();
        }
        
        if (root.has("use_high_priority")) {
            config.use_high_priority = root["use_high_priority"].asBool();
        }
        
        // 读取压缩设置
        if (root.has("compression")) {
            json::Value compression = root["compression"];
            
            if (compression.has("quality")) {
                config.compression.quality = compression["quality"].asInt();
            }
            
            if (compression.has("keyframe_interval")) {
                config.compression.keyframe_interval = compression["keyframe_interval"].asInt();
            }
            
            if (compression.has("use_difference_encoding")) {
                config.compression.use_difference_encoding = compression["use_difference_encoding"].asBool();
            }
            
            if (compression.has("use_roi_encoding")) {
                config.compression.use_roi_encoding = compression["use_roi_encoding"].asBool();
            }
            
            if (compression.has("roi_padding")) {
                config.compression.roi_padding = compression["roi_padding"].asInt();
            }
        }
        
        // 读取预测设置
        if (root.has("prediction")) {
            json::Value prediction = root["prediction"];
            
            if (prediction.has("max_prediction_time")) {
                config.prediction.max_prediction_time = prediction["max_prediction_time"].asDouble();
            }
            
            if (prediction.has("position_uncertainty")) {
                config.prediction.position_uncertainty = prediction["position_uncertainty"].asDouble();
            }
            
            if (prediction.has("velocity_uncertainty")) {
                config.prediction.velocity_uncertainty = prediction["velocity_uncertainty"].asDouble();
            }
            
            if (prediction.has("acceleration_uncertainty")) {
                config.prediction.acceleration_uncertainty = prediction["acceleration_uncertainty"].asDouble();
            }
            
            if (prediction.has("min_confidence_threshold")) {
                config.prediction.min_confidence_threshold = prediction["min_confidence_threshold"].asDouble();
            }
        }
        
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "解析配置文件失败: " << e.what() << std::endl;
        std::cerr << "使用默认配置" << std::endl;
        config = ClientConfig();  // 回退到默认配置
        file.close();
        return true;  // 返回true，因为我们处理了错误
    }
}

bool ConfigManager::saveClientConfig(const std::string& path, const ClientConfig& config) {
    // 确保目录存在
    fs::path config_path(path);
    fs::path dir_path = config_path.parent_path();
    
    if (!dir_path.empty() && !fs::exists(dir_path)) {
        try {
            fs::create_directories(dir_path);
        } catch (const std::exception& e) {
            std::cerr << "创建配置目录失败: " << e.what() << std::endl;
            return false;
        }
    }
    
    // 创建JSON对象
    json::Value root;
    
    // 设置基本参数
    root.set("server_ip", json::Value(config.server_ip));
    root.set("server_port", json::Value(static_cast<int>(config.server_port)));
    root.set("game_id", json::Value(static_cast<int>(config.game_id)));
    root.set("target_fps", json::Value(static_cast<int>(config.target_fps)));
    root.set("screen_width", json::Value(static_cast<int>(config.screen_width)));
    root.set("screen_height", json::Value(static_cast<int>(config.screen_height)));
    root.set("auto_connect", json::Value(config.auto_connect));
    root.set("auto_start", json::Value(config.auto_start));
    root.set("enable_aim_assist", json::Value(config.enable_aim_assist));
    root.set("enable_esp", json::Value(config.enable_esp));
    root.set("enable_recoil_control", json::Value(config.enable_recoil_control));
    root.set("use_high_priority", json::Value(config.use_high_priority));
    
    // 添加热键设置
    json::Value hotkeys;
    hotkeys.setObject();
    hotkeys.set("toggle_esp", json::Value("F2"));
    hotkeys.set("toggle_aim", json::Value("F3"));
    hotkeys.set("toggle_recoil", json::Value("F4"));
    hotkeys.set("toggle_all", json::Value("F6"));
    root.set("hotkeys", hotkeys);
    
    // 添加调试设置
    json::Value debug;
    debug.setObject();
    debug.set("enable_logging", json::Value(true));
    debug.set("log_level", json::Value("info"));
    debug.set("log_file", json::Value("logs/client.log"));
    debug.set("show_fps", json::Value(true));
    debug.set("show_ping", json::Value(true));
    root.set("debug", debug);
    
    // 添加瞄准设置
    json::Value aim_settings;
    aim_settings.setObject();
    aim_settings.set("mode", json::Value("smooth"));
    aim_settings.set("fov", json::Value(100));
    aim_settings.set("smoothness", json::Value(5.0));
    aim_settings.set("target_bone", json::Value("head"));
    aim_settings.set("trigger_key", json::Value("LSHIFT"));
    aim_settings.set("require_trigger_key", json::Value(true));
    aim_settings.set("aim_delay_ms", json::Value(0));
    aim_settings.set("target_switch_delay_ms", json::Value(300));
    aim_settings.set("max_distance", json::Value(0));
    aim_settings.set("respect_smoke", json::Value(true));
    aim_settings.set("respect_flash", json::Value(true));
    root.set("aim_settings", aim_settings);
    
    // 添加ESP设置
    json::Value esp_settings;
    esp_settings.setObject();
    esp_settings.set("box_thickness", json::Value(2));
    esp_settings.set("box_opacity", json::Value(160));
    esp_settings.set("text_size", json::Value(14));
    esp_settings.set("show_distance", json::Value(true));
    esp_settings.set("show_health", json::Value(false));
    esp_settings.set("show_name", json::Value(false));
    esp_settings.set("max_distance", json::Value(0));
    
    json::Value colors;
    colors.setObject();
    colors.set("t", json::Value("FF0000FF"));
    colors.set("ct", json::Value("0000FFFF"));
    colors.set("head", json::Value("00FF00FF"));
    colors.set("text", json::Value("FFFFFFFF"));
    colors.set("distance", json::Value("FFFF00FF"));
    esp_settings.set("colors", colors);
    
    root.set("esp_settings", esp_settings);
    
    // 压缩设置
    json::Value compression;
    compression.setObject();
    compression.set("quality", json::Value(static_cast<int>(config.compression.quality)));
    compression.set("keyframe_interval", json::Value(static_cast<int>(config.compression.keyframe_interval)));
    compression.set("use_difference_encoding", json::Value(config.compression.use_difference_encoding));
    compression.set("use_roi_encoding", json::Value(config.compression.use_roi_encoding));
    compression.set("roi_padding", json::Value(static_cast<int>(config.compression.roi_padding)));
    root.set("compression", compression);
    
    // 预测设置
    json::Value prediction;
    prediction.setObject();
    prediction.set("max_prediction_time", json::Value(config.prediction.max_prediction_time));
    prediction.set("position_uncertainty", json::Value(config.prediction.position_uncertainty));
    prediction.set("velocity_uncertainty", json::Value(config.prediction.velocity_uncertainty));
    prediction.set("acceleration_uncertainty", json::Value(config.prediction.acceleration_uncertainty));
    prediction.set("min_confidence_threshold", json::Value(config.prediction.min_confidence_threshold));
    root.set("prediction", prediction);
    
    // 添加网络设置
    json::Value network_settings;
    network_settings.setObject();
    network_settings.set("timeout_ms", json::Value(5000));
    network_settings.set("heartbeat_interval_ms", json::Value(1000));
    network_settings.set("reconnect_attempts", json::Value(3));
    network_settings.set("reconnect_delay_ms", json::Value(2000));
    network_settings.set("packet_compression", json::Value(true));
    root.set("network_settings", network_settings);
    
    // 保存到文件
    std::ofstream file(path);
    if (!file.is_open()) {
        std::cerr << "无法打开配置文件进行写入: " << path << std::endl;
        return false;
    }
    
    try {
        file << root.toString();
        file.close();
        return true;
    } catch (const std::exception& e) {
        std::cerr << "写入配置文件失败: " << e.what() << std::endl;
        file.close();
        return false;
    }
}

void ConfigManager::createDefaultServerConfig(const std::string& path) {
    // 创建默认配置
    ServerConfig config;
    
    // 保存默认配置
    saveServerConfig(path, config);
}

void ConfigManager::createDefaultClientConfig(const std::string& path) {
    // 创建默认配置
    ClientConfig config;
    
    // 保存默认配置
    saveClientConfig(path, config);
}

std::string ConfigManager::exportConfigToJson(const ClientConfig& config) {
    // 创建JSON对象
    json::Value root;
    
    // 设置基本参数
    root.set("server_ip", json::Value(config.server_ip));
    root.set("server_port", json::Value(static_cast<int>(config.server_port)));
    root.set("game_id", json::Value(static_cast<int>(config.game_id)));
    root.set("target_fps", json::Value(static_cast<int>(config.target_fps)));
    root.set("screen_width", json::Value(static_cast<int>(config.screen_width)));
    root.set("screen_height", json::Value(static_cast<int>(config.screen_height)));
    root.set("auto_connect", json::Value(config.auto_connect));
    root.set("auto_start", json::Value(config.auto_start));
    root.set("enable_aim_assist", json::Value(config.enable_aim_assist));
    root.set("enable_esp", json::Value(config.enable_esp));
    root.set("enable_recoil_control", json::Value(config.enable_recoil_control));
    root.set("use_high_priority", json::Value(config.use_high_priority));
    
    // 压缩设置
    json::Value compression;
    compression.setObject();
    compression.set("quality", json::Value(static_cast<int>(config.compression.quality)));
    compression.set("keyframe_interval", json::Value(static_cast<int>(config.compression.keyframe_interval)));
    compression.set("use_difference_encoding", json::Value(config.compression.use_difference_encoding));
    compression.set("use_roi_encoding", json::Value(config.compression.use_roi_encoding));
    compression.set("roi_padding", json::Value(static_cast<int>(config.compression.roi_padding)));
    root.set("compression", compression);
    
    // 预测设置
    json::Value prediction;
    prediction.setObject();
    prediction.set("max_prediction_time", json::Value(config.prediction.max_prediction_time));
    prediction.set("position_uncertainty", json::Value(config.prediction.position_uncertainty));
    prediction.set("velocity_uncertainty", json::Value(config.prediction.velocity_uncertainty));
    prediction.set("acceleration_uncertainty", json::Value(config.prediction.acceleration_uncertainty));
    prediction.set("min_confidence_threshold", json::Value(config.prediction.min_confidence_threshold));
    root.set("prediction", prediction);
    
    return root.toString();
}

bool ConfigManager::importConfigFromJson(const std::string& json_str, ClientConfig& config) {
    try {
        // 使用默认值初始化配置
        config = ClientConfig();
        
        // 解析JSON
        json::Value root = json::parse(json_str);
        
        // 提取基本配置
        if (root.has("server_ip")) {
            config.server_ip = root["server_ip"].asString();
        }
        
        if (root.has("server_port")) {
            config.server_port = root["server_port"].asInt();
        }
        
        if (root.has("game_id")) {
            config.game_id = root["game_id"].asInt();
        }
        
        if (root.has("target_fps")) {
            config.target_fps = root["target_fps"].asInt();
        }
        
        if (root.has("screen_width")) {
            config.screen_width = root["screen_width"].asInt();
        }
        
        if (root.has("screen_height")) {
            config.screen_height = root["screen_height"].asInt();
        }
        
        if (root.has("auto_connect")) {
            config.auto_connect = root["auto_connect"].asBool();
        }
        
        if (root.has("auto_start")) {
            config.auto_start = root["auto_start"].asBool();
        }
        
        if (root.has("enable_aim_assist")) {
            config.enable_aim_assist = root["enable_aim_assist"].asBool();
        }
        
        if (root.has("enable_esp")) {
            config.enable_esp = root["enable_esp"].asBool();
        }
        
        if (root.has("enable_recoil_control")) {
            config.enable_recoil_control = root["enable_recoil_control"].asBool();
        }
        
        if (root.has("use_high_priority")) {
            config.use_high_priority = root["use_high_priority"].asBool();
        }
        
        // 读取压缩设置
        if (root.has("compression")) {
            json::Value compression = root["compression"];
            
            if (compression.has("quality")) {
                config.compression.quality = compression["quality"].asInt();
            }
            
            if (compression.has("keyframe_interval")) {
                config.compression.keyframe_interval = compression["keyframe_interval"].asInt();
            }
            
            if (compression.has("use_difference_encoding")) {
                config.compression.use_difference_encoding = compression["use_difference_encoding"].asBool();
            }
            
            if (compression.has("use_roi_encoding")) {
                config.compression.use_roi_encoding = compression["use_roi_encoding"].asBool();
            }
            
            if (compression.has("roi_padding")) {
                config.compression.roi_padding = compression["roi_padding"].asInt();
            }
        }
        
        // 读取预测设置
        if (root.has("prediction")) {
            json::Value prediction = root["prediction"];
            
            if (prediction.has("max_prediction_time")) {
                config.prediction.max_prediction_time = prediction["max_prediction_time"].asDouble();
            }
            
            if (prediction.has("position_uncertainty")) {
                config.prediction.position_uncertainty = prediction["position_uncertainty"].asDouble();
            }
            
            if (prediction.has("velocity_uncertainty")) {
                config.prediction.velocity_uncertainty = prediction["velocity_uncertainty"].asDouble();
            }
            
            if (prediction.has("acceleration_uncertainty")) {
                config.prediction.acceleration_uncertainty = prediction["acceleration_uncertainty"].asDouble();
            }
            
            if (prediction.has("min_confidence_threshold")) {
                config.prediction.min_confidence_threshold = prediction["min_confidence_threshold"].asDouble();
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        std::cerr << "解析JSON失败: " << e.what() << std::endl;
        return false;
    }
}

} // namespace zero_latency