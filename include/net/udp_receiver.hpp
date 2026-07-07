#pragma once

#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <unistd.h>
#include <span>
#include <stdexcept>

namespace hft::net {

class UDPBusyPollReceiver {
public:
    explicit UDPBusyPollReceiver(uint16_t port) {
        fd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to create UDP socket");
        }

        // CRITICAL: Configure the socket for non-blocking I/O.
        // This is the foundation of busy-polling.
        int flags = fcntl(fd_, F_GETFL, 0);
        fcntl(fd_, F_SETFL, flags | O_NONBLOCK);

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
            close(fd_);
            throw std::runtime_error("Failed to bind UDP socket");
        }
    }

    ~UDPBusyPollReceiver() {
        if (fd_ >= 0) close(fd_);
    }

    // Delete copy semantics to prevent accidental dual-binding of the socket file descriptor.
    UDPBusyPollReceiver(const UDPBusyPollReceiver&) = delete;
    UDPBusyPollReceiver& operator=(const UDPBusyPollReceiver&) = delete;

    /**
     * @brief Traps the CPU in an infinite spin-loop until a packet is physically available.
     * @param buffer A pre-allocated memory chunk to drop the payload into.
     * @return The number of bytes received.
     */
    inline size_t receive(std::span<uint8_t> buffer) noexcept {
        while (true) {
            // MSG_DONTWAIT acts as a secondary defense to guarantee the thread never sleeps.
            ssize_t bytes = recv(fd_, buffer.data(), buffer.size(), MSG_DONTWAIT);
            
            // [[likely]] because once a packet arrives, we want the subsequent math to 
            // already be loaded in the CPU instruction pipeline.
            if (bytes > 0) [[likely]] {
                return static_cast<size_t>(bytes);
            }
            
            // If bytes == -1 (and errno is EAGAIN), it means no data is ready.
            // We do absolutely nothing. The loop instantly restarts, burning CPU cycles
            // but guaranteeing single-digit nanosecond reaction time when the packet lands.
        }
    }

private:
    int fd_;
};

} // namespace hft::net