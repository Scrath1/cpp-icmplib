﻿#include <iostream>
#include "icmplib.h"

int main(int argc, char *argv[])
{
    std::string address = "8.8.8.8", resolved;
    if (argc > 1) { address = argv[1]; }
    try {
        if (!icmplib::AddressIPv4::IsCorrect(address)) {
            resolved = address; address = icmplib::AddressIPv4(address).ToString();
        }
    } catch (...) {
        std::cout << "Ping request could not find host " << address << ". Please check the name and try again." << std::endl;
        return 1;
    }

    int ret = 0;
    std::cout << "Pinging " << (resolved.empty() ? address : resolved + " [" + address + "]")
              << " with " << ICMPLIB_PING_DATA_SIZE << " bytes of data:" << std::endl;
    auto result = icmplib::Ping(address);
    switch (result.response) {
    case icmplib::PingResponseType::Failure:
        std::cout << "Network error." << std::endl;
        ret = 1;
        break;
    case icmplib::PingResponseType::Timeout:
        std::cout << "Request timed out." << std::endl;
        break;
    default:
        std::cout << "Reply from " << result.ipv4 << ": ";
        switch (result.response) {
        case icmplib::PingResponseType::Success:
            std::cout << "time=" << result.interval << " TTL=" << static_cast<unsigned>(result.ttl);
            break;
        case icmplib::PingResponseType::Unreachable:
            std::cout << "Destination unreachable.";
            break;
        case icmplib::PingResponseType::TimeExceeded:
            std::cout << "Time exceeded.";
            break;
        }
        std::cout << std::endl;
    }
    return ret;
}
