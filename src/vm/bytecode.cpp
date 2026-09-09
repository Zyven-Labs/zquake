#include "vm/bytecode.hpp"
#include "core/logging/logger.hpp"
#include "core/container/string.hpp"
#include <sstream>

namespace zq::vm {

bool Bytecode::Load(const void* data, size_t size) {
    const Header* header = static_cast<const Header*>(data);
    
    if (header->ident != 0x30444750) {
        log::Error("Invalid progs.dat magic number");
        return false;
    }
    
    header_ = *header;
    
    const Global* globals = reinterpret_cast<const Global*>(
        static_cast<const char*>(data) + header_.globals_off);
    globals_.assign(globals, globals + header_.global_count);
    
    const Function* functions = reinterpret_cast<const Function*>(
        static_cast<const char*>(data) + header_.functions_off);
    functions_.assign(functions, functions + header_.function_count);
    
    strings_.assign(
        static_cast<const char*>(data) + header_.strings_off,
        static_cast<const char*>(data) + header_.strings_off + header_.string_length);
    
    std::ostringstream oss;
    oss << "Loaded progs.dat: " << header_.function_count << " functions";
    log::Info(String(oss.str().c_str()));
    return true;
}

const Bytecode::Header& Bytecode::GetHeader() const {
    return header_;
}

const std::vector<Bytecode::Global>& Bytecode::GetGlobals() const {
    return globals_;
}

const std::vector<Bytecode::Function>& Bytecode::GetFunctions() const {
    return functions_;
}

const char* Bytecode::GetString(int offset) const {
    if (offset < 0 || offset >= static_cast<int>(strings_.size())) {
        return "";
    }
    return strings_.data() + offset;
}

}
