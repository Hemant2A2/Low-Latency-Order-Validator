#pragma once
#include <cstdint>

namespace hft::core {

// Force the compiler to use 1-byte alignment.
// This prevents the insertion of hidden padding bytes, ensuring our 
// C++ struct perfectly matches the contiguous binary layout of the network packet.
#pragma pack(push, 1)
struct OrderMessage {
    uint64_t timestamp_ns;   // 8 bytes
    uint32_t instrument_id;  // 4 bytes
    uint32_t price;          // 4 bytes
    uint32_t quantity;       // 4 bytes
    uint8_t  side;           // 1 byte (1 = Buy, 2 = Sell)
};
#pragma pack(pop) // Restore default compiler alignment

// Compile-time verification. If this fails, your struct will misalign with the network data.
static_assert(sizeof(OrderMessage) == 21, "OrderMessage must be exactly 21 bytes with zero padding");

} // namespace hft::core