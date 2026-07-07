#pragma once

#include <sys/mman.h>
#include <cstddef>
#include <stdexcept>
#include <iostream>

#if defined(__APPLE__)
    #include <mach/vm_statistics.h>
#endif

namespace hft::utils {

template <typename T>
class HugePageBuffer {
public:
    explicit HugePageBuffer(size_t elements) : capacity_(elements) {
        size_t bytes = elements * sizeof(T);
        
        // Align the requested size to the nearest 2MB boundary
        constexpr size_t SUPERPAGE_SIZE = 2 * 1024 * 1024;
        alloc_size_ = (bytes + SUPERPAGE_SIZE - 1) & ~(SUPERPAGE_SIZE - 1);

#if defined(__APPLE__)
        // // macOS Superpage Allocation via Mach Virtual Memory Tags
        // int fd = VM_MAKE_TAG(VM_MEMORY_SUPERPAGE);
        // int flags = MAP_PRIVATE | MAP_ANON;
        // data_ = static_cast<T*>(mmap(nullptr, alloc_size_, PROT_READ | PROT_WRITE, flags, fd, 0));
        
        // macOS Superpage Allocation fallback
        int fd = -1;
        int flags = MAP_PRIVATE | MAP_ANON;
        data_ = static_cast<T*>(mmap(nullptr, alloc_size_, PROT_READ | PROT_WRITE, flags, fd, 0));
        
#elif defined(__linux__)
        // Linux Huge Page Allocation
        int flags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB;
        data_ = static_cast<T*>(mmap(nullptr, alloc_size_, PROT_READ | PROT_WRITE, flags, -1, 0));
#else
        // Fallback for unsupported OS
        data_ = MAP_FAILED;
#endif

        // Graceful Fallback: If OS rejects Huge Pages (e.g., RAM is too fragmented),
        // fallback to standard 4KB mmap allocation.
        if (data_ == MAP_FAILED) {
            std::cout << "[MEMORY] Superpage allocation failed. Falling back to 4KB pages.\n";
            data_ = static_cast<T*>(mmap(nullptr, alloc_size_, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0));
            if (data_ == MAP_FAILED) {
                throw std::bad_alloc();
            }
        } else {
            std::cout << "[MEMORY] Successfully allocated 2MB Superpages.\n";
        }

        // Memory Pre-faulting (Warmup):
        // Physically map the pages into RAM by writing to them before the hot path starts.
        for (size_t i = 0; i < capacity_; ++i) {
            data_[i] = T{};
        }
    }

    ~HugePageBuffer() {
        if (data_ != MAP_FAILED && data_ != nullptr) {
            munmap(data_, alloc_size_);
        }
    }

    // Delete copy semantics to prevent accidental massive memory duplication
    HugePageBuffer(const HugePageBuffer&) = delete;
    HugePageBuffer& operator=(const HugePageBuffer&) = delete;

    // Overload the [] operator so it acts exactly like a std::vector
    inline T& operator[](size_t index) noexcept {
        return data_[index];
    }

    inline const T& operator[](size_t index) const noexcept {
        return data_[index];
    }

    inline size_t size() const noexcept {
        return capacity_;
    }

private:
    T* data_;
    size_t capacity_;
    size_t alloc_size_;
};

} // namespace hft::utils