#pragma once
#include "core/container/string.hpp"

namespace zq::engine {

// Parse CVar commands (set, bind, etc.)
class CVarParser {
public:
    static bool ParseSetCommand(const String& line, String& cvar_name, String& value);
    static bool ParseBindCommand(const String& line, String& key, String& command);
    static String StripComments(const String& line);
};

}
