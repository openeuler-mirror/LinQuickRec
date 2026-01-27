# LinQuickRec

An end-to-end reference implementation of the UB-based recommendation system.


## Requirements installation

### Download source codes

```bash
#OpenSSL_1_1_1m
git clone https://github.com/openssl/openssl.git
cd openssl
git checkout -b tab_OpenSSL_1_1_1m OpenSSL_1_1_1m
git submodule update --init --recursive

#gflags v2.2.2
git clone https://github.com/gflags/gflags.git
cd gflags
git checkout -b tab_v2.2.2 v2.2.2
git submodule update --init --recursive

#leveldb 1.23
git clone https://github.com/google/leveldb.git
cd leveldb
git checkout -b tab_1.23 1.23
git submodule update --init --recursive

#protobuf 5.28.3
git clone https://github.com/protocolbuffers/protobuf.git
cd protobuf
git checkout -b tab_v5.28.3 v5.28.3
git submodule update --init --recursive

#brpc 1.15.0
git clone https://github.com/apache/brpc.git
cd brpc
git checkcout -b tab_1.15.0 1.15.0
git submodule update --init --recursive
```

### Compile source codes
```bash
#openssl
cd openssl
./config
make -j32
make install

#gflags
cd gflags
mkdir build && cd build
cmake -DBUILD_SHARED_LIBS=ON _DBUILD_STATIC_LIBS=ON -DINSTALL_HEADERS=ON -DINSTALL_SHARED_LIBS=ON -DINSTALL_STATIC_LIBS=ON ..
make -j32
make install

#leveldb
cd leveldb
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release -DLEVELDB_BUILD_TESTS=OFF -DLEVELDB_BUILD_BENCHMARKS=OFF ..
make -j32
make install

#protobuf
cd protobuf
mkdir build && cd build
cmake -DBUILD_SHARED_LIBS=ON -Dprotobuf_BUILD_TESTS=OFF -DCMAKE_CXX_STANDARD=17 -DCMAKE_INSTALL_PREFIX=/usr/local -DABSL_PROPAGATE_CXX_STD=ON
make -j32
make install -j32

#brpc
cd brpc
make build && cd build
cmake -DWITH_RDMA=ON ..
cmake ..
make -j32


```