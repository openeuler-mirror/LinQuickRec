#ifndef LINQUICKREC_KR_USER_LOG_REDIS_H
#define LINQUICKREC_KR_USER_LOG_REDIS_H

#include <hiredis/hiredis.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>
#include <nlohmann/json.hpp>

using json = nlohmann::json;


class UserLogRedis {
public:
    UserLogRedis(const std::string& host = "127.0.0.1",
                 int port = 6379,
                 int db = 0,
                 const std::string& key_prefix = "ul");
    ~UserLogRedis();

    long long append(int64_t user_id, const json& log);
    long long extend(int64_t user_id, const std::vector<json>& logs);
    std::vector<json> get_slice(int64_t user_id, long long start = 0, long long end = -1);
    std::vector<json> get_all(int64_t user_id);
    long long len(int64_t user_id);
    json pop_left(int64_t user_id);
    json pop_right(int64_t user_id);
    bool clear(int64_t user_id);
private:
    redisContext* ctx_ = nullptr;
    std::string prefix_;

    [[nodiscard]] std::string build_key(int64_t user_id) const;
    void check_reply(const redisReply* reply);
};

#endif //LINQUICKREC_KR_USER_LOG_REDIS_H
