#pragma once

#include <cstdint>
#include <array>
#include "core/container/string.hpp"

namespace zq::net {

// Network address (IP:port)
class NetAddr {
public:
    NetAddr() : port_(0) { address_.fill(0); }
    
    struct IPv4 {
        std::array<uint8_t, 4> address;
        uint16_t port;
    };
    
    void SetIPv4(const std::array<uint8_t, 4>& addr, uint16_t port);
    void SetLocalhost(uint16_t port);
    void SetBroadcast(uint16_t port);
    
    const std::array<uint8_t, 4>& Address() const;
    uint16_t Port() const;
    
    String ToString() const;
    bool operator==(const NetAddr& other) const;
    bool operator!=(const NetAddr& other) const;
    
    bool IsLocalhost() const;
    bool IsBroadcast() const;
    bool IsUnspecified() const;
    
private:
    std::array<uint8_t, 4> address_;
    uint16_t port_;
};

} // namespace zq::net
