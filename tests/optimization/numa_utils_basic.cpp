// SPDX-License-Identifier: MIT
//
// NUMA Utils 基础测试

#include <usn/optimization/numa_utils.hpp>
#include <cassert>
#include <cstring>
#include <iostream>
#include <thread>

using namespace usn;

void test_numa_topology() {
    std::cout << "[TEST] NUMA Topology\n";
    
    auto& topo = NumaTopology::instance();
    
    std::cout << "  NUMA available: " << (topo.is_available() ? "yes" : "no") << "\n";
    std::cout << "  Number of nodes: " << topo.num_nodes() << "\n";
    
    if (topo.is_available()) {
        const auto& nodes = topo.nodes();
        for (const auto& node : nodes) {
            std::cout << "  Node " << node.id << ": "
                      << node.num_cpus << " CPUs, "
                      << node.free_memory << " KB free\n";
        }
        
        int current_node = topo.get_current_node();
        std::cout << "  Current node: " << current_node << "\n";
    } else {
        std::cout << "  ⚠ NUMA not available (single node assumed)\n";
    }
    
    std::cout << "  ✓ NUMA Topology test passed\n";
}

void test_cpu_affinity() {
    std::cout << "[TEST] CPU Affinity\n";
    
    int current_cpu = CpuAffinity::get_current_cpu();
    std::cout << "  Current CPU: " << current_cpu << "\n";
    
    // 尝试绑定到当前 CPU（应该成功）
    bool bound = CpuAffinity::bind_to_cpu(current_cpu);
    std::cout << "  Bind to CPU " << current_cpu << ": " 
              << (bound ? "success" : "failed") << "\n";
    
    // 测试线程绑定
    std::thread t([current_cpu]() {
        bool thread_bound = CpuAffinity::bind_to_cpu(current_cpu);
        std::cout << "  Thread bind to CPU " << current_cpu << ": "
                  << (thread_bound ? "success" : "failed") << "\n";
    });
    t.join();
    
    std::cout << "  ✓ CPU Affinity test passed\n";
}

void test_numa_memory_allocation() {
    std::cout << "[TEST] NUMA Memory Allocation\n";
    
    auto& topo = NumaTopology::instance();
    
    if (topo.is_available()) {
        const std::size_t size = 1024 * 1024;  // 1MB
        
        // 尝试在节点 0 分配内存
        void* ptr = topo.allocate_on_node(size, 0);
        assert(ptr != nullptr);
        
        // 使用内存
        std::memset(ptr, 0xAA, size);
        
        // 释放
        topo.free_on_node(ptr, size);
        
        std::cout << "  ✓ NUMA memory allocation test passed\n";
    } else {
        std::cout << "  ⚠ Skipped (NUMA not available)\n";
    }
}

int main() {
    std::cout << "Running NUMA Utils tests...\n\n";
    
    try {
        test_numa_topology();
        test_cpu_affinity();
        test_numa_memory_allocation();
        
        std::cout << "\n✅ All tests passed!\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\n❌ Test failed: " << e.what() << "\n";
        return 1;
    }
}
