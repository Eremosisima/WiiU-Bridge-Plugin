#!/usr/bin/env python3
"""
WUPS Plugin Validator - Checks .wps file structure before deploying to Wii U.
Run this before copying to SD card.
"""
import struct
import sys
import os

def validate_wups(wps_path):
    print(f"=== WUPS Plugin Validator ===")
    print(f"File: {wps_path}")
    
    if not os.path.exists(wps_path):
        print(f"ERROR: File not found!")
        return False
    
    size = os.path.getsize(wps_path)
    print(f"Size: {size:,} bytes")
    
    with open(wps_path, 'rb') as f:
        data = f.read()
    
    # Check ELF magic
    if data[0:4] != b'\x7fELF':
        print("ERROR: Not an ELF file!")
        return False
    print("ELF magic: OK")
    
    # Check class (32-bit)
    if data[4] != 1:
        print(f"ERROR: Not 32-bit ELF (class={data[4]})")
        return False
    print("ELF class: 32-bit OK")
    
    # Check encoding (big-endian for PowerPC)
    if data[5] != 2:
        print(f"WARNING: Not big-endian (encoding={data[5]})")
    else:
        print("ELF encoding: Big-endian OK")
    
    # Check PL magic at offset 9-10 (written by wups_rules dd command)
    pl_magic = data[9:11]
    if pl_magic == b'PL':
        print("PL magic: OK (offset 9)")
    else:
        print(f"WARNING: PL magic not found at offset 9 (got {pl_magic.hex()})")
        # Check if it's at offset 10
        if data[10:12] == b'PL':
            print("  -> Found at offset 10 (might be OK)")
    
    # Parse section headers to find WUPS sections
    e_shoff = struct.unpack_from('>I' if data[5] == 2 else '<I', data, 28)[0]
    e_shentsize = struct.unpack_from('>H' if data[5] == 2 else '<H', data, 46)[0]
    e_shnum = struct.unpack_from('>H' if data[5] == 2 else '<H', data, 48)[0]
    e_shstrndx = struct.unpack_from('>H' if data[5] == 2 else '<H', data, 50)[0]
    
    print(f"\nSection headers: {e_shnum} entries at offset 0x{e_shoff:x}")
    
    # Read section name string table
    shstr_off = e_shoff + e_shstrndx * e_shentsize
    shstr_offset = struct.unpack_from('>I' if data[5] == 2 else '<I', data, shstr_off + 16)[0]
    shstr_size = struct.unpack_from('>I' if data[5] == 2 else '<I', data, shstr_off + 20)[0]
    shstrtab = data[shstr_offset:shstr_offset + shstr_size]
    
    # Find WUPS sections
    wups_sections = {}
    for i in range(e_shnum):
        sh_offset = e_shoff + i * e_shentsize
        sh_name_idx = struct.unpack_from('>I' if data[5] == 2 else '<I', data, sh_offset)[0]
        sh_addr = struct.unpack_from('>I' if data[5] == 2 else '<I', data, sh_offset + 12)[0]
        sh_offset_val = struct.unpack_from('>I' if data[5] == 2 else '<I', data, sh_offset + 16)[0]
        sh_size = struct.unpack_from('>I' if data[5] == 2 else '<I', data, sh_offset + 20)[0]
        
        # Get section name
        name_end = shstrtab.find(b'\x00', sh_name_idx)
        name = shstrtab[sh_name_idx:name_end].decode('ascii', errors='replace')
        
        if name.startswith('.wups.'):
            wups_sections[name] = {
                'addr': sh_addr,
                'offset': sh_offset_val,
                'size': sh_size,
                'data': data[sh_offset_val:sh_offset_val + sh_size]
            }
    
    # Validate WUPS sections
    required_sections = ['.wups.meta', '.wups.load', '.wups.hooks']
    print("\n--- WUPS Sections ---")
    
    all_ok = True
    for section in required_sections:
        if section in wups_sections:
            info = wups_sections[section]
            print(f"  {section}: OK (addr=0x{info['addr']:08x}, size={info['size']})")
        else:
            print(f"  {section}: MISSING!")
            all_ok = False
    
    # Parse .wups.meta to extract metadata
    if '.wups.meta' in wups_sections:
        meta_data = wups_sections[".wups.meta"]['data']
        print("\n--- Plugin Metadata ---")
        
        # Extract key=value pairs
        meta_str = meta_data.decode('ascii', errors='replace')
        keys = ['name', 'wups', 'version', 'author', 'license', 'description', 'storage_id', 'buildtimestamp']
        for key in keys:
            marker = f"{key}="
            idx = meta_str.find(marker)
            if idx >= 0:
                value_start = idx + len(marker)
                value_end = meta_str.find('\x00', value_start)
                if value_end < 0:
                    value_end = value_start + 32
                value = meta_str[value_start:value_end].strip()
                print(f"  {key}: {value}")
    
    # Parse .wups.hooks to count hooks
    if '.wups.hooks' in wups_sections:
        hooks_data = wups_sections[".wups.hooks"]['data']
        hook_count = len(hooks_data) // 8  # Each hook is 8 bytes (type + address)
        print(f"\n--- Hooks: {hook_count} registered ---")
        
        hook_types = {
            0x00: 'INIT_WUT_MALLOC',
            0x01: 'FINI_WUT_MALLOC',
            0x02: 'INIT_WUT_NEWLIB',
            0x03: 'FINI_WUT_NEWLIB',
            0x04: 'INIT_WUT_STDCPP',
            0x05: 'FINI_WUT_STDCPP',
            0x06: 'INIT_WUT_DEVOPTAB',
            0x07: 'FINI_WUT_DEVOPTAB',
            0x08: 'INIT_WUT_SOCKETS',
            0x09: 'FINI_WUT_SOCKETS',
            0x0A: 'INIT_WRAPPER',
            0x0B: 'FINI_WRAPPER',
            0x0F: 'APPLICATION_STARTS',
            0x10: 'RELEASE_FOREGROUND',
            0x11: 'ACQUIRED_FOREGROUND',
            0x14: 'APPLICATION_ENDS',
            0x16: 'INIT_STORAGE',
            0x17: 'INIT_CONFIG',
            0x18: 'INIT_BUTTON_COMBO',
            0x19: 'INIT_WUT_THREAD',
            0x1A: 'INIT_REENT_FUNCTIONS',
        }
        
        for i in range(hook_count):
            hook_type = struct.unpack_from('<H', hooks_data, i * 8)[0]
            hook_addr = struct.unpack_from('<I', hooks_data, i * 8 + 4)[0]
            type_name = hook_types.get(hook_type, f'UNKNOWN(0x{hook_type:04x})')
            print(f"    [{i}] {type_name} -> 0x{hook_addr:08x}")
    
    # Check imports
    print("\n--- Imports ---")
    for section in wups_sections:
        if section.startswith('.fimport_') or section.startswith('.dimport_'):
            module = section.split('_', 1)[1]
            print(f"  {module}")
    
    # Summary
    print("\n=== VALIDATION RESULT ===")
    if all_ok:
        print("PASS: All required WUPS sections present")
        print("Plugin should load in Aroma")
    else:
        print("FAIL: Missing required sections!")
        print("Plugin will NOT load in Aroma")
    
    return all_ok

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python validate_wups.py <plugin.wps>")
        sys.exit(1)
    
    result = validate_wups(sys.argv[1])
    sys.exit(0 if result else 1)
