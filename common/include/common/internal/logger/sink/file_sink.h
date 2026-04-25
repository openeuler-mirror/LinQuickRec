#ifndef COMMON_LOGGER_SINK_FILE_SINK_H
#define COMMON_LOGGER_SINK_FILE_SINK_H

#include "sink.h"
#include "../config.h"
#include <fstream>
#include <mutex>
#include <string>

namespace common {
namespace logger {

/**
 * @brief 文件日志输出器，支持文件轮转
 */
class FileSink : public LogSink {
public:
    /**
     * @brief 构造函数
     * @param file_path 日志文件路径
     * @param level 日志级别过滤器
     * @param max_file_size 最大文件大小（字节）
     * @param max_files 最大文件数量
     */
    FileSink(const std::string& file_path, 
             LogLevel level = LogLevel::INFO,
             size_t max_file_size = 100 * 1024 * 1024,
             int max_files = 10);
    
    ~FileSink();
    
    void Write(const std::string& message) override;
    void Flush() override;
    void SetLevel(LogLevel level) override;
    LogLevel GetLevel() const override;
    
    /**
     * @brief 获取当前日志文件大小
     */
    size_t GetCurrentFileSize() const;
    
    /**
     * @brief 获取日志文件路径
     */
    const std::string& GetFilePath() const;
    
private:
    /**
     * @brief 打开日志文件
     */
    void OpenFile();
    
    /**
     * @brief 检查是否需要轮转文件
     */
    bool ShouldRotate();
    
    /**
     * @brief 执行文件轮转
     */
    void RotateFile();
    
    /**
     * @brief 删除旧日志文件
     */
    void CleanOldFiles();
    
private:
    std::string file_path_;
    LogLevel level_;
    size_t max_file_size_;
    int max_files_;
    
    mutable std::mutex mutex_;
    std::ofstream file_;
    size_t current_file_size_;
};

} // namespace logger
} // namespace common

#endif // COMMON_LOGGER_SINK_FILE_SINK_H