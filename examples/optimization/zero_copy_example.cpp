// SPDX-License-Identifier: MIT
//
// Zero-Copy 和 NUMA-Aware 使用示例

#include <usn/optimization/zero_copy.hpp>
#include <usn/optimization/numa_utils.hpp>
#include <usn/core/packet_ring.hpp>
#include <iostream>
#include <thread>
#include <vector>

using namespace usn;

int main() {
    std::cout << "Zero-Copy & NUMA-Aware Example\n";
    std::cout << "==============================\n\n";
    
    // 1. 检测 NUMA 拓扑
    std::cout << "[1] NUMA Topology Detection\n";
    auto& topo = NumaTopology::instance();
    
    if (topo.is_available()) {
        std::cout << "  ✓ NUMA is available\n";
        std::cout << "  Number of nodes: " << topo.num_nodes() << "\n";
        
        const auto& nodes = topo.nodes();
        for (const auto& node : nodes) {
            std::cout << "  Node " << node.id << ": "
                      << node.num_cpus << " CPUs\n";
        }
    } else {
        std::cout << "  ⚠ NUMA not available (single node)\n";
    }
    std::cout << "\n";
    
    // 2. 创建 Zero-Copy 缓冲区
    std::cout << "[2] Creating Zero-Copy Buffer\n";
    const std::size_t buffer_size = 10 * 1024 * 1024;  // 10MB
    
    auto buffer = ZeroCopyBuffer::create(buffer_size, false);
    if (!buffer) {
        std::cerr << "Failed to create zero-copy buffer\n";
        return 1;
    }
    
    std::cout << "  ✓ Created " << buffer_size / 1024 / 1024 
              << " MB zero-copy buffer\n";
    std::cout << "  Uses huge pages: " 
              << (buffer->uses_huge_pages() ? "yes" : "no") << "\n";
    std::cout << "\n";
    
    // 3. 使用 Zero-Copy 内存池
    std::cout << "[3] Creating Zero-Copy Memory Pool\n";
    const std::size_t block_size = 1500;  // 以太网 MTU
    const std::size_t num_blocks = 1000;
    
    ZeroCopyMemoryPool pool(block_size, num_blocks, false);
    std::cout << "  ✓ Created pool: " << num_blocks << " blocks, "
              << block_size << " bytes each\n";
    std::cout << "  Total size: " 
              << (num_blocks * block_size) / 1024 / 1024 << " MB\n";
    std::cout << "\n";
    
    // 4. 分配和使用内存块
    std::cout << "[4] Allocating and using memory blocks\n";
    std::vector<uint8_t*> blocks;
    
    for (std::size_t i = 0; i < 100; ++i) {
        uint8_t* block = pool.allocate();
        if (!block) {
            std::cout << "  Pool exhausted at block " << i << "\n";
            break;
        }
        
        // 填充数据
        std::fill(block, block + block_size, static_cast<uint8_t>(i & 0xFF));
        blocks.push_back(block);
    }
    
    std::cout << "  ✓ Allocated " << blocks.size() << " blocks\n";
    
    // 验证数据
    bool all_valid = true;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        if (blocks[i][0] != static_cast<uint8_t>(i & 0xFF)) {
            all_valid = false;
            break;
        }
    }
    
    if (all_valid) {
        std::cout << "  ✓ All blocks contain correct data\n";
    }
    std::cout << "\n";
    
    // 5. 与 Packet Ring Buffer 集成
    std::cout << "[5] Integrating with Packet Ring Buffer\n";
    PacketRing ring(1024);
    
    std::size_t packets_pushed = 0;
    for (std::size_t i = 0; i < blocks.size(); ++i) {
        Packet pkt(blocks[i], block_size, 0);
        if (ring.try_push(pkt)) {
            packets_pushed++;
        }
    }
    
    std::cout << "  ✓ Pushed " << packets_pushed << " packets to ring\n";
    
    // 从 ring 中读取
    std::vector<Packet> received(packets_pushed);
    std::size_t packets_popped = ring.try_pop_batch(received);
    std::cout << "  ✓ Popped " << packets_popped << " packets from ring\n";
    std::cout << "\n";
    
    // 6. CPU 亲和性
    std::cout << "[6] CPU Affinity\n";
    int current_cpu = CpuAffinity::get_current_cpu();
    if (current_cpu >= 0) {
        std::cout << "  Current CPU: " << current_cpu << "\n";
        
        bool bound = CpuAffinity::bind_to_cpu(current_cpu);
        std::cout << "  Bind to CPU " << current_cpu << ": "
                  << (bound ? "success" : "failed") << "\n";
    }
    std::cout << "\n";
    
    // 7. 清理
    std::cout << "[7] Cleanup\n";
    for (auto* block : blocks) {
        pool.deallocate(block);
    }
    std::cout << "  ✓ Returned all blocks to pool\n";
    
    std::cout << "\n✅ Example completed successfully!\n";
    
    return 0;
}
