#ifndef ICMPLIB_PING_DATA_SIZE
#define ICMPLIB_PING_DATA_SIZE 64
#endif

#include <chrono>
#include <string>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <climits>
#endif

#define ICMPLIB_ICMP_ECHO_RESPONSE 0
#define ICMPLIB_ICMP_DESTINATION_UNREACHABLE 3
#define ICMPLIB_ICMP_ECHO_REQUEST 8
#define ICMPLIB_ICMP_TIME_EXCEEDED 11

#define ICMPLIB_IPV4_ADDRESS_SIZE 16
#define ICMPLIB_IPV4_HEADER_SIZE 20
#define ICMPLIB_IPV4_TTL_OFFSET 8
#define ICMPLIB_RECV_BUFFER_SIZE 1024
#define ICMPLIB_ORIGINAL_DATA_SIZE ICMPLIB_IPV4_HEADER_SIZE + 8

#ifdef _WIN32
#define ICMPLIB_SOCKET SOCKET
#define ICMPLIB_SOCKADDR SOCKADDR
#define ICMPLIB_SOCKADDR_IN SOCKADDR_IN
#define ICMPLIB_SOCKETADDR_LENGTH int
#define ICMPLIB_SOCKET_ERROR SOCKET_ERROR
#define ICMPLIB_INETPTON InetPtonA
#define ICMPLIB_INETNTOP InetNtopA
#define ICMPLIB_CLOSESOCKET closesocket
#else
#define ICMPLIB_SOCKET int
#define ICMPLIB_SOCKADDR sockaddr
#define ICMPLIB_SOCKADDR_IN sockaddr_in
#define ICMPLIB_SOCKETADDR_LENGTH socklen_t
#define ICMPLIB_SOCKET_ERROR -1
#define ICMPLIB_INETPTON inet_pton
#define ICMPLIB_INETNTOP inet_ntop
#define ICMPLIB_CLOSESOCKET close
#endif

#if (defined _WIN32 && defined _MSC_VER)
#pragma comment(lib, "ws2_32.lib")
#endif

