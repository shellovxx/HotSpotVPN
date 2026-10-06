"""Package a verified rootless dylib without requiring dpkg-deb on the host."""
import argparse,gzip,hashlib,io,tarfile
from pathlib import Path
from verify_binary import slices

def archive(files):
    out=io.BytesIO()
    with tarfile.open(fileobj=out,mode='w') as tar:
        for name,(raw,mode) in files.items():
            info=tarfile.TarInfo(name); info.size=len(raw); info.mode=mode; info.uid=info.gid=0
            tar.addfile(info,io.BytesIO(raw))
    return gzip.compress(out.getvalue(),mtime=0)

def main():
    args=argparse.ArgumentParser()
    args.add_argument('--binary',type=Path,required=True)
    args.add_argument('--output',type=Path,required=True)
    a=args.parse_args(); root=Path(__file__).resolve().parents[1]
    binary=a.binary.read_bytes()
    if (0x0100000c,0x80000002) not in slices(binary): raise ValueError('arm64e PAC00 slice required')
    control={'./control':((root/'control').read_bytes(),0o644)}
    for script in ('postinst','postrm'): control['./'+script]=((root/script).read_bytes(),0o755)
    data={
        './var/jb/Library/MobileSubstrate/DynamicLibraries/HotspotVPNDNS.dylib':(binary,0o755),
        './var/jb/Library/MobileSubstrate/DynamicLibraries/HotspotVPNDNS.plist':((root/'HotspotVPNDNS.plist').read_bytes(),0o644),
        './var/jb/Library/PreferenceLoader/Preferences/HotspotVPNDNS.plist':((root/'layout/Library/PreferenceLoader/Preferences/HotspotVPNDNS.plist').read_bytes(),0o644)
    }
    members={'debian-binary':b'2.0\n','control.tar.gz':archive(control),'data.tar.gz':archive(data)}
    out=bytearray(b'!<arch>\n')
    for name,raw in members.items():
        out.extend(f'{name+"/":<16}{0:<12}{0:<6}{0:<6}{100644:<8}{len(raw):<10}`\n'.encode())
        out.extend(raw)
        if len(raw)%2:out.extend(b'\n')
    a.output.write_bytes(out)
    print(a.output.name,len(out),'bytes','SHA256',hashlib.sha256(out).hexdigest())

if __name__=='__main__':main()
