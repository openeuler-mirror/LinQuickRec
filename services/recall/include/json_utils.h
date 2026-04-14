#ifndef JSON_UTILS_H
#define JSON_UTILS_H

#include <string>
#include <sstream>
#include <stdexcept>
#include <cctype>

/**
 * @brief JSON 工具类
 * 
 * 提供基础的 JSON 解析和构建功能
 * 注意：这是一个简化实现，生产环境建议使用 rapidjson 或 simdjson
 */
class JsonUtils {
public:
    /**
     * @brief 转义字符串为合法的 JSON 字符串
     * 
     * @param input 原始字符串
     * @return std::string 转义后的 JSON 字符串
     */
    static std::string escape_string(const std::string& input) {
        std::ostringstream oss;
        for (char c : input) {
            unsigned char uc = static_cast<unsigned char>(c);
            switch (c) {
                case '\\': oss << "\\\\"; break;
                case '"': oss << "\\\""; break;
                case '\b': oss << "\\b"; break;
                case '\f': oss << "\\f"; break;
                case '\n': oss << "\\n"; break;
                case '\r': oss << "\\r"; break;
                case '\t': oss << "\\t"; break;
                default:
                    if (uc < 0x20) {
                        oss << "\\u" << std::hex << std::setw(4) 
                            << std::setfill('0') << static_cast<int>(uc);
                    } else {
                        oss << c;
                    }
            }
        }
        return oss.str();
    }

    /**
     * @brief 从 JSON 对象中提取字符串值
     * 
     * @param json JSON 字符串
     * @param key 要提取的键
     * @param value 输出参数，存储提取的值
     * @return true 提取成功
     * @return false 提取失败（键不存在或格式错误）
     */
    static bool extract_string(const std::string& json, 
                               const std::string& key, 
                               std::string& value) {
        std::string search_key = "\"" + key + "\"";
        size_t pos = json.find(search_key);
        if (pos == std::string::npos) {
            return false;
        }
        
        // 查找冒号
        pos = json.find(':', pos);
        if (pos == std::string::npos) {
            return false;
        }

        // 查找第一个双引号
        pos = json.find('"', pos + 1);
        if (pos == std::string::npos) {
            return false;
        }
        
        // 提取字符串内容
        size_t start = pos + 1;
        size_t end = start;
        while (end < json.size()) {
            // 检查是否是结束引号（且不是转义的）
            if (json[end] == '"' && (end == 0 || json[end - 1] != '\\')) {
                break;
            }
            end++;
        }
        
        if (end <= start) {
            return false;
        }
        
        value = json.substr(start, end - start);
        
        // 反转义
        std::string unescaped;
        if (unescape_string(value, unescaped)) {
            value = unescaped;
        }
        
        return true;
    }

    /**
     * @brief 从 JSON 对象中提取整数值
     * 
     * @param json JSON 字符串
     * @param key 要提取的键
     * @param value 输出参数，存储提取的值
     * @return true 提取成功
     * @return false 提取失败（键不存在或格式错误）
     */
    static bool extract_int(const std::string& json, 
                           const std::string& key, 
                           int& value) {
        std::string search_key = "\"" + key + "\"";
        size_t pos = json.find(search_key);
        if (pos == std::string::npos) {
            return false;
        }
        
        // 查找冒号
        pos = json.find(':', pos);
        if (pos == std::string::npos) {
            return false;
        }
        
        // 跳过空白字符
        pos++;
        while (pos < json.size() && std::isspace(json[pos])) {
            pos++;
        }
        
        // 查找数字结束位置
        size_t end = pos;
        while (end < json.size() && 
               (std::isdigit(json[end]) || json[end] == '-' || json[end] == '+')) {
            end++;
        }
        
        if (pos == end) {
            return false;
        }
        
        try {
            value = std::stoi(json.substr(pos, end - pos));
            return true;
        } catch (const std::exception&) {
            return false;
        }
    }

    /**
     * @brief 构建 JSON 字符串（简单的键值对）
     * 
     * @param key_value_pairs 键值对列表
     * @return std::string 构建的 JSON 字符串
     */
    static std::string build_object(
        const std::vector<std::pair<std::string, std::string>>& key_value_pairs) {
        
        std::ostringstream oss;
        oss << "{";
        
        bool first = true;
        for (const auto& pair : key_value_pairs) {
            if (!first) {
                oss << ",";
            }
            first = false;
            
            oss << "\"" << escape_string(pair.first) << "\":\"" 
                << escape_string(pair.second) << "\"";
        }
        
        oss << "}";
        return oss.str();
    }

private:
    /**
     * @brief 反转义 JSON 字符串
     * 
     * @param escaped 转义后的字符串
     * @param unescaped 输出参数，存储反转义后的字符串
     * @return true 反转义成功
     * @return false 反转义失败
     */
    static bool unescape_string(const std::string& escaped, 
                               std::string& unescaped) {
        unescaped.clear();
        unescaped.reserve(escaped.size());
        
        for (size_t i = 0; i < escaped.size(); ++i) {
            if (escaped[i] == '\\' && i + 1 < escaped.size()) {
                i++;
                switch (escaped[i]) {
                    case '\\': unescaped += '\\'; break;
                    case '"': unescaped += '"'; break;
                    case 'b': unescaped += '\b'; break;
                    case 'f': unescaped += '\f'; break;
                    case 'n': unescaped += '\n'; break;
                    case 'r': unescaped += '\r'; break;
                    case 't': unescaped += '\t'; break;
                    case 'u':
                        // 简单的 unicode 转义处理（仅支持基本平面）
                        if (i + 4 < escaped.size()) {
                            std::string hex = escaped.substr(i + 1, 4);
                            try {
                                int code = std::stoi(hex, nullptr, 16);
                                if (code < 0x80) {
                                    unescaped += static_cast<char>(code);
                                } else if (code < 0x800) {
                                    unescaped += static_cast<char>(0xC0 | (code >> 6));
                                    unescaped += static_cast<char>(0x80 | (code & 0x3F));
                                } else {
                                    unescaped += static_cast<char>(0xE0 | (code >> 12));
                                    unescaped += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
                                    unescaped += static_cast<char>(0x80 | (code & 0x3F));
                                }
                                i += 4;
                            } catch (...) {
                                unescaped += "\\u";
                                unescaped += hex;
                            }
                        } else {
                            unescaped += "\\u";
                        }
                        break;
                    default:
                        unescaped += '\\';
                        unescaped += escaped[i];
                        break;
                }
            } else {
                unescaped += escaped[i];
            }
        }
        
        return true;
    }
};

#endif // JSON_UTILS_H
