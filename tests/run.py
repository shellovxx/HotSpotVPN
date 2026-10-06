"""Build and test the actual portable packet parser on Linux/macOS/Windows."""
import argparse, os, subprocess, sys, tempfile
from pathlib import Path

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--cc', default=os.environ.get('CC','clang'))
    args=p.parse_args()
    root=Path(__file__).resolve().parents[1]
    with tempfile.TemporaryDirectory(prefix='hotspotvpndns-tests-') as temp:
        suffix='.dll' if sys.platform=='win32' else '.dylib' if sys.platform=='darwin' else '.so'
        library=Path(temp)/('dhcp_dns'+suffix)
        compiler=[args.cc,'cc'] if Path(args.cc).stem.lower()=='zig' else [args.cc]
        subprocess.run([*compiler,'-std=c11','-O2','-Wall','-Wextra','-Werror','-shared',*(['-fPIC'] if sys.platform!='win32' else []),str(root/'src/dhcp_dns.c'),'-o',str(library)],check=True)
        subprocess.run([sys.executable,str(root/'tests/test_packets.py'),str(library)],check=True)

if __name__=='__main__':main()
