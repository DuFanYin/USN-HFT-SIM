cmake_minimum_required(VERSION 3.16)

project(user_space_netstack LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2 -march=native")
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O2 -march=native")

# 选项
option(USN_BUILD_TESTS "Build unit tests" ON)
option(USN_BUILD_BENCHMARKS "Build benchmarks" ON)

# 查找 libnuma（可选）
find_library(NUMA_LIBRARY numa)
if(NUMA_LIBRARY)
    message(STATUS "Found libnuma: ${NUMA_LIBRARY}")
    set(USN_HAVE_NUMA ON)
else()
    message(STATUS "libnuma not found, NUMA features will be disabled")
    set(USN_HAVE_NUMA OFF)
endif()

# 查找 liburing（可选）
find_library(URING_LIBRARY uring)
if(URING_LIBRARY)
    message(STATUS "Found liburing: ${URING_LIBRARY}")
    set(USN_HAVE_URING ON)
else()
    message(STATUS "liburing not found, io_uring features will be disabled")
    set(USN_HAVE_URING OFF)
endif()

# 包含目录
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include)

# 核心库（header-only for now）
add_library(usn_core INTERFACE)
target_include_directories(usn_core
    INTERFACE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 测试
if(USN_BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    add_subdirectory(tests)
endif()

# 基准测试
if(USN_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()

# 应用程序
add_subdirectory(apps)
