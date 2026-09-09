#pragma once

#include <cstdint>
#include <cstddef>
#include <string_view>
#include <vector>
#include "core/container/string.hpp"

namespace zq::fs {

class PAKArchive {
public:
    PAKArchive();
    ~PAKArchive() = default;
    
    bool Open(std::string_view path);
    void Close();
    bool IsOpen() const;
    
    struct Entry {
        char name[56];       // dpackfile_t: name comes FIRST
        uint32_t file_offset;
        uint32_t file_size;
    };
    
    std::vector<Entry> GetEntries() const;
    bool ReadFile(const char* filename, void* buffer, size_t max_size) const;
    String GetName() const;
    
private:
    struct Header {
        char ident[4];
        uint32_t directory_offset;
        uint32_t directory_length;
    };
    
    Header header_;
    std::vector<Entry> entries_;
    std::vector<char> file_data_;
    String name_;
};

} // namespace zq::fs
