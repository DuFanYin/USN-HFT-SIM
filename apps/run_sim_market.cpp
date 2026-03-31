// SPDX-License-Identifier: MIT
//
// Market UDP 仿真入口：
// - 拉起 feed_subscriber 与 feed_publisher 两个进程
// - 运行指定秒数后自动停止

#include <csignal>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace {

void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [--duration SEC] [--rate MSG_PER_SEC] [--payload-size BYTES]\n"
        << "               [--subscribers N] [--streams N] [--jitter-ms MS]\n"
        << "               [--drop-rate 0.0-1.0] [--reorder-rate 0.0-1.0]\n"
        << "               [--slow-subscriber-ratio 0-100]\n";
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
    constexpr const char* kSubscriberPath = "./build/apps/feed_subscriber";
    constexpr const char* kPublisherPath = "./build/apps/feed_publisher";

    int run_seconds = 10;
    int rate = 2;
    int payload_size = static_cast<int>(sizeof(uint64_t) * 2 + sizeof(uint32_t) * 5);
    int subscribers = 1;
    int streams = 1;
    int jitter_ms = 0;
    int slow_subscriber_ratio = 0;
    double drop_rate = 0.0;
    double reorder_rate = 0.0;

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
        } else if (key == "--rate") {
            rate = std::max(1, val);
        } else if (key == "--payload-size") {
            payload_size = std::max(64, val);
        } else if (key == "--subscribers") {
            subscribers = std::max(1, val);
        } else if (key == "--streams") {
            streams = std::max(1, val);
        } else if (key == "--jitter-ms") {
            jitter_ms = std::max(0, val);
        } else if (key == "--slow-subscriber-ratio") {
            slow_subscriber_ratio = std::clamp(val, 0, 100);
        } else if (key == "--drop-rate") {
            drop_rate = std::clamp(std::atof(argv[i + 1]), 0.0, 1.0);
        } else if (key == "--reorder-rate") {
            reorder_rate = std::clamp(std::atof(argv[i + 1]), 0.0, 1.0);
        } else {
            std::cerr << "[run_market_sim] unknown arg: " << key << "\n";
            return 1;
        }
    }

    std::cout << "[run_market_sim] starting subscribers...\n";
    std::vector<pid_t> subscriber_pids;
    std::vector<std::string> summary_files;
    subscriber_pids.reserve(static_cast<std::size_t>(subscribers));
    summary_files.reserve(static_cast<std::size_t>(subscribers));
    for (int i = 0; i < subscribers; ++i) {
        const bool is_slow = (i * 100 / subscribers) < slow_subscriber_ratio;
        const std::string summary_file = "/tmp/usn_run_market_sub_" + std::to_string(::getpid()) + "_" + std::to_string(i) + ".txt";
        summary_files.push_back(summary_file);
        pid_t subscriber = spawn_process(
            kSubscriberPath,
            {
                "--subscriber-id", std::to_string(i),
                "--artificial-delay-ms", is_slow ? "2" : "0",
                "--summary-file", summary_file,
            }
        );
        if (subscriber <= 0) {
            for (pid_t pid : subscriber_pids) {
                stop_process(pid);
            }
            return 1;
        }
        subscriber_pids.push_back(subscriber);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(300));

    std::cout << "[run_market_sim] starting publisher...\n";
    pid_t publisher = spawn_process(
        kPublisherPath,
        {
            "--rate", std::to_string(rate),
            "--payload-size", std::to_string(payload_size),
            "--streams", std::to_string(streams),
            "--jitter-ms", std::to_string(jitter_ms),
            "--drop-rate", std::to_string(drop_rate),
            "--reorder-rate", std::to_string(reorder_rate),
        }
    );
    if (publisher <= 0) {
        for (pid_t pid : subscriber_pids) {
            stop_process(pid);
        }
        return 1;
    }

    std::cout << "[run_market_sim] running for " << run_seconds << "s\n";
    std::this_thread::sleep_for(std::chrono::seconds(run_seconds));

    std::cout << "[run_market_sim] stopping...\n";
    stop_process(publisher);
    for (pid_t pid : subscriber_pids) {
        stop_process(pid);
    }
    uint64_t sum_recv_total = 0;
    uint64_t sum_gaps = 0;
    uint64_t sum_reorder_events = 0;
    uint64_t worst_latency_max = 0;
    int worst_sub_id = -1;
    for (const auto& path : summary_files) {
        std::ifstream in(path);
        if (!in.good()) {
            continue;
        }
        std::string line;
        std::getline(in, line);
        std::istringstream iss(line);
        std::string token;
        int sub_id = -1;
        uint64_t recv_total = 0;
        uint64_t gaps = 0;
        uint64_t reorder_events = 0;
        uint64_t latency_max = 0;
        while (iss >> token) {
            const auto pos = token.find('=');
            if (pos == std::string::npos) {
                continue;
            }
            const std::string k = token.substr(0, pos);
            const std::string v = token.substr(pos + 1);
            if (k == "id") sub_id = std::atoi(v.c_str());
            else if (k == "recv_total") recv_total = static_cast<uint64_t>(std::strtoull(v.c_str(), nullptr, 10));
            else if (k == "gaps") gaps = static_cast<uint64_t>(std::strtoull(v.c_str(), nullptr, 10));
            else if (k == "reorder_events") reorder_events = static_cast<uint64_t>(std::strtoull(v.c_str(), nullptr, 10));
            else if (k == "latency_max_us") latency_max = static_cast<uint64_t>(std::strtoull(v.c_str(), nullptr, 10));
        }
        sum_recv_total += recv_total;
        sum_gaps += gaps;
        sum_reorder_events += reorder_events;
        if (latency_max >= worst_latency_max) {
            worst_latency_max = latency_max;
            worst_sub_id = sub_id;
        }
    }
    std::cout << "[run_market_sim][summary] total_recv=" << sum_recv_total
              << " total_gaps=" << sum_gaps
              << " total_reorder_events=" << sum_reorder_events
              << " worst_latency_max_us=" << worst_latency_max
              << " worst_subscriber_id=" << worst_sub_id << "\n";
    std::cout << "[run_market_sim] done\n";
    return 0;
}

