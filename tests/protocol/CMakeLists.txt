add_executable(usn_udp_protocol_tests
    udp_protocol_basic.cpp
)
target_link_libraries(usn_udp_protocol_tests PRIVATE usn_core pthread GTest::gtest_main)
add_test(NAME udp_protocol_basic COMMAND usn_udp_protocol_tests)

add_executable(usn_udp_feedback_tests
    udp_feedback_basic.cpp
)
target_link_libraries(usn_udp_feedback_tests PRIVATE usn_core pthread GTest::gtest_main)
add_test(NAME udp_feedback_basic COMMAND usn_udp_feedback_tests)

add_executable(usn_udp_socket_tests
    udp_socket_basic.cpp
)
target_link_libraries(usn_udp_socket_tests PRIVATE usn_core pthread GTest::gtest_main)
add_test(NAME udp_socket_basic COMMAND usn_udp_socket_tests)

add_executable(usn_tcp_protocol_tests
    tcp_protocol_basic.cpp
)
target_link_libraries(usn_tcp_protocol_tests PRIVATE usn_core pthread GTest::gtest_main)
add_test(NAME tcp_protocol_basic COMMAND usn_tcp_protocol_tests)

add_executable(usn_tcp_socket_tests
    tcp_socket_basic.cpp
)
target_link_libraries(usn_tcp_socket_tests PRIVATE usn_core pthread GTest::gtest_main)
add_test(NAME tcp_socket_basic COMMAND usn_tcp_socket_tests)
