add_executable(usn_mpsc_queue_example
    core/mpsc_queue_example.cpp
)
target_link_libraries(usn_mpsc_queue_example PRIVATE usn_core pthread)

add_executable(usn_packet_ring_example
    core/packet_ring_example.cpp
)
target_link_libraries(usn_packet_ring_example PRIVATE usn_core pthread)

add_executable(usn_zero_copy_example
    optimization/zero_copy_example.cpp
)
target_link_libraries(usn_zero_copy_example PRIVATE usn_core pthread)
if(USN_HAVE_NUMA)
    target_link_libraries(usn_zero_copy_example PRIVATE ${NUMA_LIBRARY})
endif()