namespace icmplib {
#ifdef _WIN32
    class WinSock {
    public:
        WinSock(const WinSock &) = delete;
        WinSock(WinSock &&) = delete;
        virtual ~WinSock() {
            WSACleanup();
        }
        WinSock &operator=(const WinSock &) = delete;
        static WinSock &Initialize() {
            static WinSock instance;
            return instance;
        }
    private:
        WinSock() {
            WSADATA wsaData;
            int error = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (error != NO_ERROR) {
                throw std::runtime_error("Cannot initialize WinSock!");
            }
            if ((LOBYTE(wsaData.wVersion) != 2) || (HIBYTE(wsaData.wVersion) != 2)) {
                WSACleanup();
                throw std::runtime_error("Cannot initialize WinSock!");
            }
        }
    };
#endif
    class ICMPEcho {
    public:
        struct Result {
            enum class ResponseType {
                Success,
                Unreachable,
                TimeExceeded,
                Timeout,
                Unsupported,
                Failure
            } response;
            double interval;
            std::string ipv4;
            uint8_t code;
            uint8_t ttl;
        };
        ICMPEcho() = delete;
        ICMPEcho(const ICMPEcho &) = delete;
        ICMPEcho(ICMPEcho &&) = delete;
        ICMPEcho &operator=(const ICMPEcho &) = delete;
        static Result Execute(const std::string &ipv4, unsigned timeout = 60, uint8_t ttl = 255) {
#ifdef _WIN32
            WinSock::Initialize();
#endif

            Result result = { Result::ResponseType::Timeout, static_cast<double>(timeout), std::string(), 0, 0 };

            ICMPLIB_SOCKADDR_IN address;
            std::memset(&address, 0, sizeof(ICMPLIB_SOCKADDR_IN));
            address.sin_family = AF_INET;
            address.sin_port = htons(53);

            if (ICMPLIB_INETPTON(AF_INET, ipv4.c_str(), &address.sin_addr) <= 0) {
                return { Result::ResponseType::Failure, 0, std::string(), 0, 0 };
            }

            ICMPLIB_SOCKET sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
#ifdef _WIN32
            if (sock == INVALID_SOCKET) {
#else
            if (sock <= 0) {
#endif	
                throw std::runtime_error("Cannot initialize socket!");
            }

            if (setsockopt(sock, IPPROTO_IP, IP_TTL, reinterpret_cast<char *>(&ttl), sizeof(ttl)) == ICMPLIB_SOCKET_ERROR) {
                ICMPLIB_CLOSESOCKET(sock);
                return { Result::ResponseType::Failure, 0, std::string(), 0, 0 };
            }

#ifdef _WIN32
            unsigned long mode = 1;
            if (ioctlsocket(sock, FIONBIO, &mode) != NO_ERROR) {
#else
            int flags = fcntl(sock, F_GETFL, 0);
            if ((flags == -1) || fcntl(sock, F_SETFL, flags | O_NONBLOCK) == -1) {
#endif
                ICMPLIB_CLOSESOCKET(sock);
                return { Result::ResponseType::Failure, 0, std::string(), 0, 0 };
            }

            ICMPEchoMessage request;
            std::memset(&request, 0, sizeof(ICMPEchoMessage));
            request.id = rand() % USHRT_MAX;
            request.type = ICMPLIB_ICMP_ECHO_REQUEST;
            SetChecksum(request, sizeof(ICMPEchoMessage));
            int bytes = sendto(sock, reinterpret_cast<char *>(&request), sizeof(ICMPEchoMessage), 0, reinterpret_cast<ICMPLIB_SOCKADDR *>(&address), static_cast<ICMPLIB_SOCKETADDR_LENGTH>(sizeof(ICMPLIB_SOCKADDR_IN)));
            if (bytes == ICMPLIB_SOCKET_ERROR) {
                ICMPLIB_CLOSESOCKET(sock);
                return { Result::ResponseType::Failure, 0, std::string(), 0, 0 };
            }

            auto start = std::chrono::high_resolution_clock::now();

            while (true) {
                ICMPLIB_SOCKETADDR_LENGTH length = sizeof(ICMPLIB_SOCKADDR_IN);
                std::memset(&address, 0, sizeof(ICMPLIB_SOCKADDR_IN));
                char buffer[ICMPLIB_RECV_BUFFER_SIZE];
                bytes = recvfrom(sock, buffer, ICMPLIB_RECV_BUFFER_SIZE, 0, reinterpret_cast<ICMPLIB_SOCKADDR *>(&address), &length);
                auto end = std::chrono::high_resolution_clock::now();
                if (bytes <= 0) {
                    if (static_cast<unsigned>(std::chrono::duration_cast<std::chrono::seconds>(end - start).count()) > timeout) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::microseconds(1));
                    continue;
                }

                auto initPacket = [&](void *packet, unsigned long length) {
                    std::memset(packet, 0, length);
                    std::memcpy(packet, &buffer[ICMPLIB_IPV4_HEADER_SIZE], static_cast<long unsigned>(bytes) - ICMPLIB_IPV4_HEADER_SIZE > length ? length : static_cast<long unsigned>(bytes) - ICMPLIB_IPV4_HEADER_SIZE);
                };

                ICMPHeader header;
                initPacket(&header, sizeof(ICMPHeader));

                auto setResult = [&](Result::ResponseType response) {
                    result.response = response;
                    result.interval = static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(end - start).count()) / 1000.0;
                    char addressBuffer[ICMPLIB_IPV4_ADDRESS_SIZE + 1];
                    if (ICMPLIB_INETNTOP(AF_INET, &address.sin_addr, addressBuffer, ICMPLIB_IPV4_ADDRESS_SIZE + 1) != NULL) {
                        result.ipv4 = addressBuffer;
                    }
                    result.code = header.code;
                    result.ttl = buffer[ICMPLIB_IPV4_TTL_OFFSET];
                };

                ICMPEchoMessage response;
                ICMPRevertedMessage reverted;
                uint16_t checksum = header.checksum;
                switch (header.type) {
                case ICMPLIB_ICMP_ECHO_RESPONSE:
                    setResult(Result::ResponseType::Success);
                    initPacket(&response, sizeof(ICMPEchoMessage));
                    response.checksum = 0;
                    if ((checksum != SetChecksum(response, sizeof(ICMPEchoMessage))) || (request.id != response.id)) {
                        setResult(Result::ResponseType::Unsupported);
                    }                    
                    break;
                case ICMPLIB_ICMP_DESTINATION_UNREACHABLE:
                    setResult(Result::ResponseType::Unreachable);
                case ICMPLIB_ICMP_TIME_EXCEEDED:
                    if (result.response == Result::ResponseType::Timeout) {
                        setResult(Result::ResponseType::TimeExceeded);
                    }
                    initPacket(&reverted, sizeof(ICMPRevertedMessage));
                    reverted.checksum = 0;
                    if (checksum != SetChecksum(reverted, sizeof(ICMPRevertedMessage))) {
                        setResult(Result::ResponseType::Unsupported);
                    }
                    break;
                case ICMPLIB_ICMP_ECHO_REQUEST:
                    continue;
                default:
                    setResult(Result::ResponseType::Unsupported);
                }
                break;
            }

            ICMPLIB_CLOSESOCKET(sock);
            return result;
        }
    private:
        struct ICMPHeader {
            uint8_t type;
            uint8_t code;
            uint16_t checksum;
        };

        struct ICMPEchoMessage : ICMPHeader {
            uint16_t id;
            uint16_t seq;
            uint8_t data[ICMPLIB_PING_DATA_SIZE];
        };

        struct ICMPRevertedMessage : ICMPHeader {
            uint32_t unused;
            uint8_t data[ICMPLIB_ORIGINAL_DATA_SIZE];
        };

        static uint16_t SetChecksum(ICMPHeader &packet, unsigned long length) {
            uint16_t *element = reinterpret_cast<uint16_t *>(&packet);
            uint32_t sum = 0;
            for (; length > 1; length -= 2) {
                sum += *element++;
            }
            if (length > 0) {
                sum += *reinterpret_cast<uint8_t *>(element);
            }
            sum = (sum >> 16) + (sum & 0xFFFF);
            sum += (sum >> 16);
            packet.checksum = static_cast<uint16_t>(~sum);
            return packet.checksum;
        };
    };

    using PingResult = ICMPEcho::Result;
    using PingResponseType = ICMPEcho::Result::ResponseType;

    PingResult Ping(const std::string &ipv4, unsigned timeout = 60, uint8_t ttl = 255) {
        return ICMPEcho::Execute(ipv4, timeout, ttl);
    }
}