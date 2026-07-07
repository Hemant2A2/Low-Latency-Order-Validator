#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <random>

// Include the exact same packed struct our receiver uses
#include "../include/core/messages.hpp"

using namespace hft;

int main() {
    std::cout << "[EXCHANGE] Booting Exchange Simulator..." << std::endl;

    // 1. Create a standard blocking UDP socket
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        std::cerr << "[EXCHANGE] Failed to create socket." << std::endl;
        return 1;
    }

    // 2. Configure the destination address (Localhost, Port 8080)
    sockaddr_in dest_addr{};
    dest_addr.sin_family = AF_INET;
    dest_addr.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &dest_addr.sin_addr);

    // 3. Setup a fast random number generator for dummy data
    std::mt19937 rng(42); // Fixed seed for deterministic testing
    std::uniform_int_distribution<uint32_t> price_dist(10000, 50000); // $100.00 to $500.00
    std::uniform_int_distribution<uint32_t> qty_dist(1, 10); // 1 to 10 shares per order
    std::uniform_int_distribution<uint16_t> side_dist(1, 2);          // 1 = Buy, 2 = Sell

    constexpr size_t TOTAL_PACKETS = 100000;
    std::cout << "[EXCHANGE] Blasting " << TOTAL_PACKETS << " binary packets to 127.0.0.1:8080..." << std::endl;

    for (size_t i = 0; i < TOTAL_PACKETS; ++i) {
        // Create the message directly on the stack
        core::OrderMessage msg{};
        msg.timestamp_ns = 0; // The receiver will stamp this
        msg.instrument_id = 1; // e.g., AAPL
        msg.price = price_dist(rng);
        msg.quantity = qty_dist(rng);
        msg.side = static_cast<uint8_t>(side_dist(rng));

        // Transmit the raw struct memory directly over the wire.
        // This is exactly how C++ structs become network binary payloads.
        ssize_t sent = sendto(sock, &msg, sizeof(core::OrderMessage), 0,
                              reinterpret_cast<struct sockaddr*>(&dest_addr),
                              sizeof(dest_addr));

        if (sent < 0) {
            std::cerr << "[EXCHANGE] Socket send error on packet " << i << std::endl;
            break;
        }

        // Optional: A micro-sleep to prevent completely flooding the local loopback interface
        // If your receiver is properly busy-polling, you can comment this out to stress-test it.
        // usleep(1); 
    }

    std::cout << "[EXCHANGE] Transmission complete. Closing market." << std::endl;
    close(sock);
    return 0;
}