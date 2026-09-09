#pragma once
#include "subsystems/net_driver.hpp"

namespace zq::net {
class ReliableChannel {
public:
    ReliableChannel() = default;
    bool Send(uint32_t sequence, const void* data, size_t length);
    bool Receive(uint32_t& sequence, void* buffer, size_t max_length);
    bool IsOpen() const { return true; }
};
}
