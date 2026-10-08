"""Package a signed RootHide dylib without requiring dpkg-deb on the host."""
import argparse,gzip,hashlib,io,tarfile,posixpath
from pathlib import Path
from verify_binary import slices

def archive(files, links=None):
    out=io.BytesIO()
    with tarfile.open(fileobj=out,mode='w') as tar:
        directories=set()
        for name in [*files,*(links or {})]:
            parent=posixpath.dirname(name)
            while parent not in ('','.'): directories.add(parent); parent=posixpath.dirname(parent)
        for name in sorted(directories,key=lambda n:(n.count('/'),n)):
            info=tarfile.TarInfo(name); info.type=tarfile.DIRTYPE; info.mode=0o755
            tar.addfile(info)
        for name,(raw,mode) in files.items():
            info=tarfile.TarInfo(name); info.size=len(raw); info.mode=mode; info.uid=info.gid=0
            tar.addfile(info,io.BytesIO(raw))
        for name,target in (links or {}).items():
            info=tarfile.TarInfo(name); info.type=tarfile.SYMTYPE; info.linkname=target; info.mode=0o777
            tar.addfile(info)
    return gzip.compress(out.getvalue(),mtime=0)

def main():
    args=argparse.ArgumentParser()
    args.add_argument('--binary',type=Path,required=True)
    args.add_argument('--preferences-binary',type=Path,required=True)
    args.add_argument('--output',type=Path,required=True)
    a=args.parse_args(); root=Path(__file__).resolve().parents[1]
    binary=a.binary.read_bytes()
    if (0x0100000c,0x80000002) not in slices(binary): raise ValueError('arm64e PAC00 slice required')
    if b'/var/jb/' in binary: raise ValueError('legacy rootless path in RootHide dylib')
    if b'@loader_path/.jbroot/usr/lib/libsubstrate.dylib' not in binary: raise ValueError('missing RootHide substrate load path')
    control={'./control':((root/'control').read_bytes(),0o644)}
    for script in ('postinst','postrm'): control['./'+script]=((root/script).read_bytes(),0o755)
    data={
        './Library/MobileSubstrate/DynamicLibraries/HotspotVPNDNS.dylib':(binary,0o755),
        './Library/MobileSubstrate/DynamicLibraries/HotspotVPNDNS.plist':((root/'HotspotVPNDNS.plist').read_bytes(),0o644),
        './Library/PreferenceLoader/Preferences/HotspotVPNDNS.plist':((root/'layout/Library/PreferenceLoader/Preferences/HotspotVPNDNS.plist').read_bytes(),0o644)
    }
    for icon in (root/'layout/Library/PreferenceLoader/Preferences').glob('HotspotVPNDNS*.png'):
        data['./Library/PreferenceLoader/Preferences/'+icon.name]=(icon.read_bytes(),0o644)
    preferences=a.preferences_binary.read_bytes()
    if (0x0100000c,0x80000002) not in slices(preferences): raise ValueError('preferences arm64e PAC00 slice required')
    if b'/var/jb/' in preferences: raise ValueError('legacy rootless path in preferences')
    bundle='./Library/PreferenceBundles/HotspotVPNDNSPrefs.bundle/'
    data[bundle+'HotspotVPNDNSPrefs']=(preferences,0o755)
    for resource in (root/'prefs/Resources').iterdir():
        if resource.is_file(): data[bundle+resource.name]=(resource.read_bytes(),0o644)
    members={'debian-binary':b'2.0\n','control.tar.gz':archive(control),'data.tar.gz':archive(data,{bundle+'.jbroot':'../../..'})}
    out=bytearray(b'!<arch>\n')
    for name,raw in members.items():
        out.extend(f'{name+"/":<16}{0:<12}{0:<6}{0:<6}{100644:<8}{len(raw):<10}`\n'.encode())
        out.extend(raw)
        if len(raw)%2:out.extend(b'\n')
    a.output.parent.mkdir(parents=True,exist_ok=True)
    a.output.write_bytes(out)
    print(a.output.name,len(out),'bytes','SHA256',hashlib.sha256(out).hexdigest())

if __name__=='__main__':main()
