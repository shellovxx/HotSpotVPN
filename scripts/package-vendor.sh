#!/bin/sh
# Run after RootHide Converter on a COPY of the original vendor package.
# Requires python3, ldid and install_name_tool (RootHide Patcher cctools).
set -eu
hpd_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
hpd_input=${1:?Usage: package-vendor.sh converted.deb output.deb}
hpd_output=${2:?Usage: package-vendor.sh converted.deb output.deb}
hpd_sign=${HPD_LDID:-ldid}
hpd_install_name=${HPD_INSTALL_NAME_TOOL:-/Applications/Patcher.app/cctools/install_name_tool}
hpd_stage=$(mktemp -d)
trap 'rm -rf -- "$hpd_stage"' EXIT HUP INT TERM
[ "$(dpkg-deb -f "$hpd_input" Package)" = com.evgeniy.hotspotvpn ]
[ "$(dpkg-deb -f "$hpd_input" Architecture)" = iphoneos-arm64e ]
dpkg-deb -R "$hpd_input" "$hpd_stage"
for hpd_binary in Library/MobileSubstrate/DynamicLibraries/HotspotVPN.dylib Library/ControlCenter/Bundles/HotspotVPNCC.bundle/HotspotVPNCC; do
    python3 "$hpd_project/scripts/quiet_macho.py" "$hpd_stage/$hpd_binary" --output "$hpd_stage/$hpd_binary.quiet"
    mv "$hpd_stage/$hpd_binary.quiet" "$hpd_stage/$hpd_binary"
done
"$hpd_install_name" -id @rpath/HotspotVPNCC "$hpd_stage/Library/ControlCenter/Bundles/HotspotVPNCC.bundle/HotspotVPNCC"
for hpd_binary in Library/MobileSubstrate/DynamicLibraries/HotspotVPN.dylib Library/ControlCenter/Bundles/HotspotVPNCC.bundle/HotspotVPNCC; do
    "$hpd_sign" -S "$hpd_stage/$hpd_binary"
    python3 "$hpd_project/scripts/verify_binary.py" "$hpd_stage/$hpd_binary"
done
sed -i 's/^Version:.*/Version: 1.0+roothide.1/; s/^Depends:.*/Depends: firmware (>= 15.0), roothide (>= 0.1.0), ellekit, com.opa334.ccsupport/' "$hpd_stage/DEBIAN/control"
dpkg-deb -Zzstd -b "$hpd_stage" "$hpd_output"
