#pragma once
#include <cstdint>
#include <cstddef>
#include <vector>

namespace zq::vm {

// QuakeC progs.dat format loader
class Bytecode {
public:
    struct Header {
        int32_t ident;          // "PGD0"
        int32_t version;        // 6
        int32_t length;         // file length
        int32_t defines_off;
        int32_t define_count;
        int32_t globals_off;
        int32_t global_count;
        int32_t functions_off;
        int32_t function_count;
        int32_t strings_off;
        int32_t string_length;
    };
    
    struct Global {
        int32_t offset;       // offset in global memory
        int32_t name;         // offset in string table
    };
    
    struct Function {
        int32_t first_statement; // offset to first statement
        int32_t local_size;      // size of local vars
        int32_t id;              // function ID
        int32_t parameters;      // linked list of parameters
        int32_t sparams;         // size of params in stack
        int32_t name;            // offset in string table
        int32_t file;            // source file ID
    };
    
    bool Load(const void* data, size_t size);
    const Header& GetHeader() const;
    const std::vector<Global>& GetGlobals() const;
    const std::vector<Function>& GetFunctions() const;
    const char* GetString(int offset) const;
    
private:
    Header header_;
    std::vector<Global> globals_;
    std::vector<Function> functions_;
    std::vector<char> strings_;
};

}
