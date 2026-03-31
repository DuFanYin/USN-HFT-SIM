add_executable(usn_mpsc_queue_bench
    core/mpsc_queue_bench.cpp
)
target_link_libraries(usn_mpsc_queue_bench PRIVATE usn_core pthread)

