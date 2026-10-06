"""Reject an incomplete architecture build before packaging for A12+ iPhones."""
import struct, sys
from pathlib import Path

def slices(data):
    if data[:4] == bytes.fromhex('cafebabe'):
        count=struct.unpack_from('>I',data,4)[0]
        if count > 32 or len(data) < 8+20*count: raise ValueError('bad FAT header')
        result=[]
        for i in range(count):
            cpu,sub,off,size,align=struct.unpack_from('>IIIII',data,8+20*i)
            if off+size>len(data): raise ValueError('slice outside file')
            result.extend(slices(data[off:off+size]))
        return result
    if data[:4] != bytes.fromhex('cffaedfe') or len(data)<32: raise ValueError('expected 64-bit Mach-O')
    cpu,sub,kind=struct.unpack_from('<III',data,4)
    if kind != 6: raise ValueError('expected dylib')
    ncmds,commands_size=struct.unpack_from('<II',data,16)
    if commands_size>len(data)-32: raise ValueError('commands outside file')
    at=32; signed=False
    for _ in range(ncmds):
        if at+8>32+commands_size: raise ValueError('truncated load command')
        cmd,size=struct.unpack_from('<II',data,at)
        if size<8 or at+size>32+commands_size: raise ValueError('invalid load command')
        if cmd==0x1d:
            if size<16: raise ValueError('truncated signature command')
            off,n=struct.unpack_from('<II',data,at+8)
            if n<12 or off+n>len(data) or data[off:off+4]!=bytes.fromhex('fade0cc0'): raise ValueError('invalid signature blob')
            signed=True
        at+=size
    if not signed: raise ValueError('missing embedded code signature')
    return [(cpu,sub)]

if __name__=='__main__':
    found=slices(Path(sys.argv[1]).read_bytes())
    if (0x0100000c,0x80000002) not in found:
        sys.exit('Refusing package: no arm64e PAC00 slice for iPhone 11 Pro Max / iOS 16.6.1. Use a compatible Apple/Procursus toolchain.')
    print('Architecture check passed: signed arm64e PAC00 dylib. See validation.json for device test results.')
