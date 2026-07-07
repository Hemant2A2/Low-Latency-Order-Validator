#include <iostream>
#include <thread>
#include <vector>
#include <array>
#include <fstream>
#include <pthread.h>

#if defined(__APPLE__)
    #include <mach/mach.h>
    #include <mach/thread_policy.h>
#endif

#include "../include/utils/huge_page_allocator.hpp"
#include "../include/utils/rdtsc_timer.hpp"
#include "../include/core/spsc_queue.hpp"
#include "../include/net/zero_copy_parser.hpp"
#include "../include/net/udp_receiver.hpp"
#include "../include/trading/risk_engine.hpp"
#include "../include/utils/memory_pool.hpp"
#include "../include/trading/rules.hpp"

using namespace hft;

// Global flag to shut down the dashboard thread cleanly
std::atomic<bool> simulation_running{true};

// Pin a thread to a specific CPU core to prevent OS scheduler migrations.
void pin_thread_to_core(int core_id) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#elif defined(__APPLE__)
    // macOS does not support strict core pinning like Linux.
    // We use thread affinity policy to hint to the macOS Mach kernel 
    // that these threads should be isolated on performance cores.
    thread_affinity_policy_data_t policy = { core_id };
    thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_AFFINITY_POLICY,
                      (thread_policy_t)&policy, THREAD_AFFINITY_POLICY_COUNT);
#endif
}

int main() {
    std::cout << "[SYSTEM] Booting Micro-Latency Order Validator..." << std::endl;

    // 0. Allocate a massive 50MB Arena before trading begins (The Cold Path)
    utils::MemoryPool arena(50 * 1024 * 1024);

    constexpr size_t QUEUE_CAPACITY = 1024;
    constexpr size_t TOTAL_PACKETS = 100000; // We will simulate 100k packets

    // 1. Initialize the Lock-Free Ring Buffer on the heap (to avoid stack overflow)
    auto queue = std::make_unique<core::SPSCQueue<core::OrderMessage, QUEUE_CAPACITY>>();

    // 2. SWAP STD::VECTOR FOR THE HUGE PAGE BUFFER
    // This instantly asks the OS for contiguous 2MB pages and pre-faults them.
    utils::HugePageBuffer<uint64_t> latencies(TOTAL_PACKETS);

    // trading::StatefulRiskEngine risk_engine(&arena);
    trading::StatefulRiskEngine<trading::FatFingerRule, trading::PositionLimitRule> 
        risk_engine(&arena, trading::FatFingerRule{}, trading::PositionLimitRule{});

    // ========================================================================
    // DASHBOARD THREAD (The Seqlock Reader)
    // ========================================================================
    std::thread dashboard_thread([&]() {
        pin_thread_to_core(3); // Pin to Core 3

        while (simulation_running.load(std::memory_order_relaxed)) {
            // Lock-free read of the complete struct
            trading::RiskMetrics live_metrics = risk_engine.get_metrics_lock().load();
            
            // Only print if there is activity to avoid console spam at boot
            if (live_metrics.total_orders_processed > 0) {
                std::cout << "\r[DASHBOARD] Pos: " << live_metrics.current_position 
                          << " | VWAP: $" << live_metrics.current_vwap 
                          << " | Orders: " << live_metrics.total_orders_processed 
                          << " / " << TOTAL_PACKETS << std::flush;
            }
            
            // Standard OS sleep. The dashboard doesn't need to be microsecond-fast.
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        std::cout << "\n[DASHBOARD] Simulation complete. Shutting down UI." << std::endl;
    });

    // ========================================================================
    // CONSUMER THREAD (Risk Engine & Trading Logic)
    // ========================================================================
    std::thread consumer_thread([&]() {
        pin_thread_to_core(2); // Pin to Core 2

        core::OrderMessage msg;
        size_t processed = 0;

        while (processed < TOTAL_PACKETS) {
            // Busy-poll the lock-free queue
            if (queue->pop(msg)) {
                // 1. Record the order (Dynamic Allocation via Custom Pool)
                bool is_safe = risk_engine.validate_and_record(msg);
                
                // 2.Force the CPU to scan memory backward!
                // We ask it to calculate the VWAP of the last 1,000 orders.
                // Because of our __builtin_prefetch hint, the memory controller 
                // will pull this data into L2 cache, hiding the RAM latency.
                volatile double current_vwap = risk_engine.calculate_vwap(1000);
                
                // 3. Record completion time
                uint64_t end_tsc = utils::HardwareTimer::rdtsc();
                latencies[processed] = end_tsc - msg.timestamp_ns;
                processed++;
            }
        }
    });

    // ========================================================================
    // PRODUCER THREAD (Network Ingress & Zero-Copy Parser)
    // ========================================================================
    std::thread producer_thread([&]() {
        pin_thread_to_core(1); // Pin to Core 1

        net::UDPBusyPollReceiver receiver(8080);
        std::array<uint8_t, 256> rx_buffer;
        size_t sent = 0;

        while (sent < TOTAL_PACKETS) {
            // Busy-poll the network socket
            size_t bytes = receiver.receive(rx_buffer);
            
            // T0: The exact nanosecond the packet enters userspace
            uint64_t start_tsc = utils::HardwareTimer::rdtsc();

            // Zero-copy cast the memory
            const auto* parsed_msg = net::ZeroCopyParser::parse(std::span{rx_buffer.data(), bytes});
            
            if (parsed_msg) [[likely]] {
                // Copy struct onto the queue and stamp our start time into it
                core::OrderMessage local_msg = *parsed_msg;
                local_msg.timestamp_ns = start_tsc; 
                
                // Spin until there is room in the queue (backpressure)
                while (!queue->push(local_msg)) {} 
                sent++;
            }
        }
    });

    // Wait for the simulation to finish
    producer_thread.join();
    consumer_thread.join();
    dashboard_thread.join();

    std::cout << "[SYSTEM] Hot path completed. Writing telemetry to disk..." << std::endl;

    // ========================================================================
    // COLD PATH: Disk I/O & Telemetry Export
    // ========================================================================
    std::ofstream csv("latency_cycles.csv");
    csv << "PacketID,LatencyCycles\n";
    for (size_t i = 0; i < latencies.size(); ++i) {
        csv << i << "," << latencies[i] << "\n";
    }
    csv.close();

    std::cout << "[SYSTEM] Success. Data saved to latency_cycles.csv." << std::endl;
    return 0;
}