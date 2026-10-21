cmake_minimum_required(VERSION 3.16)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_ROOT "/opt/rv1106/arm-rockchip830-linux-uclibcgnueabihf/bin")
set(CMAKE_C_COMPILER "${TOOLCHAIN_ROOT}/arm-rockchip830-linux-uclibcgnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/arm-rockchip830-linux-uclibcgnueabihf-g++")

project(multimedia_service LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SDK_MEDIA "/home/lubancat/LubanCat_RV1106_RV1103_Linux_SDK_20250212/media")
set(JEMALLOC_ROOT "/home/lubancat/third_so/jemalloc/out_rv1106")

set(MULTIMEDIA_SERVICE_TARGET multimedia_service)
set(MULTIMEDIA_SERVICE_SOURCES
    multimedia_service.cpp
    multimedia_service_video.cpp
    multimedia_service_video_base.cpp
    multimedia_service_exception.cpp
    MultimediaServiceFactory.cpp
    MultimediaServiceInterface.cpp
    cam_utils_stub.cpp
    /home/lubancat/ai_camera_rv1106/video_rv1106.c
    /home/lubancat/ai_camera_rv1106/video_handler_manager.c
    ${SDK_MEDIA}/samples/example/common/isp3.x/sample_comm_isp.c
    /home/lubancat/middleware/osal/osal_mutex.c
    /home/lubancat/middleware/services_base/service_base.cpp
    /home/lubancat/middleware/services_base/kn_service_base_exception.cpp
    /home/lubancat/middleware/src_busi/message/message_converter.cpp
    /home/lubancat/middleware/src_busi/message/message_converter_device_capability.cpp
    /home/lubancat/middleware/src_busi/devices/devices_capability.c
    /home/lubancat/middleware/utils/kn_util_sys.c
    /home/lubancat/middleware/utils/cm_timeout.cpp
    /home/lubancat/middleware/utils/jsoncpp/json_reader.cpp
    /home/lubancat/middleware/utils/jsoncpp/json_value.cpp
    /home/lubancat/middleware/utils/jsoncpp/json_writer.cpp
)

add_library(${MULTIMEDIA_SERVICE_TARGET} STATIC
    ${MULTIMEDIA_SERVICE_SOURCES}
)

add_library(cam::multimedia_service ALIAS ${MULTIMEDIA_SERVICE_TARGET})

set_source_files_properties(
    /home/lubancat/middleware/src_busi/devices/devices_capability.c
    PROPERTIES LANGUAGE CXX
)

target_include_directories(${MULTIMEDIA_SERVICE_TARGET}
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
        /home/lubancat/ai_camera_rv1106
        /home/lubancat/middleware/osal
        /home/lubancat/middleware/services_base
        /home/lubancat/middleware/src_busi/message
        /home/lubancat/middleware/src_busi/devices
        /home/lubancat/middleware/utils
        /home/lubancat/middleware/utils/jsoncpp
        ${JEMALLOC_ROOT}/include
        ${SDK_MEDIA}/samples/example/common
        ${SDK_MEDIA}/samples/example/common/isp3.x
        ${SDK_MEDIA}/out/include
        ${SDK_MEDIA}/out/include/rkaiq
        ${SDK_MEDIA}/out/include/rkaiq/uAPI2
        ${SDK_MEDIA}/out/include/rkaiq/common
        ${SDK_MEDIA}/out/include/rkaiq/xcore
        ${SDK_MEDIA}/out/include/rkaiq/algos
        ${SDK_MEDIA}/out/include/rkaiq/iq_parser
        ${SDK_MEDIA}/out/include/rkaiq/iq_parser_v2
        ${SDK_MEDIA}/out/include/rkaiq/smartIr
)

target_compile_options(${MULTIMEDIA_SERVICE_TARGET}
    PRIVATE
        -Wall
        -Wextra
        -Wno-unused-parameter
)

target_link_directories(${MULTIMEDIA_SERVICE_TARGET}
    PUBLIC
        ${SDK_MEDIA}/out/lib
        ${SDK_MEDIA}/out/root/usr/lib
        ${JEMALLOC_ROOT}/lib
)

target_link_libraries(${MULTIMEDIA_SERVICE_TARGET}
    PUBLIC
        pthread
        dl
        m
        atomic
        jemalloc
)
