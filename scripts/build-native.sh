#!/bin/sh
# Requires an Apple-compatible toolchain with the iOS 14+ arm64e ABI.
set -eu
hpd_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
hpd_build="$hpd_project/work/ios-build"
hpd_sdk=${HPD_SDK:?Set HPD_SDK to an iOS 16.5 SDK with headers}
hpd_link_sdk=${HPD_LINK_SDK:-$hpd_sdk}
hpd_compiler=${HPD_CLANG:-clang}
hpd_linker=${HPD_LD:-ld}
hpd_signer=${HPD_LDID:-ldid}
hpd_lipo=${HPD_LIPO:-lipo}
hpd_theos=${THEOS:?Set THEOS to roothide/theos}
mkdir -p "$hpd_build"
perl "$hpd_theos/vendor/logos/bin/logos.pl" "$hpd_project/src/HotspotVPNDNS.x" > "$hpd_build/HotspotVPNDNS.m"
for hpd_arch in arm64 arm64e; do
set -- -target "$hpd_arch-apple-ios15.0" -isysroot "$hpd_sdk" -isystem "$hpd_sdk/usr/include" -iframework "$hpd_sdk/System/Library/Frameworks" -I"$hpd_theos/vendor/include" -I"$hpd_project/src" -F"$hpd_theos/vendor/lib/iphone/roothide" -DTHEOS_PACKAGE_SCHEME_ROOTHIDE
if [ -n "${HPD_RESOURCE_DIR:-}" ]; then
    set -- "$@" -resource-dir "$HPD_RESOURCE_DIR"
fi
"$hpd_compiler" "$@" -O2 -DNDEBUG -fvisibility=hidden -Wall -Wextra -Werror -c "$hpd_build/HotspotVPNDNS.m" -o "$hpd_build/HotspotVPNDNS.$hpd_arch.o"
"$hpd_compiler" "$@" -std=c11 -O2 -DNDEBUG -fvisibility=hidden -Wall -Wextra -Werror -c "$hpd_project/src/dhcp_dns.c" -o "$hpd_build/dhcp_dns.$hpd_arch.o"
"$hpd_linker" -dylib -arch "$hpd_arch" -platform_version ios 15.0 16.5 -syslibroot "$hpd_link_sdk" -L"$hpd_theos/vendor/lib/iphone/roothide" -F"$hpd_theos/vendor/lib/iphone/roothide" -lSystem -lobjc -lroothide -framework CydiaSubstrate -framework Foundation -framework CoreFoundation -install_name @rpath/HotspotVPNDNS.dylib -headerpad 0x1000 -x -o "$hpd_build/HotspotVPNDNS.$hpd_arch.dylib" "$hpd_build/HotspotVPNDNS.$hpd_arch.o" "$hpd_build/dhcp_dns.$hpd_arch.o"
"$hpd_signer" -S "$hpd_build/HotspotVPNDNS.$hpd_arch.dylib"
done
"$hpd_lipo" -create "$hpd_build/HotspotVPNDNS.arm64.dylib" "$hpd_build/HotspotVPNDNS.arm64e.dylib" -output "$hpd_build/HotspotVPNDNS.dylib"
python3 "$hpd_project/scripts/verify_binary.py" "$hpd_build/HotspotVPNDNS.dylib"
echo "$hpd_build/HotspotVPNDNS.dylib"
