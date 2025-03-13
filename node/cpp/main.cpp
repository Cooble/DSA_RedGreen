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
#include <fstream>
#include <vector>
#include <algorithm>

bool operator<(const in_addr &lhs, const in_addr &rhs)
{
    return lhs.s_addr < rhs.s_addr;
}


// CONFIGURATION
constexpr float RATIO = 0.33f; // RED GREEN ratio
constexpr int TIMEOUT = 3000; // in milliseconds
constexpr int HEARTBEAT_INTERVAL = 1; // in seconds
constexpr int HTTP_PORT = 8080;
constexpr int MULTICAST_PORT = 50000;
constexpr char* MULTICAST_ADDRESS = "239.255.255.250";

struct UdpClient
{
    std::map<in_addr, uint64_t> clientsA;
    std::map<in_addr, uint64_t> clientsB;
    
    // current state of the client
    // A - red, B - green
    char state = 'A';

    std::mutex clients_mutex;
    in_addr my_ip;
    const char *message = " heartbeat";
    int sockfd_;
    std::string multicast_address_;
    unsigned short port_;
    sockaddr_in multicast_addr_;

public:
    UdpClient(const std::string &multicast_address, unsigned short port)
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
        std::thread send_thread(&UdpClient::send_heartbeats_and_test_ratio, this);
        std::thread receive_thread(&UdpClient::receive_heartbeats, this);
        std::thread http_server_thread(&UdpClient::start_http_server, this);

        send_thread.join();
        receive_thread.join();
        http_server_thread.join();
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
        // only works on linux
        
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
                sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
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

        if (bind(sockfd_, (sockaddr *)&bind_addr, sizeof(bind_addr)) < 0)
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

    void send_heartbeats_and_test_ratio()
    {
        auto messageBuffer = (char *)malloc(sizeof(message) + 1);
        while (true)
        {
            strcpy(messageBuffer, message);
            messageBuffer[0] = state;

            int result = sendto(sockfd_, messageBuffer, strlen(messageBuffer), 0, (struct sockaddr *)&multicast_addr_,
                                sizeof(multicast_addr_));
            if (result < 0)
            {
                perror("sendto failed");
            }
            std::this_thread::sleep_for(std::chrono::seconds(HEARTBEAT_INTERVAL));
            test_ratio();
        }
    }

    void print_clients()
    {
        std::cout << "'\nState: ";
        std::cout << (state=='A' ? "🔴\n" : "🟢\n"); 

        // no need to print all clients, html server will do that
    //    std::lock_guard<std::mutex> lock(clients_mutex);
        //std::cout << "Clients A: " << clientsA.size() << std::endl;
       // for (const auto &c : clientsA)
       //     std::cout << "\tA: " << inet_ntoa(c.first) << std::endl;
     //   std::cout << "Clients B: " << clientsB.size() << std::endl;
    //    for (const auto &c : clientsB)
    //        std::cout << "\tB: " << inet_ntoa(c.first) << std::endl;
    }

    /**
     * Test if the ratio of clients is within the limits
     * If not, switch to the other state
     */
    void test_ratio()
    {
        print_clients();

        std::lock_guard<std::mutex> lock(clients_mutex);

        remove_stale_clients(clientsA);
        remove_stale_clients(clientsB);

        auto expectedA = (clientsA.size() + clientsB.size()) * RATIO;
        if (expectedA - (int)expectedA > .0f)//round up
            expectedA++;

        int a = (int)expectedA;
        int b = clientsA.size() + clientsB.size() - a;

        if (clientsA.size() > a && state == 'A'){
            if (has_lowest_ip(clientsA)){
                std::cout << "--->Switching to 🟢" << std::endl;
                state = 'B';
            }
        }
        else if (clientsB.size() > b && state == 'B'){
            if (has_lowest_ip(clientsB)){
                std::cout << "--->Switching to 🔴" << std::endl;
                state = 'A';
            }
        }
    }

    /**
     * Remove clients that haven't sent a heartbeat in TIMEOUT milliseconds
     */
    void remove_stale_clients(std::map<in_addr, uint64_t> &clients)
    {
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

        for (auto it = clients.begin(); it != clients.end();)
        {
            if (now - it->second > TIMEOUT)
            {
                std::cout << "Removing client " << inet_ntoa(it->first) << std::endl;
                it = clients.erase(it);
            }
            else
                ++it;
        }
    }

