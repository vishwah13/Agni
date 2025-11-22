#!/usr/bin/env python3
"""
Convert SPIR-V fallback shaders to C++ byte arrays.
"""

import sys
from pathlib import Path

def spv_to_cpp_array(spv_path, var_name):
    """Convert SPIR-V binary to C++ array"""
    with open(spv_path, 'rb') as f:
        data = f.read()

    # Format as hex array
    hex_data = ', '.join(f'0x{b:02x}' for b in data)

    # Split into lines of reasonable length (12 bytes per line)
    bytes_per_line = 12
    lines = []
    for i in range(0, len(data), bytes_per_line):
        line_data = ', '.join(f'0x{b:02x}' for b in data[i:i+bytes_per_line])
        lines.append('\t' + line_data)

    array_data = ',\n'.join(lines)

    return array_data, len(data)

def main():
    script_dir = Path(__file__).parent

    shaders = [
        ('MeshFallback.vert.spv', 'meshVertSpv'),
        ('MeshFallback.frag.spv', 'meshFragSpv'),
        ('SkyboxFallback.vert.spv', 'skyboxVertSpv'),
        ('SkyboxFallback.frag.spv', 'skyboxFragSpv'),
    ]

    # Generate header file
    header_content = """// Auto-generated fallback shader arrays
// Do not edit manually - regenerate with generate_fallback_arrays.py

#pragma once

namespace FallbackShaders
{
"""

    for _, var_name in shaders:
        header_content += f"\textern const unsigned char {var_name}[];\n"
        header_content += f"\textern const unsigned int {var_name}_len;\n\n"

    header_content += "} // namespace FallbackShaders\n"

    # Generate source file
    source_content = """// Auto-generated fallback shader arrays
// Do not edit manually - regenerate with generate_fallback_arrays.py

#include "FallbackShaders.hpp"

namespace FallbackShaders
{

"""

    for spv_file, var_name in shaders:
        spv_path = script_dir / spv_file
        if not spv_path.exists():
            print(f"Error: {spv_path} not found!")
            sys.exit(1)

        array_data, size = spv_to_cpp_array(spv_path, var_name)

        source_content += f"const unsigned char {var_name}[] = {{\n"
        source_content += array_data
        source_content += "\n};\n\n"
        source_content += f"const unsigned int {var_name}_len = {size};\n\n"

    source_content += "} // namespace FallbackShaders\n"

    # Write header file
    header_path = script_dir.parent.parent / 'src' / 'FallbackShaders.hpp'
    with open(header_path, 'w') as f:
        f.write(header_content)
    print(f"Generated: {header_path}")

    # Write source file
    source_path = script_dir.parent.parent / 'src' / 'FallbackShaders.cpp'
    with open(source_path, 'w') as f:
        f.write(source_content)
    print(f"Generated: {source_path}")

if __name__ == '__main__':
    main()
