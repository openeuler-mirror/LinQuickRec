file(GLOB PROTO_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.proto")

protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS ${PROTO_FILES})

# 创建 protobuf 静态库
add_library(proto_lib STATIC ${PROTO_SRCS})

target_include_directories(proto_lib PUBLIC
        ${CMAKE_CURRENT_BINARY_DIR}  # 生成的 .pb.h 文件位置
        ${Protobuf_INCLUDE_DIRS}
)

# 只链接 protobuf，Abseil 依赖会通过 protobuf::libprotobuf 自动传递
target_link_libraries(proto_lib PUBLIC
        protobuf::libprotobuf
)