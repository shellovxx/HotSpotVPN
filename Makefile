ARCHS = arm64 arm64e
TARGET = iphone:clang:16.5:15.0
THEOS_PACKAGE_SCHEME = roothide
FINALPACKAGE ?= 1
DEBUG ?= 0

include $(THEOS)/makefiles/common.mk
TWEAK_NAME = HotspotVPNDNS
HotspotVPNDNS_FILES = src/HotspotVPNDNS.x src/dhcp_dns.c
HotspotVPNDNS_CFLAGS = -O2 -DNDEBUG -fvisibility=hidden -Wall -Wextra -Werror
HotspotVPNDNS_FRAMEWORKS = CoreFoundation
include $(THEOS_MAKE_PATH)/tweak.mk

after-stage::
	python3 scripts/verify_binary.py "$(THEOS_STAGING_DIR)/Library/MobileSubstrate/DynamicLibraries/HotspotVPNDNS.dylib"
