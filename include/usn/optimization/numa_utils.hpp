// SPDX-License-Identifier: MIT
//
// NUMA-Aware 工具函数
//
// 设计目标：
// - NUMA 拓扑检测
// - CPU 亲和性绑定
// - 内存本地性优化
// - 线程绑定到特定 NUMA 节点
//
// 依赖：libnuma（可选，如果没有则提供 fallback）

#pragma once

#include <cstddef>
#include <cstdlib>
#include <vector>
#include <thread>

#ifdef __linux__
#include <numa.h>
#include <numaif.h>
#include <pthread.h>
#include <sched.h>
#endif

namespace usn {

// NUMA 节点信息
struct NumaNode {
    int id;
    std::size_t num_cpus;
    std::vector<int> cpu_ids;
    std::size_t free_memory;  // KB
};

// NUMA 拓扑信息
class NumaTopology {
public:
    static NumaTopology& instance() {
        static NumaTopology topo;
        return topo;
    }
    
    // 检测 NUMA 拓扑
    bool detect() {
#ifdef __linux__
        if (numa_available() < 0) {
            numa_available_ = false;
            return false;
        }
        
        numa_available_ = true;
        max_node_ = numa_max_node();
        num_nodes_ = max_node_ + 1;
        
        // 获取每个节点的信息
        nodes_.clear();
        for (int node = 0; node <= max_node_; ++node) {
            NumaNode info;
            info.id = node;
            
            // 获取节点上的 CPU
            struct bitmask* cpus = numa_allocate_cpumask();
            if (numa_node_to_cpus(node, cpus) == 0) {
                info.num_cpus = numa_bitmask_weight(cpus);
                for (int cpu = 0; cpu < numa_num_possible_cpus(); ++cpu) {
                    if (numa_bitmask_isbitset(cpus, cpu)) {
                        info.cpu_ids.push_back(cpu);
                    }
                }
            }
            numa_free_cpumask(cpus);
            
            // 获取可用内存（需要 root 权限）
            long long free = numa_node_size(node, nullptr);
            info.free_memory = free > 0 ? free / 1024 : 0;
            
            nodes_.push_back(info);
        }
        
        return true;
#else
        // 非 Linux 系统：假设单节点
        numa_available_ = false;
        num_nodes_ = 1;
        max_node_ = 0;
        return false;
#endif
    }
    
    // 检查 NUMA 是否可用
    bool is_available() const noexcept {
        return numa_available_;
    }
    
    // 获取节点数量
    int num_nodes() const noexcept {
        return num_nodes_;
    }
    
    // 获取节点信息
    const std::vector<NumaNode>& nodes() const noexcept {
        return nodes_;
    }
    
    // 获取当前线程所在的 NUMA 节点
    int get_current_node() const {
#ifdef __linux__
        if (!numa_available_) {
            return 0;
        }
        return numa_node_of_cpu(sched_getcpu());
#else
        return 0;
#endif
    }
    
    // 在指定节点分配内存
    void* allocate_on_node(std::size_t size, int node) {
#ifdef __linux__
        if (!numa_available_) {
            (void)node;  // 未使用参数
            return ::malloc(size);
        }
        return numa_alloc_onnode(size, node);
#else
        (void)node;  // 未使用参数
        return ::malloc(size);
#endif
    }
    
    // 释放 NUMA 分配的内存
    void free_on_node(void* ptr, std::size_t size) {
#ifdef __linux__
        if (!numa_available_) {
            (void)size;  // 未使用参数
            ::free(ptr);
            return;
        }
        numa_free(ptr, size);
#else
        (void)size;  // 未使用参数
        ::free(ptr);
#endif
    }

private:
    NumaTopology() {
        detect();
    }
    
    bool numa_available_ = false;
    int num_nodes_ = 1;
    int max_node_ = 0;
    std::vector<NumaNode> nodes_;
};

// CPU 亲和性工具
class CpuAffinity {
public:
    // 将当前线程绑定到指定的 CPU core
    static bool bind_to_cpu(int cpu_id) {
#ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);
        
        return pthread_setaffinity_np(
            pthread_self(),
            sizeof(cpu_set_t),
            &cpuset
        ) == 0;
#else
        // macOS 不支持，返回 false
        (void)cpu_id;
        return false;
#endif
    }
    
    // 将线程绑定到指定的 CPU core
    static bool bind_thread_to_cpu(std::thread& thread, int cpu_id) {
#ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(cpu_id, &cpuset);
        
        return pthread_setaffinity_np(
            thread.native_handle(),
            sizeof(cpu_set_t),
            &cpuset
        ) == 0;
#else
        (void)thread;
        (void)cpu_id;
        return false;
#endif
    }
    
    // 将线程绑定到 NUMA 节点的第一个 CPU
    static bool bind_to_numa_node(int node_id) {
        auto& topo = NumaTopology::instance();
        if (!topo.is_available() || node_id >= topo.num_nodes()) {
            return false;
        }
        
        const auto& nodes = topo.nodes();
        if (node_id < static_cast<int>(nodes.size()) && !nodes[node_id].cpu_ids.empty()) {
            return bind_to_cpu(nodes[node_id].cpu_ids[0]);
        }
        
        return false;
    }
    
    // 获取当前 CPU core ID
    static int get_current_cpu() {
#ifdef __linux__
        return sched_getcpu();
#else
        return -1;
#endif
    }
    
    // 设置 CPU 亲和性掩码（绑定到多个 CPU）
    static bool set_affinity_mask(const std::vector<int>& cpu_ids) {
#ifdef __linux__
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        
        for (int cpu_id : cpu_ids) {
            CPU_SET(cpu_id, &cpuset);
        }
        
        return pthread_setaffinity_np(
            pthread_self(),
            sizeof(cpu_set_t),
            &cpuset
        ) == 0;
#else
        (void)cpu_ids;
        return false;
#endif
    }
};

}  // namespace usn
