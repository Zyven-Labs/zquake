#pragma once

#include <cstdint>
#include <string_view>
#include "core/container/string.hpp"

namespace zq::net {

// Quake network protocol handling
class Protocol {
public:
    enum Version {
        Q1_V15 = 15,
        Q1_V28 = 28,
        QW_V2 = 2
    };
    
    struct Header {
        uint32_t length;
        uint8_t data[2048];
    };
    
    static bool ParseConnect(const void* data, size_t length);
    static bool ParseChallenge(const void* data, size_t length);
    static bool ParseGame(const void* data, size_t length);
    static bool ParseDelta(const void* data, size_t length);
    
    static Header BuildConnect(const String& name, const String& game, int version);
    static Header BuildChallenge();
    static Header BuildGame(const String& map);
    
    static int GetMaxPayload(Version version);
};

} // namespace zq::net
