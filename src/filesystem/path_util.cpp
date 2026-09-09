#include "filesystem/path_util.hpp"
#include <algorithm>

namespace zq::fs {

String NormalizePath(std::string_view path) {
    String result;
    result.Reserve(path.size());
    
    size_t start = 0;
    while (start < path.size()) {
        // Skip leading separators
        while (start < path.size() && (path[start] == '/' || path[start] == '\\')) start++;
        
        // Find path component
        size_t end = start;
        while (end < path.size() && path[end] != '/' && path[end] != '\\') end++;
        
        String component(path.substr(start, end - start));
        if (component == "..") {
            size_t last_slash = result.RFind('/');
            if (last_slash != String::npos) {
                result = result.substr(0, last_slash);
            }
        } else if (component != ".") {
            if (!result.empty()) result += '/';
            result += component;
        }
        
        start = end;
    }
    
    return String(result);
}

String CombinePaths(std::string_view base, std::string_view relative) {
    if (relative.empty()) return String(base);
    if (IsAbsolutePath(relative)) return String(relative);
    
    String result(base);
    if (!result.empty() && result[result.size() - 1] != '/') {
        result += '/';
    }
    result += String(relative);
    return NormalizePath(std::string_view(result.data(), result.size()));
}

String GetExtension(std::string_view path) {
    size_t last_dot = path.rfind('.');
    if (last_dot == String::npos) return String("");
    return String(path.substr(last_dot));
}

String GetFileName(std::string_view path) {
    size_t last_slash = path.find_last_of("/\\");
    if (last_slash == String::npos) return String(path);
    return String(path.substr(last_slash + 1));
}

String GetFileNameWithoutExtension(std::string_view path) {
    String filename = GetFileName(path);
    size_t dot = filename.RFind('.');
    if (dot == String::npos) return filename;
    return filename.substr(0, dot);
}

bool IsAbsolutePath(std::string_view path) {
    if (path.empty()) return false;
#ifdef ZQ_PLATFORM_WINDOWS
    return path.size() >= 2 && 
           ((path[0] >= 'A' && path[0] <= 'Z') || (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':';
#else
    return path[0] == '/';
#endif
}

} // namespace zq::fs
