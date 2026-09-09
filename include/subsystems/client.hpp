#pragma once
#include "subsystems/net_driver.hpp"

namespace zq::net {
class Client {
public:
    Client();
    ~Client();
    bool Connect(const NetAddr& addr);
    void Disconnect();
    bool IsConnected() const;
    void Tick(float dt);
private:
    bool connected_ = false;
};
}
