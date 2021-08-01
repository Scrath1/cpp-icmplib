﻿#include <iostream>
#include "icmplib.h"

int main()
{
    std::cout << "Ping 8.8.8.8 ";
    try {
        std::cout << icmplib::Ping("8.8.8.8") << "ms" << std::endl;
    } catch (std::exception &e) {
        std::cout << "failure" << std::endl;
        return 1;
    }
    return 0;
}