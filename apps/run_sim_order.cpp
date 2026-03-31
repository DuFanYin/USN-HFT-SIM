// SPDX-License-Identifier: MIT
//
// Order TCP 仿真入口：
// - 拉起 order_gateway 与 order_client 两个进程
// - 运行指定秒数后自动停止

#include <csignal>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

pid_t spawn_process(const char* bin_path) {
    pid_t pid = ::fork();
    if (pid < 0) {
        std::perror("fork");
        return -1;
    }
    if (pid == 0) {
        ::execl(bin_path, bin_path, static_cast<char*>(nullptr));
        std::perror("execl");
        _exit(127);
    }
    return pid;
}

void stop_process(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    ::kill(pid, SIGINT);
    ::waitpid(pid, nullptr, 0);
}

}  // namespace

int main(int argc, char** argv) {
    constexpr const char* kGatewayPath = "./build/apps/order_gateway";
    constexpr const char* kClientPath = "./build/apps/order_client";

    int run_seconds = 10;
    if (argc >= 2) {
        run_seconds = std::max(1, std::atoi(argv[1]));
    }

    std::cout << "[run_order_sim] starting gateway...\n";
    pid_t gateway = spawn_process(kGatewayPath);
    if (gateway <= 0) {
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::cout << "[run_order_sim] starting client...\n";
    pid_t client = spawn_process(kClientPath);
    if (client <= 0) {
        stop_process(gateway);
        return 1;
    }

    std::cout << "[run_order_sim] running for " << run_seconds << "s\n";
    std::this_thread::sleep_for(std::chrono::seconds(run_seconds));

    std::cout << "[run_order_sim] stopping...\n";
    stop_process(client);
    stop_process(gateway);
    std::cout << "[run_order_sim] done\n";
    return 0;
}

