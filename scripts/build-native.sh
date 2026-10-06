#!/bin/sh
# Compatible Procursus Clang 16 + ld64 951.9 + iOS 16.5 SDK were device-tested.
set -eu
hpd_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
hpd_build="$hpd_project/work/ios-build"
hpd_sdk=${HPD_SDK:?Set HPD_SDK to an iOS 16.5 SDK with headers}
hpd_link_sdk=${HPD_LINK_SDK:-$hpd_sdk}
hpd_compiler=${HPD_CLANG:-clang}
hpd_linker=${HPD_LD:-ld}
hpd_signer=${HPD_LDID:-ldid}
mkdir -p "$hpd_build"
set -- -target arm64e-apple-ios15.0 -isysroot "$hpd_sdk" -isystem "$hpd_sdk/usr/include" -iframework "$hpd_sdk/System/Library/Frameworks"
if [ -n "${HPD_RESOURCE_DIR:-}" ]; then
    set -- "$@" -resource-dir "$HPD_RESOURCE_DIR"
fi
for hpd_unit in HotspotVPNDNS dhcp_dns; do
    "$hpd_compiler" "$@" -std=c11 -O2 -DNDEBUG -Wall -Wextra -Werror -c "$hpd_project/src/$hpd_unit.c" -o "$hpd_build/$hpd_unit.o"
done
"$hpd_linker" -dylib -arch arm64e -platform_version ios 15.0 16.5 -syslibroot "$hpd_link_sdk" -lSystem.B -lobjc.A -framework CoreFoundation -install_name @rpath/HotspotVPNDNS.dylib -o "$hpd_build/HotspotVPNDNS.dylib" "$hpd_build/HotspotVPNDNS.o" "$hpd_build/dhcp_dns.o"
"$hpd_signer" -S "$hpd_build/HotspotVPNDNS.dylib"
python3 "$hpd_project/scripts/verify_binary.py" "$hpd_build/HotspotVPNDNS.dylib"
echo "$hpd_build/HotspotVPNDNS.dylib"
