#ifndef HPD_PREFERENCES_H
#define HPD_PREFERENCES_H
#include <CoreFoundation/CoreFoundation.h>
#include <stdint.h>
#define HPD_DOMAIN "local.hotspotvpndns"
#define HPD_NOTIFICATION "local.hotspotvpndns/prefsChanged"
/* Runtime CF strings avoid unauthenticated CFSTR isa fixups in Procursus arm64e builds. */
CFPropertyListRef hpd_copy_preference(const char *key);
int hpd_set_preference(const char *key, CFPropertyListRef value);
int hpd_selected_dns(uint8_t dns[4]);
#endif
