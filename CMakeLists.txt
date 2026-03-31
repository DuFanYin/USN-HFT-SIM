cmake_minimum_required(VERSION 3.16)

project(user_space_netstack LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 编译选项
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O3 -march=native")
set(CMAKE_CXX_FLAGS_RELEASE "-DNDEBUG -O3 -march=native")

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
    
    # Core 模块测试
    add_executable(usn_mpsc_queue_tests
        tests/core/mpsc_queue_basic.cpp
    )
    target_link_libraries(usn_mpsc_queue_tests PRIVATE usn_core pthread)
    add_test(NAME mpsc_queue_basic COMMAND usn_mpsc_queue_tests)
    
    add_executable(usn_packet_ring_tests
        tests/core/packet_ring_basic.cpp
    )
    target_link_libraries(usn_packet_ring_tests PRIVATE usn_core pthread)
    add_test(NAME packet_ring_basic COMMAND usn_packet_ring_tests)
    
    # I/O 模块测试
    add_executable(usn_batch_io_tests
        tests/io/batch_io_basic.cpp
    )
    target_link_libraries(usn_batch_io_tests PRIVATE usn_core pthread)
    add_test(NAME batch_io_basic COMMAND usn_batch_io_tests)
    
    add_executable(usn_epoll_tests
        tests/io/epoll_basic.cpp
    )
    target_link_libraries(usn_epoll_tests PRIVATE usn_core pthread)
    add_test(NAME epoll_basic COMMAND usn_epoll_tests)
    
    # Optimization 模块测试
    add_executable(usn_zero_copy_tests
        tests/optimization/zero_copy_basic.cpp
    )
    target_link_libraries(usn_zero_copy_tests PRIVATE usn_core pthread)
    add_test(NAME zero_copy_basic COMMAND usn_zero_copy_tests)
    
    add_executable(usn_numa_utils_tests
        tests/optimization/numa_utils_basic.cpp
    )
    target_link_libraries(usn_numa_utils_tests PRIVATE usn_core pthread)
    if(USN_HAVE_NUMA)
        target_link_libraries(usn_numa_utils_tests PRIVATE ${NUMA_LIBRARY})
    endif()
    add_test(NAME numa_utils_basic COMMAND usn_numa_utils_tests)
    
    # Protocol 模块测试
    add_executable(usn_udp_protocol_tests
        tests/protocol/udp_protocol_basic.cpp
    )
    target_link_libraries(usn_udp_protocol_tests PRIVATE usn_core pthread)
    add_test(NAME udp_protocol_basic COMMAND usn_udp_protocol_tests)
    
    add_executable(usn_tcp_protocol_tests
        tests/protocol/tcp_protocol_basic.cpp
    )
    target_link_libraries(usn_tcp_protocol_tests PRIVATE usn_core pthread)
    add_test(NAME tcp_protocol_basic COMMAND usn_tcp_protocol_tests)
endif()

# 基准测试
if(USN_BUILD_BENCHMARKS)
    add_executable(usn_mpsc_queue_bench
        benchmarks/core/mpsc_queue_bench.cpp
    )
    target_link_libraries(usn_mpsc_queue_bench PRIVATE usn_core pthread)
endif()

# 示例程序
add_executable(usn_mpsc_queue_example
    examples/core/mpsc_queue_example.cpp
)
target_link_libraries(usn_mpsc_queue_example PRIVATE usn_core pthread)

add_executable(usn_packet_ring_example
    examples/core/packet_ring_example.cpp
)
target_link_libraries(usn_packet_ring_example PRIVATE usn_core pthread)

add_executable(usn_zero_copy_example
    examples/optimization/zero_copy_example.cpp
)
target_link_libraries(usn_zero_copy_example PRIVATE usn_core pthread)
if(USN_HAVE_NUMA)
    target_link_libraries(usn_zero_copy_example PRIVATE ${NUMA_LIBRARY})
endif()

# 应用：TCP 订单系统（单线程）
add_executable(order_gateway
    apps/order_tcp/order_gateway.cpp
)
target_link_libraries(order_gateway PRIVATE usn_core pthread)

add_executable(order_client
    apps/order_tcp/order_client.cpp
)
target_link_libraries(order_client PRIVATE usn_core pthread)

# 应用：UDP 行情组播
add_executable(feed_publisher
    apps/market_udp/feed_publisher.cpp
)
target_link_libraries(feed_publisher PRIVATE usn_core pthread)

add_executable(feed_subscriber
    apps/market_udp/feed_subscriber.cpp
)
target_link_libraries(feed_subscriber PRIVATE usn_core pthread)
