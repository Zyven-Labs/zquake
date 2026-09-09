#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <optional>
#include <system_error>

namespace zq::io {

enum class FileMode {
    Read,
    Write,
    ReadWrite,
    Append
};

enum class SeekMode {
    Begin,
    Current,
    End
};

class IFile {
public:
    virtual ~IFile() = default;
    virtual bool Open() = 0;
    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;
    
    virtual std::size_t Read(void* data, std::size_t size) = 0;
    virtual std::size_t Write(const void* data, std::size_t size) = 0;
    virtual bool Seek(std::int64_t offset, SeekMode mode) = 0;
    virtual std::int64_t Tell() const = 0;
    virtual std::int64_t Size() const = 0;
    virtual bool Eof() const = 0;
};

class FileHandle {
public:
    FileHandle();
    explicit FileHandle(IFile* file);
    ~FileHandle();
    
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;
    
    FileHandle(FileHandle&& other) noexcept;
    FileHandle& operator=(FileHandle&& other) noexcept;
    
    bool Open();
    void Close();
    bool IsOpen() const;
    
    std::size_t Read(void* data, std::size_t size);
    std::size_t Write(const void* data, std::size_t size);
    bool Seek(std::int64_t offset, SeekMode mode);
    std::int64_t Tell() const;
    std::int64_t Size() const;
    bool Eof() const;
    
    IFile* Get() const;
    
private:
    IFile* file_;
};

// RAII file wrapper
class ScopedFile {
public:
    explicit ScopedFile(std::string_view path, FileMode mode);
    ~ScopedFile();
    
    ScopedFile(const ScopedFile&) = delete;
    ScopedFile& operator=(const ScopedFile&) = delete;
    
    ScopedFile(ScopedFile&& other) noexcept;
    ScopedFile& operator=(ScopedFile&& other) noexcept;
    
    // Implicit conversion to bool
    explicit operator bool() const { return file_.IsOpen(); }
    
    // Proxy methods
    bool IsOpen() const { return file_.IsOpen(); }
    std::size_t Read(void* data, std::size_t size) { return file_.Read(data, size); }
    std::size_t Write(const void* data, std::size_t size) { return file_.Write(data, size); }
    bool Seek(std::int64_t offset, SeekMode mode) { return file_.Seek(offset, mode); }
    std::int64_t Tell() const { return file_.Tell(); }
    std::int64_t Size() const { return file_.Size(); }
    
private:
    FileHandle file_;
};

} // namespace zq::io
