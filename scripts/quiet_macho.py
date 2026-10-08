"""Remove only direct calls to NSLog in the converted vendor binary.

The result MUST be re-signed with ldid before packaging. No global logging hook
is installed. Branch targets are resolved through the Mach-O indirect symbols.
"""
import argparse
import struct
from pathlib import Path


def silence(data, base=0):
    if data[:4] == bytes.fromhex('cafebabe'):
        result = []
        count = struct.unpack_from('>I', data, 4)[0]
        for i in range(count):
            _, _, off, size, _ = struct.unpack_from('>IIIII', data, 8 + 20*i)
            part = bytearray(data[off:off+size])
            result.extend(silence(part, base+off))
            data[off:off+size] = part
        return result
    if data[:4] != bytes.fromhex('cffaedfe'):
        raise ValueError('expected Mach-O arm64 dylib')
    cpu, subtype, kind, count = struct.unpack_from('<IIII', data, 4)
    if cpu != 0x0100000c or kind != 6:
        raise ValueError('expected arm64 dylib')
    at, sections, symtab, indirect = 32, [], None, None
    for _ in range(count):
        cmd, size = struct.unpack_from('<II', data, at)
        if size < 8 or at+size > len(data):
            raise ValueError('invalid load command')
        if cmd == 0x19:
            for j in range(struct.unpack_from('<I', data, at+64)[0]):
                off = at+72+80*j
                name = bytes(data[off:off+16]).split(b'\0')[0]
                addr, length, fileoff = struct.unpack_from('<QQI', data, off+32)
                flags, first, stride = struct.unpack_from('<III', data, off+64)
                sections.append((name, addr, length, fileoff, flags, first, stride))
        elif cmd == 2:
            symtab = struct.unpack_from('<IIII', data, at+8)
        elif cmd == 0xb:
            indirect = struct.unpack_from('<II', data, at+56)
        at += size
    if not symtab or not indirect:
        raise ValueError('missing symbol tables')
    symoff, nsyms, stroff, strsize = symtab
    indoff, nind = indirect
    log_stubs = set()
    for _, addr, length, _, flags, first, stride in sections:
        if flags & 0xff != 8 or not stride:
            continue
        for i in range(length//stride):
            if first+i >= nind:
                raise ValueError('invalid stub symbols')
            index = struct.unpack_from('<I', data, indoff+4*(first+i))[0]
            if index >= nsyms:
                continue
            strindex = struct.unpack_from('<I', data, symoff+16*index)[0]
            if strindex >= strsize:
                raise ValueError('invalid symbol name')
            name = bytes(data[stroff+strindex:stroff+strsize]).split(b'\0')[0]
            if name == b'_NSLog':
                log_stubs.add(addr+i*stride)
    patches = []
    for name, addr, length, off, *_ in sections:
        if name != b'__text':
            continue
        for i in range(0, length-3, 4):
            insn = struct.unpack_from('<I', data, off+i)[0]
            if insn & 0xfc000000 != 0x94000000:
                continue
            displacement = insn & 0x03ffffff
            if displacement & 0x02000000:
                displacement -= 0x04000000
            if addr+i+4*displacement in log_stubs:
                struct.pack_into('<I', data, off+i, 0xd503201f)
                patches.append(base+off+i)
    return patches


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('binary', type=Path)
    p.add_argument('--output', type=Path, required=True)
    a = p.parse_args()
    data = bytearray(a.binary.read_bytes())
    offsets = silence(data)
    if not offsets:
        raise ValueError('no direct NSLog calls found; inspect the binary manually')
    a.output.write_bytes(data)
    print(f'Removed {len(offsets)} direct NSLog calls; re-sign {a.output.name}')


if __name__ == '__main__':
    main()
