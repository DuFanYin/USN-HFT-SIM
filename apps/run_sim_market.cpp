// SPDX-License-Identifier: MIT
//
// Market UDP 仿真入口：
// - 拉起 feed_subscriber 与 feed_publisher 两个进程
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
    constexpr const char* kSubscriberPath = "./build/apps/feed_subscriber";
    constexpr const char* kPublisherPath = "./build/apps/feed_publisher";

    int run_seconds = 10;
    if (argc >= 2) {
        run_seconds = std::max(1, std::atoi(argv[1]));
    }

    std::cout << "[run_market_sim] starting subscriber...\n";
    pid_t subscriber = spawn_process(kSubscriberPath);
    if (subscriber <= 0) {
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::cout << "[run_market_sim] starting publisher...\n";
    pid_t publisher = spawn_process(kPublisherPath);
    if (publisher <= 0) {
        stop_process(subscriber);
        return 1;
    }

    std::cout << "[run_market_sim] running for " << run_seconds << "s\n";
    std::this_thread::sleep_for(std::chrono::seconds(run_seconds));

    std::cout << "[run_market_sim] stopping...\n";
    stop_process(publisher);
    stop_process(subscriber);
    std::cout << "[run_market_sim] done\n";
    return 0;
}

