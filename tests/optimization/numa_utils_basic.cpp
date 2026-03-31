// SPDX-License-Identifier: MIT
//
// NUMA Utils 基础测试

#include <usn/optimization/numa_utils.hpp>
#include <gtest/gtest.h>
#include <cstring>
#include <thread>

using namespace usn;

TEST(NumaTopology, DetectAndQuery) {
    auto& topo = NumaTopology::instance();

    // 至少有 1 个节点
    EXPECT_GE(topo.num_nodes(), 1);

    if (topo.is_available()) {
        const auto& nodes = topo.nodes();
        EXPECT_FALSE(nodes.empty());

        int current_node = topo.get_current_node();
        EXPECT_GE(current_node, 0);
    }
}

TEST(CpuAffinity, BindToCurrentCpu) {
    int current_cpu = CpuAffinity::get_current_cpu();
    EXPECT_GE(current_cpu, 0);

    EXPECT_TRUE(CpuAffinity::bind_to_cpu(current_cpu));
}

TEST(CpuAffinity, BindThread) {
    int current_cpu = CpuAffinity::get_current_cpu();

    bool thread_bound = false;
    std::thread t([current_cpu, &thread_bound]() {
        thread_bound = CpuAffinity::bind_to_cpu(current_cpu);
    });
    t.join();

    EXPECT_TRUE(thread_bound);
}

TEST(NumaTopology, MemoryAllocation) {
    auto& topo = NumaTopology::instance();

    if (!topo.is_available()) {
        GTEST_SKIP() << "NUMA not available";
    }

    const std::size_t size = 1024 * 1024;  // 1MB

    void* ptr = topo.allocate_on_node(size, 0);
    ASSERT_NE(ptr, nullptr);

    std::memset(ptr, 0xAA, size);

    topo.free_on_node(ptr, size);
}
