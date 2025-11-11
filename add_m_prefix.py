#!/usr/bin/env python3
"""
Script to add m_ prefix to struct member variables in C++ codebase.
This script:
1. Parses all structs in .hpp files
2. Identifies member variables without m_ prefix
3. Updates member declarations and all usages throughout the codebase
"""

import re
import os
from pathlib import Path
from typing import Dict, List, Set, Tuple
from dataclasses import dataclass


@dataclass
class StructInfo:
    """Information about a struct and its members."""
    name: str
    members: List[str]  # Original member names
    file_path: str
    start_line: int
    end_line: int


def find_cpp_files(root_dir: str) -> List[Path]:
    """Find all .cpp and .hpp files in the src directory."""
    src_dir = Path(root_dir) / "src"
    cpp_files = list(src_dir.glob("**/*.cpp"))
    hpp_files = list(src_dir.glob("**/*.hpp"))
    return cpp_files + hpp_files


def parse_struct_definition(content: str, file_path: str) -> List[StructInfo]:
    """
    Parse struct definitions from file content.
    Returns list of StructInfo objects.
    """
    structs = []
    lines = content.split('\n')

    i = 0
    while i < len(lines):
        line = lines[i].strip()

        # Match struct declaration
        struct_match = re.match(r'^struct\s+(\w+)', line)
        if struct_match:
            struct_name = struct_match.group(1)
            start_line = i

            # Find the opening brace
            brace_count = 0
            found_open = False

            # Check if opening brace is on same line
            if '{' in line:
                brace_count = line.count('{') - line.count('}')
                found_open = True
                i += 1
            else:
                # Search for opening brace
                i += 1
                while i < len(lines):
                    if '{' in lines[i]:
                        brace_count = lines[i].count('{') - lines[i].count('}')
                        found_open = True
                        i += 1
                        break
                    i += 1

            if not found_open:
                continue

            # Find matching closing brace and collect members
            members = []
            member_start = i

            while i < len(lines) and brace_count > 0:
                line = lines[i].strip()

                # Update brace count
                brace_count += line.count('{') - line.count('}')

                # Skip comments, empty lines, access specifiers, and nested structs/classes
                if (not line or
                    line.startswith('//') or
                    line.startswith('/*') or
                    line in ['public:', 'private:', 'protected:'] or
                    line.startswith('struct ') or
                    line.startswith('class ') or
                    line.startswith('enum ')):
                    i += 1
                    continue

                # Skip function declarations/definitions (has parentheses before semicolon/brace)
                if '(' in line and (';' in line or '{' in line):
                    # This is likely a function
                    if '{' in line:
                        # Skip to end of function
                        func_braces = line.count('{') - line.count('}')
                        i += 1
                        while i < len(lines) and func_braces > 0:
                            func_braces += lines[i].count('{') - lines[i].count('}')
                            i += 1
                    else:
                        i += 1
                    continue

                # Match member variable declarations
                # Patterns: type name; or type name = value;
                # Handle pointers, references, and templates
                member_pattern = r'^([\w:<>]+(?:\s*[*&]+)?(?:<[^>]+>)?)\s+(\w+)(?:\s*[=\{]|\s*;)'
                match = re.match(member_pattern, line)

                if match:
                    member_name = match.group(2)

                    # Skip if already has m_ or _ prefix
                    if not member_name.startswith('m_') and not member_name.startswith('_'):
                        members.append(member_name)

                i += 1

            end_line = i

            if members:
                structs.append(StructInfo(
                    name=struct_name,
                    members=members,
                    file_path=file_path,
                    start_line=start_line,
                    end_line=end_line
                ))
        else:
            i += 1

    return structs


def create_member_mapping(structs: List[StructInfo]) -> Dict[str, Dict[str, str]]:
    """
    Create a mapping of struct names to member rename mappings.
    Returns: {struct_name: {old_name: new_name}}
    """
    mapping = {}

    for struct in structs:
        member_map = {}
        for member in struct.members:
            new_name = f"m_{member}"
            member_map[member] = new_name

        if member_map:
            mapping[struct.name] = member_map

    return mapping


def update_member_declarations(file_path: str, structs: List[StructInfo],
                               mapping: Dict[str, Dict[str, str]]) -> str:
    """
    Update member variable declarations in struct definitions.
    Returns updated file content.
    """
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()

    lines = content.split('\n')

    for struct in structs:
        if struct.file_path != file_path:
            continue

        if struct.name not in mapping:
            continue

        member_map = mapping[struct.name]

        # Update lines within the struct definition
        for line_idx in range(struct.start_line, min(struct.end_line, len(lines))):
            line = lines[line_idx]

            # For each member, replace its declaration
            for old_name, new_name in member_map.items():
                # Match member declaration: type old_name; or type old_name = value;
                # Handle various patterns with word boundaries
                patterns = [
                    (rf'\b({old_name})\s*;', rf'{new_name};'),
                    (rf'\b({old_name})\s*=', rf'{new_name} ='),
                    (rf'\b({old_name})\s*\{{', rf'{new_name} {{'),
                    (rf'\b({old_name})\s*\(', rf'{new_name}('),  # Initializer list
                ]

                for pattern, replacement in patterns:
                    if re.search(pattern, line):
                        line = re.sub(pattern, replacement, line)

            lines[line_idx] = line

    return '\n'.join(lines)


