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

add_executable(usn_busy_poll_tests
    busy_poll_basic.cpp
)
target_link_libraries(usn_busy_poll_tests PRIVATE usn_core pthread GTest::gtest_main)
add_test(NAME busy_poll_basic COMMAND usn_busy_poll_tests)

add_executable(usn_io_status_tests
    io_status_basic.cpp
)
target_link_libraries(usn_io_status_tests PRIVATE usn_core pthread GTest::gtest_main)
add_test(NAME io_status_basic COMMAND usn_io_status_tests)

add_executable(usn_cross_io_consistency_tests
    cross_io_consistency.cpp
)
target_link_libraries(usn_cross_io_consistency_tests PRIVATE usn_core pthread GTest::gtest_main)
if(USN_HAVE_URING)
    target_link_libraries(usn_cross_io_consistency_tests PRIVATE ${URING_LIBRARY})
endif()
add_test(NAME cross_io_consistency COMMAND usn_cross_io_consistency_tests)

if(USN_HAVE_URING)
    add_executable(usn_io_uring_tests
        io_uring_basic.cpp
    )
    target_link_libraries(usn_io_uring_tests PRIVATE usn_core pthread GTest::gtest_main ${URING_LIBRARY})
    add_test(NAME io_uring_basic COMMAND usn_io_uring_tests)
endif()
