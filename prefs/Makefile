include $(THEOS)/makefiles/common.mk
BUNDLE_NAME = HotspotVPNDNSPrefs
HotspotVPNDNSPrefs_FILES = HPDRootListController.x ../src/preferences.c
HotspotVPNDNSPrefs_CFLAGS = -O2 -DNDEBUG -fvisibility=hidden -Wall -Wextra -Werror
HotspotVPNDNSPrefs_USE_MODULES = 0
HotspotVPNDNSPrefs_FRAMEWORKS = UIKit Foundation CoreFoundation
HotspotVPNDNSPrefs_PRIVATE_FRAMEWORKS = Preferences
HotspotVPNDNSPrefs_INSTALL_PATH = /Library/PreferenceBundles
include $(THEOS_MAKE_PATH)/bundle.mk
