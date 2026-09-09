#include "filesystem/virtual_fs.hpp"
#include "filesystem/pak_archive.hpp"
#include "filesystem/path_util.hpp"
#include "core/logging/logger.hpp"
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace stdfs = std::filesystem;

namespace zq::fs {

void VirtualFS::AddSearchPath(std::string_view path) {
    String p(path);
    bool is_pak = false;
    String ext = GetExtension(path);
    if (ext == ".pak" || ext == ".PAK") {
        is_pak = true;
    }
    
    Source src;
    src.path = p;
    src.is_archive = is_pak;
    
    if (is_pak) {
        auto archive = std::make_unique<PAKArchive>();
        if (archive->Open(path)) {
            src.archive = std::move(archive);
            search_paths_.push_back(std::move(src));
        } else {
            log::Warn("VirtualFS: Failed to open PAK archive");
        }
    } else {
        search_paths_.push_back(std::move(src));
    }
}

String VirtualFS::NormalizePath(std::string_view path) {
    return zq::fs::NormalizePath(path);
}

bool VirtualFS::IsAbsolutePath(std::string_view path) {
    return zq::fs::IsAbsolutePath(path);
}

String VirtualFS::GetParentDir(std::string_view path) {
    size_t last_slash = path.rfind('/');
    if (last_slash == String::npos) return String(".");
    return String(path.substr(0, last_slash));
}

String VirtualFS::GetExtension(std::string_view path) {
    return zq::fs::GetExtension(path);
}

String VirtualFS::GetFileName(std::string_view path) {
    return zq::fs::GetFileName(path);
}

bool VirtualFS::OpenFile(std::string_view path, FileAccess access) {
    (void)access;
    return Exists(path);
}

std::optional<std::vector<uint8_t>> VirtualFS::ReadFileAll(std::string_view path) {
    String norm_path = NormalizePath(path);
    
    for (const auto& src : search_paths_) {
        if (src.is_archive && src.archive && src.archive->IsOpen()) {
            for (const auto& entry : src.archive->GetEntries()) {
                if (strcmp(entry.name, norm_path.c_str()) == 0) {
                    std::vector<uint8_t> buffer(entry.file_size);
                    if (src.archive->ReadFile(entry.name, buffer.data(), buffer.size())) {
                        return buffer;
                    }
                }
            }
        } else if (!src.is_archive) {
            stdfs::path full_path = stdfs::path(src.path.c_str()) / norm_path.c_str();
            std::error_code ec;
            if (stdfs::exists(full_path, ec) && !stdfs::is_directory(full_path, ec)) {
                std::ifstream f(full_path, std::ios::binary | std::ios::ate);
                if (f.is_open()) {
                    auto size = f.tellg();
                    f.seekg(0, std::ios::beg);
                    std::vector<uint8_t> buffer(static_cast<size_t>(size));
                    if (f.read(reinterpret_cast<char*>(buffer.data()), size)) {
                        return buffer;
                    }
                }
            }
        }
    }
    
    // Also check direct path if absolute or relative without search paths
    stdfs::path direct_path(norm_path.c_str());
    std::error_code ec;
    if (stdfs::exists(direct_path, ec) && !stdfs::is_directory(direct_path, ec)) {
        std::ifstream f(direct_path, std::ios::binary | std::ios::ate);
        if (f.is_open()) {
            auto size = f.tellg();
            f.seekg(0, std::ios::beg);
            std::vector<uint8_t> buffer(static_cast<size_t>(size));
            if (f.read(reinterpret_cast<char*>(buffer.data()), size)) {
                return buffer;
            }
        }
    }
    
    return std::nullopt;
}

bool VirtualFS::WriteFile(std::string_view path, const void* data, size_t size) {
    String norm_path = NormalizePath(path);
    
    // Write to the first directory search path if available, or direct path
    stdfs::path target_path;
    bool found_dir = false;
    for (const auto& src : search_paths_) {
        if (!src.is_archive) {
            target_path = stdfs::path(src.path.c_str()) / norm_path.c_str();
            found_dir = true;
            break;
        }
    }
    if (!found_dir) {
        target_path = stdfs::path(norm_path.c_str());
    }
    
    std::error_code ec;
    stdfs::create_directories(target_path.parent_path(), ec);
    
    std::ofstream f(target_path, std::ios::binary);
    if (!f.is_open()) return false;
    f.write(static_cast<const char*>(data), size);
    return f.good();
}

bool VirtualFS::Exists(std::string_view path) {
    String norm_path = NormalizePath(path);
    
    for (const auto& src : search_paths_) {
        if (src.is_archive && src.archive && src.archive->IsOpen()) {
            for (const auto& entry : src.archive->GetEntries()) {
                if (strcmp(entry.name, norm_path.c_str()) == 0) {
                    return true;
                }
            }
        } else if (!src.is_archive) {
            stdfs::path full_path = stdfs::path(src.path.c_str()) / norm_path.c_str();
            std::error_code ec;
            if (stdfs::exists(full_path, ec)) {
                return true;
            }
        }
    }
    
    stdfs::path direct_path(norm_path.c_str());
    std::error_code ec;
    return stdfs::exists(direct_path, ec);
}

bool VirtualFS::Delete(std::string_view path) {
    String norm_path = NormalizePath(path);
    for (const auto& src : search_paths_) {
        if (!src.is_archive) {
            stdfs::path full_path = stdfs::path(src.path.c_str()) / norm_path.c_str();
            std::error_code ec;
            if (stdfs::exists(full_path, ec)) {
                return stdfs::remove(full_path, ec);
            }
        }
    }
    stdfs::path direct_path(norm_path.c_str());
    std::error_code ec;
    return stdfs::remove(direct_path, ec);
}

bool VirtualFS::Rename(std::string_view old_path, std::string_view new_path) {
    String norm_old = NormalizePath(old_path);
    String norm_new = NormalizePath(new_path);
    stdfs::path p1(norm_old.c_str());
    stdfs::path p2(norm_new.c_str());
    std::error_code ec;
    stdfs::rename(p1, p2, ec);
    return !ec;
}

bool VirtualFS::CreateDir(std::string_view path) {
    String norm_path = NormalizePath(path);
    for (const auto& src : search_paths_) {
        if (!src.is_archive) {
            stdfs::path full_path = stdfs::path(src.path.c_str()) / norm_path.c_str();
            std::error_code ec;
            return stdfs::create_directories(full_path, ec);
        }
    }
    stdfs::path direct_path(norm_path.c_str());
    std::error_code ec;
    return stdfs::create_directories(direct_path, ec);
}

bool VirtualFS::RemoveDir(std::string_view path) {
    String norm_path = NormalizePath(path);
    for (const auto& src : search_paths_) {
        if (!src.is_archive) {
            stdfs::path full_path = stdfs::path(src.path.c_str()) / norm_path.c_str();
            std::error_code ec;
            return stdfs::remove_all(full_path, ec) > 0;
        }
    }
    stdfs::path direct_path(norm_path.c_str());
    std::error_code ec;
    return stdfs::remove_all(direct_path, ec) > 0;
}

std::vector<FileEntry> VirtualFS::ListDir(std::string_view path) {
    std::vector<FileEntry> result;
    String norm_path = NormalizePath(path);
    
    for (const auto& src : search_paths_) {
        if (!src.is_archive) {
            stdfs::path dir_path = stdfs::path(src.path.c_str()) / norm_path.c_str();
            std::error_code ec;
            if (stdfs::is_directory(dir_path, ec)) {
                for (const auto& it : stdfs::directory_iterator(dir_path, ec)) {
                    FileEntry entry;
                    entry.name = String(it.path().filename().string().c_str());
                    entry.path = String(it.path().string().c_str());
                    entry.is_directory = it.is_directory(ec);
                    entry.size = entry.is_directory ? 0 : it.file_size(ec);
                    result.push_back(std::move(entry));
                }
            }
        }
    }
    return result;
}

bool VirtualFS::ReadFile(std::string_view path, std::function<bool(const uint8_t*, size_t)> callback) {
    auto data = ReadFileAll(path);
    if (!data.has_value()) return false;
    return callback(data->data(), data->size());
}

bool VirtualFS::Mount(std::string_view mount_point, std::string_view source) {
    (void)mount_point;
    AddSearchPath(source);
    return true;
}

bool VirtualFS::Unmount(std::string_view mount_point) {
    for (auto it = search_paths_.begin(); it != search_paths_.end(); ++it) {
        if (it->path == mount_point) {
            search_paths_.erase(it);
            return true;
        }
    }
    return false;
}

// MemoryFS implementation
String MemoryFS::Normalize(std::string_view path) {
    String result;
    result.Reserve(path.size());
    size_t start = 0;
    while (start < path.size()) {
        while (start < path.size() && (path[start] == '/' || path[start] == '\\')) start++;
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
    return result;
}

String MemoryFS::GetFilePath(std::string_view path) {
    if (path.empty()) return String("");
    String normalized = Normalize(path);
    if (normalized.RFind('/') == 0) normalized = normalized.substr(1);
    return normalized;
}

String MemoryFS::NormalizePath(std::string_view path) {
    return Normalize(path);
}

void MemoryFS::AddFile(std::string_view path, std::vector<uint8_t> data) {
    String normalized = GetFilePath(path);
    if (normalized.empty()) return;
    size_t last_slash = normalized.RFind('/');
    if (last_slash != String::npos) {
        String dir = normalized.substr(0, last_slash);
        bool found = false;
        for (const auto& d : directories_) { if (d == dir) { found = true; break; } }
        if (!found) directories_.push_back(dir);
        last_slash = dir.RFind('/');
        while (last_slash != String::npos) {
            dir = dir.substr(0, last_slash);
            bool found2 = false;
            for (const auto& d : directories_) { if (d == dir) { found2 = true; break; } }
            if (!found2) directories_.push_back(dir);
            last_slash = dir.RFind('/');
        }
    }
    MemFile entry;
    entry.name = normalized;
    entry.data = std::move(data);
    entry.is_directory = false;
    files_.push_back(std::move(entry));
}

bool MemoryFS::Exists(std::string_view path) {
    String normalized = GetFilePath(path);
    if (normalized.empty()) return false;
    for (const auto& f : files_) { if (f.name == normalized) return true; }
    for (const auto& d : directories_) { if (d == normalized) return true; }
    return false;
}

bool MemoryFS::CreateDir(std::string_view path) {
    String normalized = GetFilePath(path);
    if (normalized.empty()) return false;
    for (const auto& d : directories_) { if (d == normalized) return true; }
    directories_.push_back(normalized);
    return true;
}

bool MemoryFS::WriteFile(std::string_view path, const void* data, size_t size) {
    std::vector<uint8_t> buffer(static_cast<const uint8_t*>(data),
                                static_cast<const uint8_t*>(data) + size);
    AddFile(path, std::move(buffer));
    return true;
}

bool MemoryFS::OpenFile(std::string_view, FileAccess) { return true; }
std::optional<std::vector<uint8_t>> MemoryFS::ReadFileAll(std::string_view path) {
    String normalized = GetFilePath(path);
    if (normalized.empty()) return std::nullopt;
    for (const auto& f : files_) {
        if (f.name == normalized) {
            return f.data;
        }
    }
    return std::nullopt;
}

bool MemoryFS::Delete(std::string_view path) {
    String normalized = GetFilePath(path);
    for (auto it = files_.begin(); it != files_.end(); ++it) {
        if (it->name == normalized) {
            files_.erase(it);
            return true;
        }
    }
    return false;
}

bool MemoryFS::Rename(std::string_view old_path, std::string_view new_path) {
    String norm_old = GetFilePath(old_path);
    String norm_new = GetFilePath(new_path);
    for (auto& f : files_) {
        if (f.name == norm_old) {
            f.name = norm_new;
            return true;
        }
    }
    return false;
}

bool MemoryFS::RemoveDir(std::string_view path) {
    String normalized = GetFilePath(path);
    for (auto it = directories_.begin(); it != directories_.end(); ++it) {
        if (*it == normalized) {
            directories_.erase(it);
            return true;
        }
    }
    return false;
}

std::vector<FileEntry> MemoryFS::ListDir(std::string_view path) {
    std::vector<FileEntry> result;
    String normalized = GetFilePath(path);
    for (const auto& f : files_) {
        FileEntry entry;
        entry.name = f.name;
        entry.path = f.name;
        entry.size = f.data.size();
        entry.is_directory = f.is_directory;
        result.push_back(std::move(entry));
    }
    for (const auto& d : directories_) {
        FileEntry entry;
        entry.name = d;
        entry.path = d;
        entry.size = 0;
        entry.is_directory = true;
        result.push_back(std::move(entry));
    }
    return result;
}

bool MemoryFS::IsAbsolutePath(std::string_view path) {
    if (path.empty()) return false;
    return path[0] == '/';
}

String MemoryFS::GetParentDir(std::string_view path) {
    size_t last_slash = path.rfind('/');
    if (last_slash == String::npos) return String(".");
    return String(path.substr(0, last_slash));
}

String MemoryFS::GetExtension(std::string_view path) {
    size_t last_dot = path.rfind('.');
    if (last_dot == String::npos) return String("");
    return String(path.substr(last_dot));
}

String MemoryFS::GetFileName(std::string_view path) {
    size_t last_slash = path.rfind('/');
    if (last_slash == String::npos) return String(path);
    return String(path.substr(last_slash + 1));
}

bool MemoryFS::ReadFile(std::string_view path, std::function<bool(const uint8_t*, size_t)> callback) {
    auto data = ReadFileAll(path);
    if (!data.has_value()) return false;
    return callback(data->data(), data->size());
}
bool MemoryFS::Mount(std::string_view, std::string_view) { return false; }
bool MemoryFS::Unmount(std::string_view) { return false; }

} // namespace zq::fs
