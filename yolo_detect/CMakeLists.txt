cmake_minimum_required(VERSION 3.16)

set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR arm)

set(TOOLCHAIN_ROOT "/opt/rv1106/arm-rockchip830-linux-uclibcgnueabihf/bin")
set(CMAKE_C_COMPILER "${TOOLCHAIN_ROOT}/arm-rockchip830-linux-uclibcgnueabihf-gcc")
set(CMAKE_CXX_COMPILER "${TOOLCHAIN_ROOT}/arm-rockchip830-linux-uclibcgnueabihf-g++")

project(rknn_yolo_demo LANGUAGES C CXX)

set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(LUCKFOX_ROOT "/home/lubancat/luckfox/luckfox_pico_rkmpi_example-kernel-5.10.160")

# OpenCV：uClibc 精简版（core/imgproc，不含 imgcodecs）
set(OpenCV_DIR "${LUCKFOX_ROOT}/lib/uclibc/lib/cmake/opencv4")
find_package(OpenCV REQUIRED)

# stb 单头库（替代 imread/imwrite）
set(STB_INC "/home/lubancat/workspace/rknn-toolkit2-master/rknpu2/examples/3rdparty/stb")

# RKNN Runtime：uClibc
set(RKNN_INC "${LUCKFOX_ROOT}/include/rknn")
set(RKNN_LIB "${LUCKFOX_ROOT}/lib/uclibc")

# D-Bus（纯 libdbus，32位 armhf 交叉编译产物；dbus 模块位于仓库上层目录）
set(DBUS_ROOT "/home/lubancat/third_so/dbus/armhf")
set(DBUS_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../dbus")

add_executable(rknn_yolo
    yolo.cpp
    ${DBUS_SRC_DIR}/ipc_dbus_server.c
)

target_include_directories(rknn_yolo PRIVATE
    ${LUCKFOX_ROOT}/include
    ${OpenCV_INCLUDE_DIRS}
    ${RKNN_INC}
    ${STB_INC}
    ${DBUS_SRC_DIR}
    ${DBUS_ROOT}/include/dbus-1.0
    ${DBUS_ROOT}/lib/dbus-1.0/include
)

target_link_directories(rknn_yolo PRIVATE
    ${RKNN_LIB}
    ${DBUS_ROOT}/lib
)

target_link_libraries(rknn_yolo PRIVATE
    rknnmrt
    ${OpenCV_LIBS}
    dbus-1
    pthread
)
