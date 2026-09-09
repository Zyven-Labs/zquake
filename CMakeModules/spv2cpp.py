#!/usr/bin/env python3
"""spv2cpp.py - Embed SPIR-V binary as C++ constexpr array"""
import sys
import os

def main():
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <input.spv> <output.inc>")
        sys.exit(1)
    
    input_file = sys.argv[1]
    output_file = sys.argv[2]
    
    with open(input_file, 'rb') as f:
        data = f.read()
    
    base_name = os.path.splitext(os.path.basename(input_file))[0]
    
    with open(output_file, 'w') as f:
        f.write(f"// Auto-generated from {os.path.basename(input_file)} - DO NOT EDIT\n")
        f.write("#pragma once\n\n")
        f.write(f"namespace {base_name} {{\n")
        f.write("    constexpr unsigned char {}SPIRV[] = {{\n".format(base_name))
        
        for i, byte in enumerate(data):
            if i > 0:
                f.write(", ")
            f.write("0x{:02X}".format(byte))
            
            if (i + 1) % 16 == 0:
                f.write("\n")
            elif i < len(data) - 1:
                f.write(" ")
        
        f.write("\n    };\n")
        f.write(f"    constexpr std::size_t {base_name}SPIRVSize = {len(data)};\n")
        f.write("}\n")

if __name__ == "__main__":
    main()
