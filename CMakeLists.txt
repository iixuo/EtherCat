cmake_minimum_required(VERSION 3.16)
project(ethercat_beckhoff_control VERSION 1.0.0 LANGUAGES CXX)

# -----------------------------
# Basic C++ settings
# -----------------------------
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Qt 自动处理 moc/uic/rcc（信号槽、.ui、资源）
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# -----------------------------
# Options
# -----------------------------
# 在Linux上默认启用EtherCAT，其他平台默认关闭
if(UNIX AND NOT APPLE)
    option(WITH_IGH_ETHERCAT "Enable IgH EtherCAT Master (ecrt.h, libethercat)" ON)
else()
    option(WITH_IGH_ETHERCAT "Enable IgH EtherCAT Master (ecrt.h, libethercat)" OFF)
endif()

message(STATUS "WITH_IGH_ETHERCAT = ${WITH_IGH_ETHERCAT}")

# -----------------------------
# Qt (支持 Qt5 和 Qt6)
# -----------------------------
# 优先查找 Qt6，如果没有则使用 Qt5
find_package(Qt6 QUIET COMPONENTS Core Widgets)
if(Qt6_FOUND)
    message(STATUS "Found Qt6: ${Qt6_VERSION}")
    set(QT_VERSION_MAJOR 6)
else()
    find_package(Qt5 REQUIRED COMPONENTS Core Widgets)
    message(STATUS "Found Qt5: ${Qt5_VERSION}")
    set(QT_VERSION_MAJOR 5)
endif()

# -----------------------------
# Threads (for std::thread/pthread)
# -----------------------------
find_package(Threads REQUIRED)

# -----------------------------
# Sources (按你截图的结构)
# -----------------------------
set(APP_SOURCES
    src/main.cpp
    src/gui/mainwindow.cpp
    src/gui/mainwindow.h
    src/gui/mainwindow.ui
)

# EtherCAT 源文件（稍后根据库是否找到决定）
set(EC_SOURCES "")

add_executable(${PROJECT_NAME}
    ${APP_SOURCES}
)

# include 目录
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/gui
)

# 根据 Qt 版本链接正确的库
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Core
        Qt6::Widgets
        Threads::Threads
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt5::Core
        Qt5::Widgets
        Threads::Threads
    )
endif()

# -----------------------------
# IgH EtherCAT Master (ecrt)
# -----------------------------
if(WITH_IGH_ETHERCAT AND UNIX AND NOT APPLE)
    # 常见安装位置：/opt/etherlab（IgH推荐）、/usr/local、/usr
    find_path(ETHERCAT_INCLUDE_DIR
        NAMES ecrt.h
        PATHS
            /opt/etherlab/include
            /usr/local/include
            /usr/include
    )

    find_library(ETHERCAT_LIBRARY
        NAMES ethercat
        PATHS
            /opt/etherlab/lib
            /usr/local/lib
            /usr/lib
            /usr/lib/x86_64-linux-gnu
    )

    if(NOT ETHERCAT_INCLUDE_DIR OR NOT ETHERCAT_LIBRARY)
        message(WARNING
            "IgH EtherCAT not found. Building in SIMULATION mode.\n"
            "  ecrt.h: ${ETHERCAT_INCLUDE_DIR}\n"
            "  libethercat: ${ETHERCAT_LIBRARY}\n"
            "To enable EtherCAT support, install IgH EtherCAT Master.\n"
            "Falling back to simulation mode..."
        )
        set(WITH_IGH_ETHERCAT OFF)
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_IGH_ETHERCAT=0)
        message(STATUS "Building in SIMULATION mode (IgH EtherCAT not found)")
    else()
        message(STATUS "Found IgH EtherCAT:")
        message(STATUS "  Include: ${ETHERCAT_INCLUDE_DIR}")
        message(STATUS "  Library: ${ETHERCAT_LIBRARY}")
        
        # 添加 EtherCATMaster.cpp 源文件
        target_sources(${PROJECT_NAME} PRIVATE src/ethercat/EtherCATMaster.cpp)
        
        target_include_directories(${PROJECT_NAME} PRIVATE ${ETHERCAT_INCLUDE_DIR})
        target_link_libraries(${PROJECT_NAME} PRIVATE ${ETHERCAT_LIBRARY})
        target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_IGH_ETHERCAT=1)
        message(STATUS "Building with EtherCAT hardware support")
    endif()
else()
    target_compile_definitions(${PROJECT_NAME} PRIVATE WITH_IGH_ETHERCAT=0)
    if(WIN32)
        message(STATUS "Windows detected - Building in SIMULATION mode")
    elseif(APPLE)
        message(STATUS "macOS detected - Building in SIMULATION mode")
    else()
        message(STATUS "Building in SIMULATION mode (WITH_IGH_ETHERCAT=OFF)")
    endif()
endif()

# -----------------------------
# Linux: rt 库（如果你代码里用到 clock_nanosleep 等）
# -----------------------------
if(UNIX AND NOT APPLE)
    target_link_libraries(${PROJECT_NAME} PRIVATE rt)
endif()

# -----------------------------
# Warnings (可选)
# -----------------------------
if (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()

message(STATUS "========================================")
message(STATUS "Build Configuration:")
message(STATUS "  Project: ${PROJECT_NAME}")
message(STATUS "  Qt Version: ${QT_VERSION_MAJOR}")
message(STATUS "  EtherCAT: ${WITH_IGH_ETHERCAT}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "========================================")