def update_member_usages(file_path: str, mapping: Dict[str, Dict[str, str]]) -> str:
    """
    Update member variable usages (access) in code.
    Handles: obj.member, obj->member, and bare member references.
    Returns updated file content.
    """
    with open(file_path, 'r', encoding='utf-8') as f:
        content = f.read()

    # Create a flat list of all member renames
    all_renames = {}
    for struct_name, member_map in mapping.items():
        for old_name, new_name in member_map.items():
            all_renames[old_name] = new_name

    lines = content.split('\n')

    for line_idx, line in enumerate(lines):
        # Skip lines that are comments
        stripped = line.strip()
        if stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
            continue

        # For each member rename
        for old_name, new_name in all_renames.items():
            # Pattern 1: obj.member or obj->member
            # Use word boundaries to avoid partial matches
            line = re.sub(rf'\b([a-zA-Z_][\w]*)(\.|-&gt;)({old_name})\b',
                         rf'\1\2{new_name}', line)

            # Pattern 2: member (bare reference, but be careful)
            # Only replace if it's clearly a member access:
            # - After 'return '
            # - In assignments (= member)
            # - In comparisons (== member, != member, etc.)
            # - In function calls (func(member))
            # - In initializer lists ({ member })
            contexts = [
                (rf'\breturn\s+({old_name})\b', rf'return {new_name}'),
                (rf'=\s*({old_name})\b', rf'= {new_name}'),
                (rf'\(\s*({old_name})\b', rf'({new_name}'),
                (rf',\s*({old_name})\b', rf', {new_name}'),
                (rf'\{{\s*({old_name})\b', rf'{{{new_name}'),
                (rf'==\s*({old_name})\b', rf'== {new_name}'),
                (rf'!=\s*({old_name})\b', rf'!= {new_name}'),
                (rf'<\s*({old_name})\b', rf'< {new_name}'),
                (rf'>\s*({old_name})\b', rf'> {new_name}'),
                (rf'\+\s*({old_name})\b', rf'+ {new_name}'),
                (rf'-\s*({old_name})\b', rf'- {new_name}'),
                (rf'\*\s*({old_name})\b', rf'* {new_name}'),
                (rf'/\s*({old_name})\b', rf'/ {new_name}'),
            ]

            for pattern, replacement in contexts:
                line = re.sub(pattern, replacement, line)

        lines[line_idx] = line

    return '\n'.join(lines)


def main():
    """Main execution function."""
    root_dir = Path(__file__).parent

    print("=" * 70)
    print("Struct Member Variable m_ Prefix Automation Script")
    print("=" * 70)
    print()

    # Step 1: Find all C++ files
    print("[1/5] Finding C++ files...")
    cpp_files = find_cpp_files(root_dir)
    print(f"      Found {len(cpp_files)} files")
    print()

    # Step 2: Parse struct definitions from header files
    print("[2/5] Parsing struct definitions...")
    all_structs = []

    for file_path in cpp_files:
        if file_path.suffix == '.hpp':
            try:
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read()
                structs = parse_struct_definition(content, str(file_path))
                all_structs.extend(structs)
            except Exception as e:
                print(f"      Error parsing {file_path}: {e}")

    print(f"      Found {len(all_structs)} structs with members needing m_ prefix")
    print()

    # Print found structs
    if all_structs:
        print("      Structs found:")
        for struct in all_structs:
            print(f"        - {struct.name}: {len(struct.members)} members")
            for member in struct.members:
                print(f"            {member} -> m_{member}")
        print()

    # Step 3: Create member mapping
    print("[3/5] Creating member rename mapping...")
    mapping = create_member_mapping(all_structs)
    print(f"      Created mappings for {len(mapping)} structs")
    print()

    # Step 4: Update member declarations
    print("[4/5] Updating member declarations...")
    header_files = [f for f in cpp_files if f.suffix == '.hpp']

    for file_path in header_files:
        try:
            updated_content = update_member_declarations(str(file_path), all_structs, mapping)
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(updated_content)
            print(f"      Updated: {file_path.name}")
        except Exception as e:
            print(f"      Error updating {file_path}: {e}")
    print()

    # Step 5: Update member usages in all files
    print("[5/5] Updating member usages...")

    for file_path in cpp_files:
        try:
            updated_content = update_member_usages(str(file_path), mapping)
            with open(file_path, 'w', encoding='utf-8') as f:
                f.write(updated_content)
            print(f"      Updated: {file_path.name}")
        except Exception as e:
            print(f"      Error updating {file_path}: {e}")

    print()
    print("=" * 70)
    print("✓ Complete! All struct members have been prefixed with m_")
    print("=" * 70)
    print()
    print("Next steps:")
    print("  1. Review the changes: git diff")
    print("  2. Build the project to check for any issues")
    print("  3. Run tests if available")
    print()


if __name__ == "__main__":
    main()
