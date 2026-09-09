#include "subsystems/net_addr.hpp"
#include "core/container/string.hpp"
#include <cstring>
#include <sstream>

namespace zq::net {

void NetAddr::SetIPv4(const std::array<uint8_t, 4>& addr, uint16_t port) {
    address_ = addr;
    port_ = port;
}

void NetAddr::SetLocalhost(uint16_t port) {
    address_ = {127, 0, 0, 1};
    port_ = port;
}

void NetAddr::SetBroadcast(uint16_t port) {
    address_ = {255, 255, 255, 255};
    port_ = port;
}

const std::array<uint8_t, 4>& NetAddr::Address() const {
    return address_;
}

uint16_t NetAddr::Port() const {
    return port_;
}

String NetAddr::ToString() const {
    std::ostringstream oss;
    oss << static_cast<int>(address_[0]) << "."
        << static_cast<int>(address_[1]) << "."
        << static_cast<int>(address_[2]) << "."
        << static_cast<int>(address_[3]) << ":"
        << port_;
    return String(oss.str().c_str());
}

bool NetAddr::operator==(const NetAddr& other) const {
    return address_ == other.address_ && port_ == other.port_;
}

bool NetAddr::operator!=(const NetAddr& other) const {
    return !(*this == other);
}

bool NetAddr::IsLocalhost() const {
    return address_[0] == 127 && address_[1] == 0 && 
           address_[2] == 0 && address_[3] == 1;
}

bool NetAddr::IsBroadcast() const {
    return address_[0] == 255 && address_[1] == 255 && 
           address_[2] == 255 && address_[3] == 255;
}

bool NetAddr::IsUnspecified() const {
    return address_[0] == 0 && address_[1] == 0 && 
           address_[2] == 0 && address_[3] == 0;
}

} // namespace zq::net
