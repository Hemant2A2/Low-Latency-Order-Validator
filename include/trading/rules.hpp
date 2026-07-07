#pragma once
#include <cmath>
#include "../core/messages.hpp"

namespace hft::trading {

// 1. THE CRTP BASE CLASS
// Instead of dynamic polymorphism (virtual functions), we use static polymorphism.
// The base class casts itself to the Derived type at compile-time.
template <typename Derived>
struct RiskRule {
    // The strict interface forced upon all rules
    inline bool validate(const core::OrderMessage& msg, int64_t current_pos) const noexcept {
        return static_cast<const Derived*>(this)->validate_impl(msg, current_pos);
    }
};

// 2. CONCRETE RULE 1: Fat Finger Check
struct FatFingerRule : public RiskRule<FatFingerRule> {
    static constexpr uint32_t MAX_ORDER_QTY = 50; // Generator sends 1-10, so 50 is safe

    inline bool validate_impl(const core::OrderMessage& msg, int64_t /*current_pos*/) const noexcept {
        return msg.quantity <= MAX_ORDER_QTY;
    }
};

// 3. CONCRETE RULE 2: Net Position Limit Check
struct PositionLimitRule : public RiskRule<PositionLimitRule> {
    static constexpr int64_t MAX_POSITION = 1000000;

    inline bool validate_impl(const core::OrderMessage& msg, int64_t current_pos) const noexcept {
        int64_t impact = (msg.side == 1) ? msg.quantity : -static_cast<int64_t>(msg.quantity);
        return std::abs(current_pos + impact) <= MAX_POSITION;
    }
};

} // namespace hft::trading