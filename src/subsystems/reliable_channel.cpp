#include "subsystems/reliable_channel.hpp"

namespace zq::net {
bool ReliableChannel::Send(uint32_t sequence, const void* data, size_t length) { return true; }
bool ReliableChannel::Receive(uint32_t& sequence, void* buffer, size_t max_length) { return false; }
}
