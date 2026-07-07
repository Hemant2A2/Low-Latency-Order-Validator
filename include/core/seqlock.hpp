#pragma once
#include <atomic>
#include <type_traits>

namespace hft::core {

/**
 * @brief A lock-free synchronization primitive for Single-Writer, Multi-Reader.
 * The writer NEVER blocks. Readers spin if they collide with a concurrent write.
 */
template <typename T>
class Seqlock {
    static_assert(std::is_trivially_copyable_v<T>, "Type T must be trivially copyable for Seqlock.");

public:
    Seqlock() : seq_(0) {}

    /**
     * @brief Writes data to the locked payload. The Writer never blocks.
     * @param data The new payload to store.
     */
    void store(const T& data) noexcept {
        // 1. Increment sequence to an ODD number to indicate a write is starting.
        // memory_order_relaxed is fine here because the fence handles the ordering.
        size_t current_seq = seq_.load(std::memory_order_relaxed);
        seq_.store(current_seq + 1, std::memory_order_relaxed);
        
        // 2. Hardware Fence: Guarantee the odd sequence number is visible to all cores
        // BEFORE the payload data is actually modified.
        std::atomic_thread_fence(std::memory_order_release);

        // 3. Write the payload
        payload_ = data;

        // 4. Hardware Fence: Guarantee the payload is fully written to memory
        // BEFORE the sequence number is updated again.
        std::atomic_thread_fence(std::memory_order_release);

        // 5. Increment sequence to an EVEN number to indicate write is complete.
        seq_.store(current_seq + 2, std::memory_order_relaxed);
    }

    /**
     * @brief Reads data from the payload. Spun-loops if a write is in progress.
     * @return A safe, un-torn copy of the payload.
     */
    T load() const noexcept {
        T local_copy;
        size_t seq0, seq1;

        do {
            // 1. Wait until sequence is EVEN (no write in progress).
            do {
                seq0 = seq_.load(std::memory_order_acquire);
            } while (seq0 & 1); // Bitwise AND with 1 checks if odd.

            // 2. Read the payload
            local_copy = payload_;

            // 3. Hardware Fence: Ensure payload read completes before checking sequence again.
            std::atomic_thread_fence(std::memory_order_acquire);

            // 4. Check sequence again.
            seq1 = seq_.load(std::memory_order_relaxed);
            
        // If the sequence changed while we were reading, our local_copy is corrupted. Loop again.
        } while (seq0 != seq1);

        return local_copy;
    }

private:
    // alignas(64) prevents false sharing between the sequence counter and the payload.
    alignas(64) std::atomic<size_t> seq_;
    alignas(64) T payload_;
};

} // namespace hft::core