    bool has_lowest_ip(const std::map<in_addr, uint64_t> &clients)
    {
        for (const auto &c : clients)
            if (c.first < my_ip)
                return false;
        return true;
    }

    void receive_heartbeats()
    {
        char buffer[1024];
        sockaddr_in sender_addr;
        socklen_t sender_addr_len = sizeof(sender_addr);

        while (true)
        {
            int len = recvfrom(sockfd_, buffer, sizeof(buffer) - 1, 0, (struct sockaddr *)&sender_addr,
                               &sender_addr_len);
            if (len < 0){
                perror("recvfrom failed");
            }
            else
            {
                buffer[len] = '\0';
                update_client_entry(buffer, sender_addr.sin_addr);
            }
        }
    }

    void update_client_entry(const char *buffer, const in_addr &sender_addr)
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::system_clock::now().time_since_epoch())
                           .count();

        if (buffer[0] == 'A'){
            clientsA[sender_addr] = now;
            if (clientsB.find(sender_addr) != clientsB.end())
                clientsB.erase(sender_addr);
        }
        else if (buffer[0] == 'B'){
            clientsB[sender_addr] = now;
            if (clientsA.find(sender_addr) != clientsA.end())
                clientsA.erase(sender_addr);
        }
        else
            std::cout << "Unknown client type" << std::endl;
    }

    void start_http_server()
    {
        int server_fd, new_socket;
        struct sockaddr_in address;
        int addrlen = sizeof(address);

        if ((server_fd = socket(AF_INET, SOCK_STREAM, 0)) == 0)
        {
            perror("socket failed");
            exit(EXIT_FAILURE);
        }

        address.sin_family = AF_INET;
        address.sin_addr.s_addr = INADDR_ANY;
        address.sin_port = htons(HTTP_PORT);

        if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0)
        {
            perror("bind failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        if (listen(server_fd, 3) < 0)
        {
            perror("listen failed");
            close(server_fd);
            exit(EXIT_FAILURE);
        }

        while (true)
        {
            if ((new_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t *)&addrlen)) < 0)
            {
                perror("accept failed");
                close(server_fd);
                exit(EXIT_FAILURE);
            }

            std::string response = "HTTP/1.1 200 OK\nContent-Type: text/html\n\n";

            response += R"""(
<!DOCTYPE html>
<html lang="en">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>Cluster Status</title>
    <script>
    setInterval(function() {
        location.reload();
    }, 3000);
</script>
</head><body>
            )""";

            
            auto number_of_clients = clientsA.size() + clientsB.size();
            float ratio = 0.0f;
            if (number_of_clients > 0)
            ratio = (float)clientsA.size() / number_of_clients;

            response += "<h1>Cluster Status</h1>";
            response += "<p>🔴/🟢/Total: " + std::to_string(clientsA.size()) + "/" + std::to_string(clientsB.size()) + "/" + std::to_string(number_of_clients) + "</p>";
            response += "<p>Ratio: " + std::to_string(ratio) + "</p>";
            response += "<br>";
            response += "<p>State: ";
            response += state == 'A' ? "🔴" : "🟢";
            response += "\n<p>My IP: " + std::string(inet_ntoa(my_ip)) + "</p>";

         
            // fill vector with all keys
            std::vector<in_addr> keys;
            for (const auto &c : clientsA)
                keys.push_back(c.first);
            for (const auto &c : clientsB)
                keys.push_back(c.first);

            // sort keys
            std::sort(keys.begin(), keys.end(), std::less<in_addr>());

            response += "<table border='1'>";
            {std::lock_guard<std::mutex> lock(clients_mutex);
                for (const auto &k : keys)
                {
                    response += "<tr><td>" + std::string(inet_ntoa(k)) + "</td>";
                    if (clientsA.find(k) != clientsA.end())
                        response += "<td>🔴</td>";
                    else
                        response += "<td>🟢</td>";
                    response += "</tr>";
                }
            }
            response += "</table>";
            response += "</body></html>";

            send(new_socket, response.c_str(), response.size(), 0);
            close(new_socket);
        }
    }
};

int main()
{
    std::cout << "Starting UDP client..." << std::endl;
    UdpClient client(MULTICAST_ADDRESS,MULTICAST_PORT);
    client.start();
    return 0;
}
