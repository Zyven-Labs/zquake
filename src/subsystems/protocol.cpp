#include "subsystems/protocol.hpp"

namespace zq::net {

bool Protocol::ParseConnect(const void* data, size_t length) { return false; }
bool Protocol::ParseChallenge(const void* data, size_t length) { return false; }
bool Protocol::ParseGame(const void* data, size_t length) { return false; }
bool Protocol::ParseDelta(const void* data, size_t length) { return false; }

Protocol::Header Protocol::BuildConnect(const String& name, const String& game, int version) { return Protocol::Header{}; }
Protocol::Header Protocol::BuildChallenge() { return Protocol::Header{}; }
Protocol::Header Protocol::BuildGame(const String& map) { return Protocol::Header{}; }

int Protocol::GetMaxPayload(Version version) { return 1400; }

}
