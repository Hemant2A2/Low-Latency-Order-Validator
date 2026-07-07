#pragma once
#include <vector>
#include <tuple>
#include <cmath>
#include "../core/messages.hpp"
#include "../utils/memory_pool.hpp"
#include "../core/seqlock.hpp"

namespace hft::trading {

// Metrics we want to broadcast to the dashboard
struct RiskMetrics {
    int64_t current_position = 0;
    double current_vwap = 0.0;
    uint64_t total_orders_processed = 0;
};

template <typename... Rules>
class StatefulRiskEngine {
public:
    static constexpr uint32_t MAX_POSITION = 1000000;

    // Use a std::vector, but explicitly inject our CustomAllocator
    using OrderHistory = std::vector<core::OrderMessage, utils::CustomAllocator<core::OrderMessage>>;

    // explicit StatefulRiskEngine(utils::MemoryPool* pool) 
    //     // Pass the memory pool into the vector's constructor
    //     : accepted_orders_(utils::CustomAllocator<core::OrderMessage>(pool)), 
    //       current_position_(0) {}

    // STORE RULES IN A TUPLE (Resolves entirely at compile-time)
    explicit StatefulRiskEngine(utils::MemoryPool* pool, Rules... rules) 
        : accepted_orders_(utils::CustomAllocator<core::OrderMessage>(pool)), 
          current_position_(0),
          rules_(std::make_tuple(rules...)) {}

    // inline bool validate_and_record(const core::OrderMessage& msg) noexcept {
    //     // If the new order breaches our cumulative position limit, reject it
    //     if (current_position_ + msg.quantity > MAX_POSITION) {
    //         return false;
    //     }

    //     // Accept the order and track it.
    //     // Because of our CustomAllocator, when this vector exceeds its capacity 
    //     // and needs to double in size, it will instantly carve memory out of 
    //     // our pre-allocated memory pool, avoiding a malloc() call.
    //     accepted_orders_.push_back(msg);
    //     current_position_ += msg.quantity;
    //     return true;
    // }

    inline bool validate_and_record(const core::OrderMessage& msg) noexcept {
        
        // C++17 Fold Expressions + std::apply
        // The compiler expands this tuple into a flat, inline boolean expression:
        // bool is_safe = (rule1.validate() && rule2.validate() && ...);
        // There is absolutely no loop, no pointer chasing, and no virtual overhead.
        bool is_safe = std::apply([&](const auto&... rule) {
            return (... && rule.validate(msg, current_position_)); 
        }, rules_);

        if (!is_safe) {
            return false;
        }

        // Apply state updates if safe
        int64_t order_impact = (msg.side == 1) ? msg.quantity : -static_cast<int64_t>(msg.quantity);
        accepted_orders_.push_back(msg);
        current_position_ += order_impact;
        
        return true;
    }

    /**
     * @brief Calculates the Volume Weighted Average Price (VWAP) over a lookback window.
     * Utilizes explicit software prefetching to hide RAM latency during the backward scan.
     * * @param lookback_window The number of recent orders to include in the calculation.
     * @return The VWAP, or 0.0 if there is no history.
     */
    inline double calculate_vwap(size_t lookback_window) const noexcept {
        size_t total_orders = accepted_orders_.size();
        if (total_orders == 0) return 0.0;

        size_t limit = (lookback_window > total_orders) ? total_orders : lookback_window;
        
        double cumulative_pv = 0.0;
        uint64_t cumulative_volume = 0;

        // PREFETCH PHYSICS:
        // Cache lines are 64 bytes. Our OrderMessage is 21 bytes.
        // A distance of 16 elements means we are asking the memory controller to look 
        // ~336 bytes (roughly 5 cache lines) ahead of our current loop index.
        constexpr size_t PREFETCH_DISTANCE = 16; 

        // Scan backward from the most recent order
        for (size_t i = 1; i <= limit; ++i) {
            size_t current_idx = total_orders - i;

            // 1. Issue the Software Prefetch Hint
            // We must guard this to prevent out-of-bounds memory access.
            if (current_idx >= PREFETCH_DISTANCE) {
                // Parameters: (Address, Read/Write, Locality)
                // rw = 0: We are only reading the data, not writing.
                // locality = 1: Stage the data in the L2 cache, NOT the L1 cache. 
                // We do this to prevent our massive historical scan from evicting the 
                // highly critical hot-path variables currently sitting in L1.
                __builtin_prefetch(&accepted_orders_[current_idx - PREFETCH_DISTANCE], 0, 1);
            }

            // 2. Perform the actual math
            // Because we prefetched this address 16 iterations ago, the data is 
            // already waiting in the L2 cache (fetching takes ~10 cycles instead of ~300).
            const auto& order = accepted_orders_[current_idx];
            
            cumulative_pv += static_cast<double>(order.price) * order.quantity;
            cumulative_volume += order.quantity;
        }

        return cumulative_volume > 0 ? (cumulative_pv / cumulative_volume) : 0.0;
    }

    // The safe broadcast function for the trading thread to call.
    inline void publish_metrics() noexcept {
        RiskMetrics metrics;
        metrics.current_position = current_position_;
        metrics.total_orders_processed = accepted_orders_.size();
        
        // We only calculate VWAP if we have recent orders
        if (!accepted_orders_.empty()) {
            metrics.current_vwap = calculate_vwap(1000);
        }

        // Store the struct into the lock-free Seqlock
        public_metrics_.store(metrics);
    }

    // A reference to the Seqlock so the Dashboard thread can read it.
    const core::Seqlock<RiskMetrics>& get_metrics_lock() const noexcept {
        return public_metrics_;
    }

private:
    OrderHistory accepted_orders_;
    uint64_t current_position_;
    std::tuple<Rules...> rules_;
    // The physical memory barrier for broadcasting
    core::Seqlock<RiskMetrics> public_metrics_;
};

} // namespace hft::trading