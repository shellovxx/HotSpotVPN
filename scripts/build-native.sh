#!/bin/sh
# Requires modern arm64e ABI support, RootHide Theos, an iOS SDK, and ldid.
set -eu
hpd_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
hpd_build=${HPD_BUILD_DIR:-$hpd_project/work/ios-build}
hpd_sdk=${HPD_SDK:?Set HPD_SDK to an iOS 16.5 SDK}
hpd_link_sdk=${HPD_LINK_SDK:-$hpd_sdk}
hpd_compiler=${HPD_CLANG:-clang}
hpd_linker=${HPD_LD:-ld}
hpd_signer=${HPD_LDID:-ldid}
hpd_lipo=${HPD_LIPO:-lipo}
hpd_theos=${THEOS:?Set THEOS to roothide/theos}
mkdir -p "$hpd_build"
perl "$hpd_theos/vendor/logos/bin/logos.pl" "$hpd_project/src/HotspotVPNDNS.x" > "$hpd_build/HotspotVPNDNS.m"
perl "$hpd_theos/vendor/logos/bin/logos.pl" "$hpd_project/prefs/HPDRootListController.x" > "$hpd_build/HotspotVPNDNSPrefs.m"
for hpd_arch in arm64 arm64e; do
    set -- -target "$hpd_arch-apple-ios15.0" -isysroot "$hpd_sdk" -I"$hpd_theos/vendor/include" -I"$hpd_project/src" -F"$hpd_theos/vendor/lib/iphone/roothide" -DTHEOS_PACKAGE_SCHEME_ROOTHIDE -O2 -DNDEBUG -fvisibility=hidden -Wall -Wextra -Werror
    if [ -n "${HPD_RESOURCE_DIR:-}" ]; then set -- "$@" -resource-dir "$HPD_RESOURCE_DIR"; fi
    "$hpd_compiler" "$@" -std=c11 -c "$hpd_project/src/preferences.c" -o "$hpd_build/preferences.$hpd_arch.o"
    "$hpd_compiler" "$@" -std=c11 -c "$hpd_project/src/dhcp_dns.c" -o "$hpd_build/dhcp_dns.$hpd_arch.o"
    for hpd_name in HotspotVPNDNS HotspotVPNDNSPrefs; do
        "$hpd_compiler" "$@" -c "$hpd_build/$hpd_name.m" -o "$hpd_build/$hpd_name.$hpd_arch.o"
        hpd_objects="$hpd_build/preferences.$hpd_arch.o"
        hpd_frameworks='-framework Foundation -framework CoreFoundation'
        if [ "$hpd_name" = HotspotVPNDNS ]; then
            hpd_objects="$hpd_objects $hpd_build/dhcp_dns.$hpd_arch.o"
        else
            hpd_frameworks="$hpd_frameworks -framework UIKit -framework Preferences"
        fi
        "$hpd_linker" -dylib -arch "$hpd_arch" -platform_version ios 15.0 16.5 -syslibroot "$hpd_link_sdk" -L"$hpd_theos/vendor/lib/iphone/roothide" -F"$hpd_theos/vendor/lib/iphone/roothide" -F"$hpd_link_sdk/System/Library/PrivateFrameworks" -lSystem -lobjc -lroothide -framework CydiaSubstrate $hpd_frameworks -install_name "@rpath/$hpd_name.dylib" -headerpad 0x1000 -x -o "$hpd_build/$hpd_name.$hpd_arch.dylib" "$hpd_build/$hpd_name.$hpd_arch.o" $hpd_objects
        "$hpd_signer" -S "$hpd_build/$hpd_name.$hpd_arch.dylib"
    done
done
for hpd_name in HotspotVPNDNS HotspotVPNDNSPrefs; do
    "$hpd_lipo" -create "$hpd_build/$hpd_name.arm64.dylib" "$hpd_build/$hpd_name.arm64e.dylib" -output "$hpd_build/$hpd_name.dylib"
    python3 "$hpd_project/scripts/verify_binary.py" "$hpd_build/$hpd_name.dylib"
done
python3 "$hpd_project/scripts/package.py" --binary "$hpd_build/HotspotVPNDNS.dylib" --preferences-binary "$hpd_build/HotspotVPNDNSPrefs.dylib" --output "$hpd_project/packages/local.hotspotvpndns_1.2.0_iphoneos-arm64e.deb"
