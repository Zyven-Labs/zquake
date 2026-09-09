# spv2cpp.cmake - Embed SPIR-V binary as C++ constexpr array
cmake_minimum_required(VERSION 3.27)

# Read binary file
file(READ "${SRC}" _DATA BINARY)

# Get file size
string(LENGTH "${_DATA}" _LEN)

# Open output file
file(WRITE "${DEST}" "// Auto-generated from ${CMAKE_BASE_NAME}.spv - DO NOT EDIT\n")
file(APPEND "${DEST}" "#pragma once\n\n")
file(APPEND "${DEST}" "namespace ${NAME} {\n")
file(APPEND "${DEST}" "    constexpr unsigned char ${CMAKE_BASE_NAME}SPIRV[] = {\n")

# Write hex bytes (16 per line)
set(_idx 0)
set(_line "")
string(LENGTH "${_DATA}" _data_len)
while(_idx LESS _data_len)
    if(_idx GREATER 0)
        file(APPEND "${DEST}" ",")
    endif()
    
    # Calculate position in string
    math(EXPR _pos "${_idx}")
    string(SUBSTRING "${_DATA}" ${_pos} 1 _byte)
    
    # Convert to hex
    string(ASCII ${_byte} _hex)
    string(TOUPPER "${_hex}" _hex)
    
    string(APPEND _line "0x${_hex}")
    
    # Add comma if not last on line or not last byte
    if(_idx LESS (_data_len - 1))
        string(APPEND _line ", ")
    endif()
    
    math(EXPR _idx "${_idx} + 1")
    
    # Every 16 bytes, start new line
    if(_idx MOD 16 EQUAL 0)
        file(APPEND "${DEST}" "\n")
        file(APPEND "${DEST}" "    ${_line}")
        set(_line "")
    endif()
endwhile()

# Handle last line
if(_line)
    file(APPEND "${DEST}" "\n")
    file(APPEND "${DEST}" "    ${_line}\n")
endif()

file(APPEND "${DEST}" "};\n")
file(APPEND "${DEST}" "    constexpr std::size_t ${CMAKE_BASE_NAME}SPIRVSize = ${_data_len};\n")
file(APPEND "${DEST}" "}\n")
