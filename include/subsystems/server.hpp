#pragma once
#include "subsystems/net_driver.hpp"

namespace zq::net {
class Server {
public:
    Server();
    ~Server();
    bool Start(uint16_t port);
    void Stop();
    bool IsRunning() const;
    void Tick(float dt);
private:
    bool running_ = false;
};
}
