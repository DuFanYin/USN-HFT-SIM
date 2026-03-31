add_executable(usn_udp_protocol_tests
    udp_protocol_basic.cpp
)
target_link_libraries(usn_udp_protocol_tests PRIVATE usn_core pthread)
add_test(NAME udp_protocol_basic COMMAND usn_udp_protocol_tests)

add_executable(usn_tcp_protocol_tests
    tcp_protocol_basic.cpp
)
target_link_libraries(usn_tcp_protocol_tests PRIVATE usn_core pthread)
add_test(NAME tcp_protocol_basic COMMAND usn_tcp_protocol_tests)

