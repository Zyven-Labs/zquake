#include "engine/cvar_parser.hpp"

namespace zq::engine {

bool CVarParser::ParseSetCommand(const String& line, String& cvar_name, String& value) {
    auto parts = line.Split(' ');
    if (parts.size() < 2) return false;
    cvar_name = parts[0];
    value = parts[1];
    return true;
}

bool CVarParser::ParseBindCommand(const String& line, String& key, String& command) {
    auto parts = line.Split(' ');
    if (parts.size() < 2) return false;
    key = parts[0];
    command = parts[1];
    for (size_t i = 2; i < parts.size(); i++) {
        command += ' ';
        command += parts[i];
    }
    return true;
}

String CVarParser::StripComments(const String& line) {
    size_t comment_pos = line.Find('#');
    if (comment_pos == String::npos) return line;
    return line.substr(0, comment_pos);
}

} // namespace zq::engine
