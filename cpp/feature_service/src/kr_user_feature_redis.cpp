#include "kr_user_feature_redis.h"
#include <cstdio>

inline std::string UserFeatureRedis::build_key(int64_t user_id) const {
    return prefix_ + ":" + std::to_string(user_id);
}

void UserFeatureRedis::check_reply(const redisReply* reply) {
    if (reply == nullptr)
        throw std::runtime_error("Redis reply is null");
    if (reply->type == REDIS_REPLY_ERROR)
        throw std::runtime_error(std::string("Redis error: ") + reply->str);
}

UserFeatureRedis::UserFeatureRedis(const std::string& host,
                                   int port,
                                   int db,
                                   const std::string& key_prefix)
        : prefix_(key_prefix) {
    struct timeval timeout = {1, 500000}; // 1.5s
    ctx_ = redisConnectWithTimeout(host.c_str(), port, timeout);
    if (ctx_ == nullptr || ctx_->err) {
        if (ctx_) {
            std::string err = "Redis connect: " + std::string(ctx_->errstr);
            redisFree(ctx_);
            throw std::runtime_error(err);
        } else {
            throw std::runtime_error("Redis connect: can't allocate context");
        }
    }
    // select db
    auto* r = static_cast<redisReply*>(redisCommand(ctx_, "SELECT %d", db));
    check_reply(r);
    freeReplyObject(r);
}

UserFeatureRedis::~UserFeatureRedis() {
    if (ctx_) redisFree(ctx_);
}

std::unordered_map<std::string, std::string>
UserFeatureRedis::get(int64_t user_id) {
    std::unordered_map<std::string, std::string> ret;
    redisReply* r = static_cast<redisReply*>(
            redisCommand(ctx_, "HGETALL %s", build_key(user_id).c_str()));
    check_reply(r);
    if (r->type == REDIS_REPLY_ARRAY && r->elements % 2 == 0) {
        for (size_t i = 0; i < r->elements; i += 2) {
            std::string field(r->element[i]->str, r->element[i]->len);
            std::string value(r->element[i + 1]->str, r->element[i + 1]->len);
            ret.emplace(std::move(field), std::move(value));
        }
    }
    freeReplyObject(r);
    return ret;
}

std::string UserFeatureRedis::get_field(int64_t user_id,
                                        const std::string& field) {
    redisReply* r = static_cast<redisReply*>(
            redisCommand(ctx_, "HGET %s %s", build_key(user_id).c_str(), field.c_str()));
    check_reply(r);
    std::string val;
    if (r->type == REDIS_REPLY_STRING) {
        val.assign(r->str, r->len);
    }
    freeReplyObject(r);
    return val;
}

bool UserFeatureRedis::exists(int64_t user_id) {
    redisReply* r = static_cast<redisReply*>(
            redisCommand(ctx_, "EXISTS %s", build_key(user_id).c_str()));
    check_reply(r);
    bool ret = (r->integer == 1);
    freeReplyObject(r);
    return ret;
}

std::vector<std::unordered_map<std::string, std::string>>
UserFeatureRedis::get_many(const std::vector<int64_t>& user_ids) {
    std::vector<std::unordered_map<std::string, std::string>> ans;
    ans.reserve(user_ids.size());
    // pipeline
    for (int64_t uid : user_ids) {
        redisAppendCommand(ctx_, "HGETALL %s", build_key(uid).c_str());
    }
    // 批量取回
    for (size_t i = 0; i < user_ids.size(); ++i) {
        redisReply* r;
        if (redisGetReply(ctx_, (void**)&r) != REDIS_OK)
            throw std::runtime_error("pipeline error");
        std::unordered_map<std::string, std::string> m;
        if (r->type == REDIS_REPLY_ARRAY && r->elements % 2 == 0) {
            for (size_t j = 0; j < r->elements; j += 2) {
                std::string field(r->element[j]->str, r->element[j]->len);
                std::string value(r->element[j + 1]->str, r->element[j + 1]->len);
                m.emplace(std::move(field), std::move(value));
            }
        }
        ans.emplace_back(std::move(m));
        freeReplyObject(r);
    }
    return ans;
}