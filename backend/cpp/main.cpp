#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <map>
#include <mutex>
#include <ifaddrs.h>

bool operator<(const in_addr& lhs, const in_addr& rhs)
{
    return lhs.s_addr < rhs.s_addr;
}

class UdpClient
{
    std::map<in_addr, uint64_t> clientsA;
    std::map<in_addr, uint64_t> clientsB;
    float ratio = 0.66f;
    char state = 'A';
    std::mutex clients_mutex;
    in_addr my_ip;
    const char* message = " heartbeat";
    int sockfd_;
    std::string multicast_address_;
    unsigned short port_;
    sockaddr_in multicast_addr_;

public:
    UdpClient(const std::string& multicast_address, unsigned short port)
        : multicast_address_(multicast_address), port_(port)
    {
        create_socket();
        get_my_ip();
        join_multicast_group();
        bind_socket();
        setup_multicast_address();
    }

    ~UdpClient()
    {
        leave_multicast_group();
        close(sockfd_);
    }

    void start()
    {
        std::thread send_thread(&UdpClient::send_heartbeat, this);
        std::thread receive_thread(&UdpClient::receive_packets, this);

        send_thread.join();
        receive_thread.join();
    }

private:
    void create_socket()
    {
        sockfd_ = socket(AF_INET, SOCK_DGRAM, 0);
        if (sockfd_ < 0)
        {
            perror("socket creation failed");
            exit(EXIT_FAILURE);
        }
    }

    void get_my_ip()
    {
        struct ifaddrs *ifaddr, *ifa;
        if (getifaddrs(&ifaddr) == -1)
        {
            perror("getifaddrs");
            exit(EXIT_FAILURE);
        }
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
        {
            if (ifa->ifa_addr == NULL)
                continue;
            if (ifa->ifa_addr->sa_family == AF_INET)
            {
                sockaddr_in* addr = (struct sockaddr_in*)ifa->ifa_addr;
                if (addr->sin_addr.s_addr != htonl(INADDR_LOOPBACK))
                {
                    my_ip = addr->sin_addr;
                    break;
                }
            }
        }
        freeifaddrs(ifaddr);
        std::cout << "My IP: " << inet_ntoa(my_ip) << std::endl;
    }

    void join_multicast_group()
    {
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr(multicast_address_.c_str());
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        if (setsockopt(sockfd_, IPPROTO_IP, IP_ADD_MEMBERSHIP, &mreq, sizeof(mreq)) < 0)
        {
            perror("setsockopt (IP_ADD_MEMBERSHIP) failed");
            close(sockfd_);
            exit(EXIT_FAILURE);
        }
    }

    void leave_multicast_group()
    {
        struct ip_mreq mreq;
        mreq.imr_multiaddr.s_addr = inet_addr(multicast_address_.c_str());
        mreq.imr_interface.s_addr = htonl(INADDR_ANY);
        setsockopt(sockfd_, IPPROTO_IP, IP_DROP_MEMBERSHIP, &mreq, sizeof(mreq));
    }

    void bind_socket()
    {
        sockaddr_in bind_addr;
        memset(&bind_addr, 0, sizeof(bind_addr));
        bind_addr.sin_family = AF_INET;
        bind_addr.sin_port = htons(port_);
        bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);

        if (bind(sockfd_, (sockaddr*)&bind_addr, sizeof(bind_addr)) < 0)
        {
            perror("bind failed");
            close(sockfd_);
            exit(EXIT_FAILURE);
        }
    }

    void setup_multicast_address()
    {
        memset(&multicast_addr_, 0, sizeof(multicast_addr_));
        multicast_addr_.sin_family = AF_INET;
        multicast_addr_.sin_port = htons(port_);
        multicast_addr_.sin_addr.s_addr = inet_addr(multicast_address_.c_str());
    }

    void send_heartbeat()
    {
        auto messageBuffer = (char*)malloc(sizeof(message) + 1);
        while (true)
        {
            strcpy(messageBuffer, message);
            messageBuffer[0] = state;

            int result = sendto(sockfd_, messageBuffer, strlen(messageBuffer), 0, (struct sockaddr*)&multicast_addr_,
                                sizeof(multicast_addr_));
            if (result < 0)
            {
                perror("sendto failed");
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
            test_ratio();
        }
    }

    void print_clients()
    {
        std::cout << "\n\nState: " << state << std::endl;
        std::lock_guard<std::mutex> lock(clients_mutex);
        std::cout << "Clients A: " << clientsA.size() << std::endl;
        for (const auto& c : clientsA)
            std::cout << "\tA: " << inet_ntoa(c.first) << std::endl;
        std::cout << "Clients B: " << clientsB.size() << std::endl;
        for (const auto& c : clientsB)
            std::cout << "\tB: " << inet_ntoa(c.first) << std::endl;
    }

    void test_ratio()
    {
        print_clients();

        std::lock_guard<std::mutex> lock(clients_mutex);
       
        remove_stale_clients(clientsA);
        remove_stale_clients(clientsB);

        auto expectedA = (clientsA.size() + clientsB.size()) * ratio;
        auto expectedB = (clientsA.size() + clientsB.size()) * (1 - ratio);

        if (clientsA.size() > expectedA && state == 'A' && (clientsB.size()+1) <= expectedB)
        {
            if (has_lowest_ip(clientsA))
            {
                std::cout << "Switching to B" << std::endl;
                state = 'B';
            }
        }
        else if (clientsB.size() > expectedB && state == 'B' && (clientsA.size()+1) <= expectedA)
        {
            if (has_lowest_ip(clientsB))
            {
                std::cout << "Switching to A" << std::endl;
                state = 'A';
            }
        }
    }

    void remove_stale_clients(std::map<in_addr, uint64_t>& clients)
    {
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        for (auto it = clients.begin(); it != clients.end();)
        {
            if (now - it->second > 5000){
                std::cout << "Removing client " << inet_ntoa(it->first) << std::endl;
                it = clients.erase(it);
            }
            else
                ++it;
        }
    }

    bool has_lowest_ip(const std::map<in_addr, uint64_t>& clients)
    {
        for (const auto& c : clients)
            if (c.first < my_ip)
                return false;
        return true;
    }

    void receive_packets()
    {
        char buffer[1024];
        sockaddr_in sender_addr;
        socklen_t sender_addr_len = sizeof(sender_addr);

        while (true)
        {
            int len = recvfrom(sockfd_, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&sender_addr,
                               &sender_addr_len);
            if (len < 0)
            {
                perror("recvfrom failed");
            }
            else
            {
                buffer[len] = '\0';
                update_clients(buffer, sender_addr.sin_addr);
            }
        }
    }

    void update_clients(const char* buffer, const in_addr& sender_addr)
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();

        if (buffer[0] == 'A')
            clientsA[sender_addr] = now;
        else if (buffer[0] == 'B')
            clientsB[sender_addr] = now;
        else
            std::cout << "Unknown client type" << std::endl;
    }
};

int main()
{
    std::cout << "Starting UDP client..." << std::endl;
    UdpClient client("239.255.255.250", 50000); // Example multicast address
    client.start();
    return 0;
}
