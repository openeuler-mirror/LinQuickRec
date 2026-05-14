#ifndef ETCD_HTTP_H
#define ETCD_HTTP_H

#include <string>

namespace etcd_http {

std::string post(const std::string& host_str,
                 int port,
                 const std::string& path,
                 const std::string& body);

} // namespace etcd_http

#endif // ETCD_HTTP_H
