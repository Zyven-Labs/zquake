#pragma once

#include <string_view>
#include "core/container/string.hpp"

namespace zq::fs {

// Path manipulation utilities
String NormalizePath(std::string_view path);
String CombinePaths(std::string_view base, std::string_view relative);
String GetExtension(std::string_view path);
String GetFileName(std::string_view path);
String GetFileNameWithoutExtension(std::string_view path);
bool IsAbsolutePath(std::string_view path);

} // namespace zq::fs
