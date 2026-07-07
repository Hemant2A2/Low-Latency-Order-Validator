#pragma once

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <array>
#include <stdexcept>

namespace hft::core {

/**
 * @brief Lock-Free Single-Producer Single-Consumer (SPSC) Ring Buffer.
 * * @tparam T The datatype to store. Must be trivially copyable for zero-overhead.
 * @tparam Capacity The maximum size of the queue. MUST be a power of 2 for bitwise masking.
 */
template <typename T, size_t Capacity>
class SPSCQueue {
    // Compile-time assertions to guarantee hardware sympathy.
    static_assert(std::is_trivially_copyable_v<T>, "Type T must be trivially copyable for HFT.");
    static_assert((Capacity != 0) && ((Capacity & (Capacity - 1)) == 0), "Capacity must be a power of 2.");

public:
    SPSCQueue() : head_(0), tail_(0), cached_head_(0), cached_tail_(0) {}

    // Delete copy constructors to prevent accidental expensive copies.
    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    /**
     * @brief Pushes an item into the queue. Executed strictly by the PRODUCER thread.
     * @param item The data to insert.
     * @return true if successful, false if the queue is full.
     */
    bool push(const T& item) noexcept {
        const size_t current_tail = tail_.load(std::memory_order_relaxed);
        const size_t next_tail = (current_tail + 1) & Mask;

        // Check if full. Use the local cached head to avoid an expensive atomic load
        // from the consumer's cache line unless absolutely necessary.
        if (next_tail == cached_head_) {
            // Queue appears full. Fetch the true atomic head from the consumer.
            cached_head_ = head_.load(std::memory_order_acquire);
            if (next_tail == cached_head_) {
                return false; // Truly full. Drop the packet.
            }
        }

        // Copy the data directly into the pre-allocated array.
        data_[current_tail] = item;

        // Publish the write to the consumer. memory_order_release guarantees 
        // the data is written to memory BEFORE the tail pointer is updated.
        tail_.store(next_tail, std::memory_order_release);
        return true;
    }

    /**
     * @brief Pops an item from the queue. Executed strictly by the CONSUMER thread.
     * @param out_item Reference to populate with the popped data.
     * @return true if data was popped, false if the queue is empty.
     */
    bool pop(T& out_item) noexcept {
        const size_t current_head = head_.load(std::memory_order_relaxed);

        // Check if empty. Use the local cached tail first.
        if (current_head == cached_tail_) {
            // Queue appears empty. Fetch the true atomic tail from the producer.
            // memory_order_acquire guarantees we see the memory the producer wrote.
            cached_tail_ = tail_.load(std::memory_order_acquire);
            if (current_head == cached_tail_) {
                return false; // Truly empty. Back to busy-polling.
            }
        }

        // Extract the data.
        out_item = data_[current_head];

        // Advance the head pointer and publish to the producer.
        head_.store((current_head + 1) & Mask, std::memory_order_release);
        return true;
    }

private:
    static constexpr size_t Mask = Capacity - 1;

    // ------------------------------------------------------------------------
    // CACHE LINE 1: Producer's memory domain.
    // alignas(64) forces this exact variable to start on a fresh CPU cache line.
    // ------------------------------------------------------------------------
    alignas(64) std::atomic<size_t> tail_;
    size_t cached_head_; // Accessed only by Producer, safe to keep on Producer cache line.

    // ------------------------------------------------------------------------
    // CACHE LINE 2: Consumer's memory domain.
    // ------------------------------------------------------------------------
    alignas(64) std::atomic<size_t> head_;
    size_t cached_tail_; // Accessed only by Consumer, safe to keep on Consumer cache line.

    // ------------------------------------------------------------------------
    // CACHE LINE 3: The actual data payload.
    // ------------------------------------------------------------------------
    alignas(64) std::array<T, Capacity> data_;
};

} // namespace hft::core