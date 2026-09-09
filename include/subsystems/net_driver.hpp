#pragma once

#include "subsystems/net_addr.hpp"
#include <functional>

namespace zq::net {

// UDP socket abstraction
class UDPSocket {
public:
    virtual ~UDPSocket() = default;
    virtual bool Bind(const NetAddr& addr) = 0;
    virtual bool Send(const NetAddr& addr, const void* data, size_t length) = 0;
    virtual size_t Receive(NetAddr& from, void* buffer, size_t max_length) = 0;
    virtual bool IsOpen() const = 0;
    virtual void Close() = 0;
};

UDPSocket* CreateSocket();
void DestroySocket(UDPSocket* socket);

} // namespace zq::net
