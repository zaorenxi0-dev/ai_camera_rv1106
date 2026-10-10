cmake_minimum_required(VERSION 3.16)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_ROOT "/opt/rv1106/arm-rockchip830-linux-uclibcgnueabihf/bin")
set(CMAKE_C_COMPILER "${TOOLCHAIN_ROOT}/arm-rockchip830-linux-uclibcgnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/arm-rockchip830-linux-uclibcgnueabihf-g++")

project(ai_camera_rv1106 C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SDK_MEDIA "/home/lubancat/LubanCat_RV1106_RV1103_Linux_SDK_20250212/media")
set(JEMALLOC_ROOT "/home/lubancat/third_so/jemalloc/out_rv1106")
set(DBUS_ROOT "/home/lubancat/third_so/dbus/armhf")

add_executable(ai_camera_rv1106
    main.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/dbus/ipc_dbus_client.c
)

add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/multimedia_service
                 ${CMAKE_CURRENT_BINARY_DIR}/multimedia_service)

target_include_directories(ai_camera_rv1106 PRIVATE
    /home/lubancat/ai_camera_rv1106
    /home/lubancat/ai_camera_rv1106/multimedia_service
    /home/lubancat/middleware/utils
    /home/lubancat/middleware/libstorage/inc
    /home/lubancat/middleware/osal
    /home/lubancat/middleware/services_base
    /home/lubancat/middleware/src_busi/message
    /home/lubancat/middleware/src_busi/devices
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
    /home/lubancat/ai_camera_rv1106/dbus
    ${DBUS_ROOT}/include/dbus-1.0
    ${DBUS_ROOT}/lib/dbus-1.0/include
)

target_compile_options(ai_camera_rv1106 PRIVATE
    -Wall
    -Wextra
)

target_link_directories(ai_camera_rv1106 PRIVATE
    ${SDK_MEDIA}/out/lib
    ${SDK_MEDIA}/out/root/usr/lib
    ${JEMALLOC_ROOT}/lib
    ${DBUS_ROOT}/lib
)

target_link_options(ai_camera_rv1106 PRIVATE
    -Wl,-rpath-link,${SDK_MEDIA}/out/lib:${SDK_MEDIA}/out/root/usr/lib:${JEMALLOC_ROOT}/lib
)

target_link_libraries(ai_camera_rv1106 PRIVATE
    cam::multimedia_service
    rockit
    rockchip_mpp
    rga
    rkaiq
    dl
    m
    atomic
    pthread
    jemalloc
    dbus-1
)
