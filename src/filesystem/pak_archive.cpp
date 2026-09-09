#include "filesystem/pak_archive.hpp"
#include "core/logging/logger.hpp"
#include <cstring>

namespace zq::fs {

PAKArchive::PAKArchive() {
    header_.ident[0] = 0;
    header_.directory_offset = 0;
    header_.directory_length = 0;
}

bool PAKArchive::Open(std::string_view path) {
    name_ = String(path);
    
    std::FILE* f = std::fopen(path.data(), "rb");
    if (!f) {
        log::Error("Failed to open PAK");
        return false;
    }
    
    std::fseek(f, 0, SEEK_END);
    long file_size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    
    file_data_.resize(file_size);
    if (std::fread(file_data_.data(), 1, file_size, f) != (size_t)file_size) {
        std::fclose(f);
        return false;
    }
    std::fclose(f);
    
    if (file_size < 12) return false;
    
    if (file_data_[0] != 'P' || file_data_[1] != 'A' || 
        file_data_[2] != 'C' || file_data_[3] != 'K') {
        return false;
    }
    
    auto u8 = [&](size_t i) -> uint32_t { return (uint32_t)(uint8_t)file_data_[i]; };
    header_.directory_offset = u8(4) | (u8(5) << 8) | (u8(6) << 16) | (u8(7) << 24);
    header_.directory_length = u8(8) | (u8(9) << 8) | (u8(10) << 16) | (u8(11) << 24);
    
    size_t entry_count = header_.directory_length / sizeof(Entry);
    entries_.resize(entry_count);
    
    if (header_.directory_offset + header_.directory_length <= (size_t)file_data_.size()) {
        memcpy(entries_.data(), file_data_.data() + header_.directory_offset, header_.directory_length);
    }
    
    return true;
}

void PAKArchive::Close() {
    file_data_.clear();
    entries_.clear();
}

bool PAKArchive::IsOpen() const {
    return !file_data_.empty();
}

std::vector<PAKArchive::Entry> PAKArchive::GetEntries() const {
    return entries_;
}

bool PAKArchive::ReadFile(const char* filename, void* buffer, size_t max_size) const {
    for (const auto& entry : entries_) {
        if (strcmp(entry.name, filename) == 0) {
            size_t read_size = (entry.file_size < max_size) ? entry.file_size : max_size;
            if (entry.file_offset + read_size <= file_data_.size()) {
                memcpy(buffer, file_data_.data() + entry.file_offset, read_size);
                return true;
            }
            return false;
        }
    }
    return false;
}

String PAKArchive::GetName() const {
    return name_;
}

} // namespace zq::fs
