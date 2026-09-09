#include "core/io/file_io.hpp"
#include <cstdio>
#include <cstring>

namespace zq::io {

class FileImpl : public IFile {
public:
    FileImpl() : file_(nullptr) {}
    ~FileImpl() override { Close(); }
    bool Open() override { return file_ != nullptr; }
    void Close() override { if (file_) { std::fclose(file_); file_ = nullptr; } }
    bool IsOpen() const override { return file_ != nullptr; }
    bool Create(const char* path, FileMode mode) {
        const char* modes[] = {"rb", "wb", "wb+", "ab"};
        file_ = std::fopen(path, modes[static_cast<int>(mode)]);
        return file_ != nullptr;
    }
    std::size_t Read(void* data, std::size_t size) override { return std::fread(data, 1, size, file_); }
    std::size_t Write(const void* data, std::size_t size) override { return std::fwrite(data, 1, size, file_); }
    bool Seek(std::int64_t offset, SeekMode mode) {
        int whence = (mode == SeekMode::Begin) ? SEEK_SET : (mode == SeekMode::Current) ? SEEK_CUR : SEEK_END;
        return fseeko(file_, offset, whence) == 0;
    }
    std::int64_t Tell() const { return ftello(file_); }
    std::int64_t Size() const {
        std::int64_t cur = Tell();
        fseeko(file_, 0, SEEK_END);
        std::int64_t s = Tell();
        fseeko(file_, cur, SEEK_SET);
        return s;
    }
    bool Eof() const override { return std::feof(file_) != 0; }
private:
    FILE* file_;
};

FileHandle::FileHandle() : file_(nullptr) {}
FileHandle::FileHandle(IFile* file) : file_(file) {}
FileHandle::~FileHandle() {}
FileHandle::FileHandle(FileHandle&& other) noexcept : file_(other.file_) { other.file_ = nullptr; }
FileHandle& FileHandle::operator=(FileHandle&& other) noexcept {
    if (this != &other) { if (file_) file_->Close(); file_ = other.file_; other.file_ = nullptr; }
    return *this;
}
bool FileHandle::Open() { return file_ && file_->Open(); }
void FileHandle::Close() { if (file_) file_->Close(); }
bool FileHandle::IsOpen() const { return file_ && file_->IsOpen(); }
std::size_t FileHandle::Read(void* data, std::size_t size) { return file_ ? file_->Read(data, size) : 0; }
std::size_t FileHandle::Write(const void* data, std::size_t size) { return file_ ? file_->Write(data, size) : 0; }
bool FileHandle::Seek(std::int64_t offset, SeekMode mode) { return file_ ? file_->Seek(offset, mode) : false; }
std::int64_t FileHandle::Tell() const { return file_ ? file_->Tell() : -1; }
std::int64_t FileHandle::Size() const { return file_ ? file_->Size() : -1; }
bool FileHandle::Eof() const { return file_ ? file_->Eof() : true; }
IFile* FileHandle::Get() const { return file_; }

ScopedFile::ScopedFile(std::string_view path, FileMode mode) {
    file_ = FileHandle(new (std::nothrow) FileImpl());
}
ScopedFile::~ScopedFile() { file_.Close(); }
ScopedFile::ScopedFile(ScopedFile&& other) noexcept : file_(std::move(other.file_)) {}
ScopedFile& ScopedFile::operator=(ScopedFile&& other) noexcept {
    if (this != &other) file_ = std::move(other.file_);
    return *this;
}

} // namespace zq::io
