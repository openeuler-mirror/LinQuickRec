#ifndef ETCD_CLIENT_H
#define ETCD_CLIENT_H

#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace discovery {

class EtcdClient {
public:
    explicit EtcdClient(const std::string& endpoints);
    ~EtcdClient();

    EtcdClient(const EtcdClient&) = delete;
    EtcdClient& operator=(const EtcdClient&) = delete;

    int64_t leaseGrant(int64_t ttl_seconds);
    bool leaseRevoke(int64_t lease_id);

    bool put(const std::string& key, const std::string& value,
             int64_t lease_id = 0);
    bool deleteKey(const std::string& key);
    std::vector<std::pair<std::string, std::string>> range(
        const std::string& prefix);

    std::string post(const std::string& path, const std::string& json_body);

private:
    std::string base64Encode(const std::string& input);
    std::string base64Decode(const std::string& input);
    std::string prefixEnd(const std::string& prefix);

    std::vector<std::string> endpoints_;
    size_t current_endpoint_ = 0;
};

} // namespace discovery

#endif // ETCD_CLIENT_H
