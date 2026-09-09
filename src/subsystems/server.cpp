#include "subsystems/server.hpp"

namespace zq::net {
Server::Server() = default;
Server::~Server() = default;
bool Server::Start(uint16_t port) { (void)port; return false; }
void Server::Stop() {}
bool Server::IsRunning() const { return running_; }
void Server::Tick(float dt) { (void)dt; }
}
