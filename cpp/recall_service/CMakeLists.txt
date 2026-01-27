file(GLOB_RECURSE SOURCES src/*.cpp src/*.cc)

add_executable(recall_service
        recall_service_main.cpp
        ${SOURCES}
)

target_include_directories(recall_service PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${PROTO_GEN_DIR}
        ${LinQuickRec_INCLUDES}
)

target_link_libraries(feature_service PRIVATE
        ${PROTO_LIB}
        ${BRPC_LIB}
        ${LinQuickRec_LIBS}
)