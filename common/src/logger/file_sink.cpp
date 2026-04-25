#include "common/internal/logger/sink/file_sink.h"
#include <filesystem>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <vector>
#include <algorithm>

namespace common {
namespace logger {

namespace fs = std::filesystem;

FileSink::FileSink(const std::string& file_path, 
                   LogLevel level,
                   size_t max_file_size,
                   int max_files)
    : file_path_(file_path),
      level_(level),
      max_file_size_(max_file_size),
      max_files_(max_files),
      current_file_size_(0) {
    OpenFile();
}

FileSink::~FileSink() {
    if (file_.is_open()) {
        file_.close();
    }
}

void FileSink::OpenFile() {
    // 确保目录存在
    fs::path path(file_path_);
    if (path.has_parent_path()) {
        fs::create_directories(path.parent_path());
    }
    
    // 以追加模式打开文件
    file_.open(file_path_, std::ios::app | std::ios::out);
    if (!file_.is_open()) {
        throw std::runtime_error("Failed to open log file: " + file_path_);
    }
    
    // 获取当前文件大小
    try {
        current_file_size_ = fs::file_size(file_path_);
    } catch (...) {
        current_file_size_ = 0;
    }
}

bool FileSink::ShouldRotate() {
    return current_file_size_ >= max_file_size_;
}

void FileSink::RotateFile() {
    if (file_.is_open()) {
        file_.close();
    }
    
    // 生成带时间戳的新文件名
    auto now = std::chrono::system_clock::now();
    auto time_t = std::chrono::system_clock::to_time_t(now);
    std::tm tm;
#ifdef _WIN32
    localtime_s(&tm, &time_t);
#else
    localtime_r(&time_t, &tm);
#endif
    
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y%m%d_%H%M%S");
    std::string timestamp = oss.str();
    
    fs::path original_path(file_path_);
    fs::path rotated_path = original_path.parent_path() / 
                           (original_path.stem().string() + 
                            "_" + timestamp + 
                            original_path.extension().string());
    
    // 重命名当前文件
    try {
        fs::rename(file_path_, rotated_path);
    } catch (const fs::filesystem_error& e) {
        // 如果重命名失败，尝试复制
        fs::copy_file(file_path_, rotated_path, fs::copy_options::overwrite_existing);
        fs::remove(file_path_);
    }
    
    // 清理旧文件
    CleanOldFiles();
    
    // 重新打开文件
    OpenFile();
}

void FileSink::CleanOldFiles() {
    try {
        fs::path log_dir = fs::path(file_path_).parent_path();
        std::string log_filename = fs::path(file_path_).stem().string();
        std::string log_ext = fs::path(file_path_).extension().string();
        
        std::vector<fs::path> log_files;
        for (const auto& entry : fs::directory_iterator(log_dir)) {
            if (entry.is_regular_file()) {
                std::string filename = entry.path().filename().string();
                if (filename.find(log_filename) == 0 && 
                    filename.find(log_ext) != std::string::npos) {
                    log_files.push_back(entry.path());
                }
            }
        }
        
        // 按修改时间排序
        std::sort(log_files.begin(), log_files.end(),
                  [](const fs::path& a, const fs::path& b) {
                      return fs::last_write_time(a) < fs::last_write_time(b);
                  });
        
        // 删除最旧的文件
        while (log_files.size() > static_cast<size_t>(max_files_)) {
            fs::remove(log_files.front());
            log_files.erase(log_files.begin());
        }
    } catch (...) {
        // 忽略清理错误
    }
}

void FileSink::Write(const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (ShouldRotate()) {
        RotateFile();
    }
    
    if (file_.is_open()) {
        file_ << message << std::endl;
        current_file_size_ += message.size() + 1; // +1 for newline
    }
}

void FileSink::Flush() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) {
        file_.flush();
    }
}

void FileSink::SetLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel FileSink::GetLevel() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

size_t FileSink::GetCurrentFileSize() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_file_size_;
}

const std::string& FileSink::GetFilePath() const {
    return file_path_;
}

} // namespace logger
} // namespace common