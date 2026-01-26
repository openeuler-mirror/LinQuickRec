#include "kr_user_log_redis.h"
#include <stdexcept>

inline std::string UserLogRedis::build_key(int64_t user_id) const {
    return prefix_ + ":" + std::to_string(user_id);
}

void UserLogRedis::check_reply(const redisReply* reply) {
    if (!reply) throw std::runtime_error("Redis reply null");
    if (reply->type == REDIS_REPLY_ERROR)
        throw std::runtime_error(std::string("Redis error: ") + reply->str);
}

UserLogRedis::UserLogRedis(const std::string& host, int port, int db, const std::string& key_prefix)
    : prefix_(key_prefix) {
    struct timeval timeout = {1, 500000};
    ctx_ = redisConnectWithTimeout(host.c_str(), port, timeout);
    if (!ctx_ || ctx_->err) {
        if (ctx_) {
            std::string err = "Connect: " + std::string(ctx_->errstr);
            redisFree(ctx_);
            throw std::runtime_error(err);
        } else {
            throw std::runtime_error("Connect: can't allocate ctx");
        }
    }
    auto* r = static_cast<redisReply*>(redisCommand(ctx_, "SELECT %d", db));
    check_reply(r);
    freeReplyObject(r);
}

UserLogRedis::~UserLogRedis() {
    if (ctx_) redisFree(ctx_);
}

long long UserLogRedis::append(int64_t user_id, const json& log) {
    std::string item = log.dump();
    redisReply* r = static_cast<redisReply*>(
        redisCommand(ctx_, "RPUSH %s %s", build_key(user_id).c_str(), item.c_str()));
    check_reply(r);
    long long ret = r->integer;
    freeReplyObject(r);
    return ret;
}

long long UserLogRedis::extend(int64_t user_id, const std::vector<json>& logs) {
    if (logs.empty()) return 0;
    std::vector<const char*> argv;
    std::vector<size_t> argvlen;
    argv.reserve(logs.size() + 2);
    argvlen.reserve(logs.size() + 2);
    std::string cmd = "RPUSH";
    std::string key = build_key(user_id);
    argv.emplace_back(cmd.c_str()); argvlen.emplace_back(cmd.size());
    argv.emplace_back(key.c_str()); argvlen.emplace_back(key.size());
    std::vector<std::string> items;
    items.reserve(logs.size());
    for (auto& j : logs) {
        items.emplace_back(j.dump());
        argv.emplace_back(items.back().c_str());
        argvlen.emplace_back(items.back().size());
    }
    redisReply* r = static_cast<redisReply*>(
        redisCommandArgv(ctx_, static_cast<int>(argv.size()), argv.data(), argvlen.data()));
    check_reply(r);
    long long ret = r->integer;
    freeReplyObject(r);
    return ret;
}

std::vector<json> UserLogRedis::get_slice(int64_t user_id, long long start, long long end) {
    std::vector<json> ans;
    redisReply* r = static_cast<redisReply*>(
        redisCommand(ctx_, "LRANGE %s %lld %lld", build_key(user_id).c_str(), start, end));
    check_reply(r);
    if (r->type == REDIS_REPLY_ARRAY) {
        ans.reserve(r->elements);
        for (size_t i = 0; i < r->elements; ++i)
            ans.emplace_back(json::parse(std::string(r->element[i]->str, r->element[i]->len)));
    }
    freeReplyObject(r);
    return ans;
}

std::vector<json> UserLogRedis::get_all(int64_t user_id) {
    return get_slice(user_id, 0, -1);
}

long long UserLogRedis::len(int64_t user_id) {
    redisReply* r = static_cast<redisReply*>(redisCommand(ctx_, "LLEN %s", build_key(user_id).c_str()));
    check_reply(r);
    long long ret = r->integer;
    freeReplyObject(r);
    return ret;
}

json UserLogRedis::pop_left(int64_t user_id) {
    redisReply* r = static_cast<redisReply*>(redisCommand(ctx_, "LPOP %s", build_key(user_id).c_str()));
    if (!r || r->type == REDIS_REPLY_NIL) {
        if (r) freeReplyObject(r);
        return nullptr;
    }
    json j = json::parse(std::string(r->str, r->len));
    freeReplyObject(r);
    return j;
}

json UserLogRedis::pop_right(int64_t user_id) {
    redisReply* r = static_cast<redisReply*>(redisCommand(ctx_, "RPOP %s", build_key(user_id).c_str()));
    if (!r || r->type == REDIS_REPLY_NIL) {
        if (r) freeReplyObject(r);
        return nullptr;
    }
    json j = json::parse(std::string(r->str, r->len));
    freeReplyObject(r);
    return j;
}

bool UserLogRedis::clear(int64_t user_id) {
    redisReply* r = static_cast<redisReply*>(redisCommand(ctx_, "DEL %s", build_key(user_id).c_str()));
    check_reply(r);
    bool ret = (r->integer > 0);
    freeReplyObject(r);
    return ret;
}