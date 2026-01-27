function(add_service_executable SERVICE_NAME)
    set(SERVICE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/${SERVICE_NAME})

    file(GLOB_RECURSE MODULE_SOURCES
            ${SERVICE_DIR}/src/*.cpp
            ${SERVICE_DIR}/src/*.cc
    )

    add_executable(${SERVICE_NAME}
            ${SERVICE_DIR}/${SERVICE_NAME}_main.cpp
            ${MODULE_SOURCES}
    )

    target_include_directories(${SERVICE_NAME} PRIVATE
            ${SERVICE_DIR}/include
            ${PROTO_GEN_DIR}
    )

    target_link_libraries(${SERVICE_NAME} PRIVATE
            proto_lib
            ${BRPC_LIB}
            linquickrec_deps
    )

    # feature_service 额外需要 hiredis
    if(HIREDIS_LIB AND "${SERVICE_NAME}" STREQUAL "feature_service")
        target_include_directories(${SERVICE_NAME} PRIVATE ${HIREDIS_INCLUDE_DIR})
        target_link_libraries(${SERVICE_NAME} PRIVATE ${HIREDIS_LIB})
    endif()
endfunction()

add_service_executable(feature_service)
#add_service_executable(rank_service)
add_service_executable(recall_service)
#add_service_executable(proxy)