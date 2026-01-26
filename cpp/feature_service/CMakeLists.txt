file(GLOB_RECURSE SOURCES src/*.cpp src/*.cc)

add_executable(feature_service
        feature_service.cpp
        ${SOURCES}
)

target_include_directories(feature_service PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/cpp/common/include
        ${PROTO_GEN_DIR}
        ${HIREDIS_INCLUDE_DIR}  # feature_service 特有
        ${LinQuickRec_INCLUDES}
)

target_link_libraries(feature_service PRIVATE
        common
        ${PROTO_LIB}
        ${BRPC_LIB}
        ${LinQuickRec_LIBS}
        ${HIREDIS_LIB}          # feature_service 特有
)