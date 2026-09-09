#include "subsystems/client.hpp"

namespace zq::net {
Client::Client() = default;
Client::~Client() = default;
bool Client::Connect(const NetAddr& addr) { return false; }
void Client::Disconnect() {}
bool Client::IsConnected() const { return connected_; }
void Client::Tick(float dt) {}
}
