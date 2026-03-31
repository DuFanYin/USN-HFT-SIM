add_executable(usn_mpsc_queue_tests
    mpsc_queue_basic.cpp
)
target_link_libraries(usn_mpsc_queue_tests PRIVATE usn_core pthread GTest::gtest_main)
add_test(NAME mpsc_queue_basic COMMAND usn_mpsc_queue_tests)

add_executable(usn_packet_ring_tests
    packet_ring_basic.cpp
)
target_link_libraries(usn_packet_ring_tests PRIVATE usn_core pthread GTest::gtest_main)
add_test(NAME packet_ring_basic COMMAND usn_packet_ring_tests)

add_executable(usn_tracing_tests
    tracing_basic.cpp
)
target_link_libraries(usn_tracing_tests PRIVATE usn_core pthread GTest::gtest_main)
add_test(NAME tracing_basic COMMAND usn_tracing_tests)
