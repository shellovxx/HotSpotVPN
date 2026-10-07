#!/bin/sh
# Use the same Logos/Theos pipeline for native iOS and desktop builds.
set -eu
hpd_project=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
: "${THEOS:?Set THEOS to your Theos checkout}"
cd "$hpd_project"
set -- clean package FINALPACKAGE=1 DEBUG=0
if [ -n "${HPD_SDK:-}" ]; then
    set -- "$@" "ISYSROOT=$HPD_SDK" "SYSROOT=${HPD_LINK_SDK:-$HPD_SDK}"
fi
if [ -n "${HPD_CLANG:-}" ]; then
    hpd_cc=$HPD_CLANG
    if [ -n "${HPD_RESOURCE_DIR:-}" ]; then hpd_cc="$hpd_cc -resource-dir $HPD_RESOURCE_DIR"; fi
    hpd_ld=$hpd_cc
    if [ -n "${HPD_LD:-}" ]; then hpd_ld="$hpd_ld --ld-path=$HPD_LD"; fi
    set -- "$@" "TARGET_CC=$hpd_cc" "TARGET_LD=$hpd_ld"
fi
if [ -n "${HPD_LDID:-}" ]; then set -- "$@" "TARGET_CODESIGN=$HPD_LDID"; fi
exec make "$@"
