add_executable(usn_batch_io_tests
    batch_io_basic.cpp
)
target_link_libraries(usn_batch_io_tests PRIVATE usn_core pthread GTest::gtest_main)
add_test(NAME batch_io_basic COMMAND usn_batch_io_tests)

add_executable(usn_epoll_tests
    epoll_basic.cpp
)
target_link_libraries(usn_epoll_tests PRIVATE usn_core pthread GTest::gtest_main)
add_test(NAME epoll_basic COMMAND usn_epoll_tests)
