#pragma once

#include <string_view>
#include <vector>
#include <optional>
#include <functional>
#include <memory>
#include "core/container/string.hpp"
#include "core/io/file_io.hpp"

namespace zq::fs {

class PAKArchive;

enum class FileAccess {
    Read,
    Write,
    ReadWrite
};

struct FileEntry {
    String name;
    String path;
    size_t size;
    bool is_directory;
};

struct SeekResult {
    bool success;
    int64_t position;
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    
    // File operations
    virtual bool OpenFile(std::string_view path, FileAccess access) = 0;
    virtual std::optional<std::vector<uint8_t>> ReadFileAll(std::string_view path) = 0;
    virtual bool WriteFile(std::string_view path, const void* data, size_t size) = 0;
    virtual bool Exists(std::string_view path) = 0;
    virtual bool Delete(std::string_view path) = 0;
    virtual bool Rename(std::string_view old_path, std::string_view new_path) = 0;
    
    // Directory operations
    virtual bool CreateDir(std::string_view path) = 0;
    virtual bool RemoveDir(std::string_view path) = 0;
    virtual std::vector<FileEntry> ListDir(std::string_view path) = 0;
    
    // Path operations
    virtual String NormalizePath(std::string_view path) = 0;
    virtual bool IsAbsolutePath(std::string_view path) = 0;
    virtual String GetParentDir(std::string_view path) = 0;
    virtual String GetExtension(std::string_view path) = 0;
    virtual String GetFileName(std::string_view path) = 0;
    
    // Callback-based file reading
    virtual bool ReadFile(std::string_view path, std::function<bool(const uint8_t*, size_t)> callback) = 0;
    
    // Mount/unmount
    virtual bool Mount(std::string_view mount_point, std::string_view source) = 0;
    virtual bool Unmount(std::string_view mount_point) = 0;
};

// Virtual filesystem that stacks multiple archives/directories
class VirtualFS : public IFileSystem {
public:
    ~VirtualFS() override = default;
    
    // Add a search path (top priority first)
    void AddSearchPath(std::string_view path);
    
    // Core IFileSystem interface
    bool OpenFile(std::string_view path, FileAccess access) override;
    std::optional<std::vector<uint8_t>> ReadFileAll(std::string_view path) override;
    bool WriteFile(std::string_view path, const void* data, size_t size) override;
    bool Exists(std::string_view path) override;
    bool Delete(std::string_view path) override;
    bool Rename(std::string_view old_path, std::string_view new_path) override;
    bool CreateDir(std::string_view path) override;
    bool RemoveDir(std::string_view path) override;
    std::vector<FileEntry> ListDir(std::string_view path) override;
    String NormalizePath(std::string_view path) override;
    bool IsAbsolutePath(std::string_view path) override;
    String GetParentDir(std::string_view path) override;
    String GetExtension(std::string_view path) override;
    String GetFileName(std::string_view path) override;
    bool ReadFile(std::string_view path, std::function<bool(const uint8_t*, size_t)> callback) override;
    bool Mount(std::string_view mount_point, std::string_view source) override;
    bool Unmount(std::string_view mount_point) override;

private:
    // Find the best matching source for a path
    struct Source {
        String path;
        bool is_archive;
        std::unique_ptr<PAKArchive> archive;
    };
    
    std::vector<Source> search_paths_;
};

// Simple in-memory file system for testing
class MemoryFS : public IFileSystem {
public:
    struct MemFile {
        String name;
        std::vector<uint8_t> data;
        bool is_directory = false;
    };
    
    bool OpenFile(std::string_view path, FileAccess access) override;
    std::optional<std::vector<uint8_t>> ReadFileAll(std::string_view path) override;
    bool WriteFile(std::string_view path, const void* data, size_t size) override;
    bool Exists(std::string_view path) override;
    bool Delete(std::string_view path) override;
    bool Rename(std::string_view old_path, std::string_view new_path) override;
    bool CreateDir(std::string_view path) override;
    bool RemoveDir(std::string_view path) override;
    std::vector<FileEntry> ListDir(std::string_view path) override;
    String NormalizePath(std::string_view path) override;
    bool IsAbsolutePath(std::string_view path) override;
    String GetParentDir(std::string_view path) override;
    String GetExtension(std::string_view path) override;
    String GetFileName(std::string_view path) override;
    bool ReadFile(std::string_view path, std::function<bool(const uint8_t*, size_t)> callback) override;
    bool Mount(std::string_view mount_point, std::string_view source) override;
    bool Unmount(std::string_view mount_point) override;
    
    // Add file data directly
    void AddFile(std::string_view path, std::vector<uint8_t> data);
    
private:
    String Normalize(std::string_view path);
    String GetFilePath(std::string_view path);
    
    std::vector<String> directories_;
    std::vector<MemFile> files_;
};

} // namespace zq::fs
