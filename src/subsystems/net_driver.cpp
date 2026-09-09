#include "subsystems/net_driver.hpp"
#include "sdl_net_stub.h"
#include <cstring>

namespace zq::net {

class SDL2Socket : public UDPSocket {
public:
    SDL2Socket() : socket_(nullptr) {}
    ~SDL2Socket() override { Close(); }
    
    bool Bind(const NetAddr& addr) override { return false; }
    bool Send(const NetAddr& addr, const void* data, size_t length) override { return false; }
    size_t Receive(NetAddr& from, void* buffer, size_t max_length) override { return 0; }
    bool IsOpen() const override { return socket_ != nullptr; }
    void Close() override { socket_ = nullptr; }
    
private:
    SDLNet_UDPSocket* socket_;
};

UDPSocket* CreateSocket() { return new SDL2Socket(); }
void DestroySocket(UDPSocket* socket) { delete socket; }

}
