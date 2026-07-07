#pragma once

#include <cstdint>

#if defined(__APPLE__) && defined(__aarch64__)
    // Apple Silicon requires mach/mach_time.h for absolute time conversions, 
    // but we will read the ARM register directly for the lowest latency ticks.
    #include <mach/mach_time.h>
#elif defined(__x86_64__) || defined(_M_X64)
    #include <x86intrin.h>
#else
    #include <chrono>
#endif

namespace hft::utils {

class HardwareTimer {
public:
    /**
     * @brief Reads the hardware time-stamp counter with near-zero overhead.
     * @return The current tick count directly from the CPU register.
     */
    static inline uint64_t rdtsc() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        // x86_64: Read Time-Stamp Counter. 
        // Note: For strict instruction serialization in real benchmarks, 
        // you would use __rdtscp() or an lfence before __rdtsc().
        return __rdtsc();

#elif defined(__APPLE__) && defined(__aarch64__)
        // Apple Silicon (ARM64): Read the generic timer virtual count register.
        // This is accessible from userspace and avoids any syscall overhead.
        uint64_t val;
        __asm__ volatile("mrs %0, cntvct_el0" : "=r"(val));
        return val;

#else
        // Fallback for unsupported architectures (will incur OS overhead).
        return static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count()
        );
#endif
    }
};

} // namespace hft::utils