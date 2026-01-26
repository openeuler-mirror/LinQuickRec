//
// Created by lixu on 2026/1/26.
//

#ifndef LINQUICKREC_KR_USER_FEATURE_REDIS_H
#define LINQUICKREC_KR_USER_FEATURE_REDIS_H

#include <hiredis/hiredis.h>
#include <string>
#include <unordered_map>
#include <vector>
#include <stdexcept>

class UserFeatureRedis {
public:
    explicit UserFeatureRedis(const std::string& host = "127.0.0.1",
                              int port = 6379,
                              int db = 0,
                              const std::string& key_prefix = "uf");
    ~UserFeatureRedis();
    UserFeatureRedis(const UserFeatureRedis&) = delete;
    UserFeatureRedis& operator=(const UserFeatureRedis&) = delete;
    std::unordered_map<std::string, std::string> get(int64_t user_id);
    std::string get_field(int64_t user_id, const std::string& field);
    bool exists(int64_t user_id);
    std::vector<std::unordered_map<std::string, std::string>>
    get_many(const std::vector<int64_t>& user_ids);

private:
    redisContext* ctx_ = nullptr;
    std::string prefix_;

    [[nodiscard]] std::string build_key(int64_t user_id) const;
    void check_reply(const redisReply* reply);
};

#endif //LINQUICKREC_KR_USER_FEATURE_REDIS_H
