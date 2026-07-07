#pragma once
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace hft::utils {

// 1. The Arena / Bump Allocator
class MemoryPool {
public:
    explicit MemoryPool(size_t capacity) : capacity_(capacity), offset_(0) {
        // Pre-allocate the massive block of raw bytes upfront
        buffer_ = new uint8_t[capacity_];
    }

    ~MemoryPool() {
        delete[] buffer_;
    }

    // Hot-path memory allocation: No OS calls, just pointer math.
    void* allocate(size_t bytes) {
        // Align to 8-byte boundaries for CPU efficiency
        size_t aligned_bytes = (bytes + 7) & ~7;

        if (offset_ + aligned_bytes > capacity_) {
            throw std::bad_alloc(); // Pool is exhausted
        }

        void* ptr = buffer_ + offset_;
        offset_ += aligned_bytes;
        return ptr;
    }

    // In a pure HFT bump allocator, we rarely deallocate individual objects.
    // We just reset the offset to 0 at the end of the trading day.
    void deallocate(void* p, size_t bytes) noexcept {
        // Linear allocators cannot reclaim fragmented memory easily, 
        // so we intentionally do nothing here to keep it blazing fast.
    }

    void reset() noexcept { offset_ = 0; }
    size_t used() const noexcept { return offset_; }

private:
    uint8_t* buffer_;
    size_t capacity_;
    size_t offset_;
};

// 2. The Custom STL Allocator Bridge
template <typename T>
class CustomAllocator {
public:
    using value_type = T;

    // The allocator holds a pointer to your shared memory pool
    MemoryPool* pool = nullptr;

    CustomAllocator() noexcept = default;
    explicit CustomAllocator(MemoryPool* p) noexcept : pool(p) {}

    // Required template magic to let std::vector rebind the allocator
    // to internal node types if necessary.
    template <typename U>
    CustomAllocator(const CustomAllocator<U>& other) noexcept : pool(other.pool) {}

    template <typename U>
    struct rebind {
        using other = CustomAllocator<U>;
    };

    T* allocate(size_t n) {
        if (!pool) throw std::bad_alloc();
        return static_cast<T*>(pool->allocate(n * sizeof(T)));
    }

    void deallocate(T* p, size_t n) noexcept {
        if (pool) pool->deallocate(p, n * sizeof(T));
    }
};

// Required for C++ allocator completeness: Check if two allocators share the same pool
template <typename T, typename U>
inline bool operator==(const CustomAllocator<T>& a, const CustomAllocator<U>& b) {
    return a.pool == b.pool;
}

template <typename T, typename U>
inline bool operator!=(const CustomAllocator<T>& a, const CustomAllocator<U>& b) {
    return !(a == b);
}

} // namespace hft::utils