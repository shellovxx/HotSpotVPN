#include "preferences.h"
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdio.h>
#include <limits.h>
#include <roothide.h>

#define HPD_PREFS "/var/mobile/Library/Preferences/local.hotspotvpndns.plist"

static CFStringRef string(const char *text) {
    return CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
}

static CFDictionaryRef copy_dictionary(const char *path);

/* Settings and bootpd share an explicit RootHide path. CFPreferences domains
 * otherwise resolve through the system cfprefsd, outside the bootstrap. */
CFPropertyListRef hpd_copy_preference(const char *key) {
    CFDictionaryRef dict = copy_dictionary(jbroot(HPD_PREFS));
    CFStringRef name = string(key);
    CFPropertyListRef value = dict && name ? CFDictionaryGetValue(dict, name) : NULL;
    if (value) CFRetain(value);
    if (dict) CFRelease(dict);
    if (name) CFRelease(name);
    return value;
}

int hpd_set_preference(const char *key, CFPropertyListRef value) {
    char path[PATH_MAX], temporary[PATH_MAX];
    if (!key || !value || snprintf(path, sizeof(path), "%s", jbroot(HPD_PREFS)) >= (int)sizeof(path)
        || snprintf(temporary, sizeof(temporary), "%s.XXXXXX", path) >= (int)sizeof(temporary)) return 0;
    CFDictionaryRef old = copy_dictionary(path);
    CFMutableDictionaryRef dict = old ? CFDictionaryCreateMutableCopy(NULL, 0, old)
        : CFDictionaryCreateMutable(NULL, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (old) CFRelease(old);
    CFStringRef name = string(key);
    if (!dict || !name) { if (dict) CFRelease(dict); if (name) CFRelease(name); return 0; }
    CFDictionarySetValue(dict, name, value);
    CFRelease(name);
    CFDataRef data = CFPropertyListCreateData(NULL, dict, kCFPropertyListXMLFormat_v1_0, 0, NULL);
    CFRelease(dict);
    if (!data) return 0;
    int fd = mkstemp(temporary), result = 0;
    if (fd >= 0) {
        struct stat st;
        int metadata = !fchmod(fd, 0644);
        if (!lstat(path, &st) && S_ISREG(st.st_mode) && geteuid() == 0)
            metadata = metadata && !fchown(fd, st.st_uid, st.st_gid);
        size_t length = (size_t)CFDataGetLength(data), offset = 0;
        const uint8_t *bytes = CFDataGetBytePtr(data);
        while (metadata && offset < length) {
            ssize_t n = write(fd, bytes + offset, length - offset);
            if (n < 0 && errno == EINTR) continue;
            if (n <= 0) break;
            offset += (size_t)n;
        }
        result = metadata && offset == length && !fsync(fd);
        if (close(fd)) result = 0;
        if (result && rename(temporary, path)) result = 0;
        if (!result) unlink(temporary);
    }
    CFRelease(data);
    return result;
}

static int enabled(void) {
    CFTypeRef value = hpd_copy_preference("Enabled");
    int result = value && CFGetTypeID(value) == CFBooleanGetTypeID()
        && CFBooleanGetValue(value);
    if (value) CFRelease(value);
    return result;
}

static CFDictionaryRef copy_dictionary(const char *path) {
    int fd = open(path, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return NULL;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 16384) {
        close(fd); return NULL;
    }
    size_t length = (size_t)st.st_size, offset = 0;
    uint8_t *bytes = malloc(length);
    if (!bytes) { close(fd); return NULL; }
    while (offset < length) {
        ssize_t count = read(fd, bytes + offset, length - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += (size_t)count;
    }
    close(fd);
    CFDataRef data = offset == length ? CFDataCreate(NULL, bytes, (CFIndex)length) : NULL;
    free(bytes);
    if (!data) return NULL;
    CFPropertyListRef plist = CFPropertyListCreateWithData(NULL, data, kCFPropertyListImmutable, NULL, NULL);
    CFRelease(data);
    if (plist && CFGetTypeID(plist) == CFDictionaryGetTypeID()) return (CFDictionaryRef)plist;
    if (plist) CFRelease(plist);
    return NULL;
}

static int hotspot_enabled(void) {
    /* The converted vendor CC module stores this domain in system cfprefsd. */
    CFDictionaryRef plist = copy_dictionary(jbroot(rootfs("/var/mobile/Library/Preferences/com.evgeniy.hotspotvpn.plist")));
    int result = 0;
    if (plist) {
        CFStringRef key = string("Enabled");
        CFTypeRef value = key ? CFDictionaryGetValue(plist, key) : NULL;
        result = value && CFGetTypeID(value) == CFBooleanGetTypeID() && CFBooleanGetValue(value);
        if (key) CFRelease(key);
    }
    if (plist) CFRelease(plist);
    return result;
}

int hpd_selected_dns(uint8_t dns[4]) {
    if (!hotspot_enabled() || !enabled()) return 0;
    CFTypeRef mode = hpd_copy_preference("Mode");
    char mode_text[32] = "cloudflare", text[32] = "1.1.1.1";
    int valid = !mode || (CFGetTypeID(mode) == CFStringGetTypeID()
        && CFStringGetCString(mode, mode_text, sizeof(mode_text), kCFStringEncodingUTF8));
    if (mode) CFRelease(mode);
    if (!valid) return 0;
    if (!strcmp(mode_text, "google")) strcpy(text, "8.8.8.8");
    else if (!strcmp(mode_text, "custom")) {
        CFTypeRef custom = hpd_copy_preference("CustomDNS");
        valid = custom && CFGetTypeID(custom) == CFStringGetTypeID()
            && CFStringGetCString(custom, text, sizeof(text), kCFStringEncodingUTF8);
        if (custom) CFRelease(custom);
    } else if (strcmp(mode_text, "cloudflare")) return 0;
    uint8_t address[4];
    if (!valid || inet_pton(AF_INET, text, address) != 1
        || address[0] == 0 || address[0] == 127 || address[0] >= 224) return 0;
    memcpy(dns, address, 4);
    return 1;
}
