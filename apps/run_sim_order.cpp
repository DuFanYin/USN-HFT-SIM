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
#include <string>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [--duration SEC] [--qps REQ_PER_SEC] [--burst N]\n"
        << "               [--disconnect-interval-s SEC] [--server-drop-at-s SEC]\n"
        << "               [--timeout-ms MS] [--cancel-ratio 0-100] [--replace-ratio 0-100]\n"
        << "               [--gateway-backpressure-threshold N]\n";
}

pid_t spawn_process(const char* bin_path, const std::vector<std::string>& args) {
    pid_t pid = ::fork();
    if (pid < 0) {
        std::perror("fork");
        return -1;
    }
    if (pid == 0) {
        std::vector<char*> argv;
        argv.reserve(args.size() + 2);
        argv.push_back(const_cast<char*>(bin_path));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        ::execv(bin_path, argv.data());
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
    int qps = 2;
    int burst = 1;
    int disconnect_interval_s = 0;
    int server_drop_at_s = 0;
    int timeout_ms = 1000;
    int cancel_ratio = 0;
    int replace_ratio = 0;
    int gateway_backpressure_threshold = 0;

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--help") {
            print_usage(argv[0]);
            return 0;
        }
    }

    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key = argv[i];
        const int val = std::atoi(argv[i + 1]);
        if (key == "--duration") {
            run_seconds = std::max(1, val);
        } else if (key == "--qps") {
            qps = std::max(1, val);
        } else if (key == "--burst") {
            burst = std::max(1, val);
        } else if (key == "--disconnect-interval-s") {
            disconnect_interval_s = std::max(0, val);
        } else if (key == "--server-drop-at-s") {
            server_drop_at_s = std::max(0, val);
        } else if (key == "--timeout-ms") {
            timeout_ms = std::max(1, val);
        } else if (key == "--cancel-ratio") {
            cancel_ratio = std::clamp(val, 0, 100);
        } else if (key == "--replace-ratio") {
            replace_ratio = std::clamp(val, 0, 100);
        } else if (key == "--gateway-backpressure-threshold") {
            gateway_backpressure_threshold = std::max(0, val);
        } else {
            std::cerr << "[run_order_sim] unknown arg: " << key << "\n";
            return 1;
        }
    }

    std::cout << "[run_order_sim] starting gateway...\n";
    pid_t gateway = spawn_process(
        kGatewayPath,
        {
            "--server-drop-at-s", std::to_string(server_drop_at_s),
            "--backpressure-threshold", std::to_string(gateway_backpressure_threshold),
        }
    );
    if (gateway <= 0) {
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::cout << "[run_order_sim] starting client...\n";
    pid_t client = spawn_process(
        kClientPath,
        {
            "--qps", std::to_string(qps),
            "--burst", std::to_string(burst),
            "--disconnect-interval-s", std::to_string(disconnect_interval_s),
            "--timeout-ms", std::to_string(timeout_ms),
            "--cancel-ratio", std::to_string(cancel_ratio),
            "--replace-ratio", std::to_string(replace_ratio),
        }
    );
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

