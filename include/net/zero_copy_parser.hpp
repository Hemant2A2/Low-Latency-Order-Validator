#pragma once
#include <span>
#include "../core/messages.hpp"

namespace hft::net {

class ZeroCopyParser {
public:
    /**
     * @brief Parses a binary buffer into an OrderMessage with absolute zero allocation.
     * @param buffer A non-owning view of the raw network bytes.
     * @return A pointer to the message directly inside the network buffer, or nullptr.
     */
    static inline const core::OrderMessage* parse(std::span<const uint8_t> buffer) noexcept {
        // C++20 [[unlikely]] attribute guides the CPU branch predictor.
        // We tell the CPU to optimize strictly for the case where the packet is whole.
        if (buffer.size() < sizeof(core::OrderMessage)) [[unlikely]] {
            return nullptr; 
        }

        // The Zero-Copy Magic: 
        // We do not copy the data. We simply tell the compiler to put on "struct glasses"
        // and look at the raw memory address as if it were already an OrderMessage.
        // Note: x86_64 and Apple Silicon handle unaligned reads at the hardware level natively.
        return reinterpret_cast<const core::OrderMessage*>(buffer.data());
    }
};

} // namespace hft::net