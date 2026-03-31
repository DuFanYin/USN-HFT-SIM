add_executable(usn_zero_copy_tests
    zero_copy_basic.cpp
)
target_link_libraries(usn_zero_copy_tests PRIVATE usn_core pthread)
add_test(NAME zero_copy_basic COMMAND usn_zero_copy_tests)

add_executable(usn_numa_utils_tests
    numa_utils_basic.cpp
)
target_link_libraries(usn_numa_utils_tests PRIVATE usn_core pthread)
if(USN_HAVE_NUMA)
    target_link_libraries(usn_numa_utils_tests PRIVATE ${NUMA_LIBRARY})
endif()
add_test(NAME numa_utils_basic COMMAND usn_numa_utils_tests)

