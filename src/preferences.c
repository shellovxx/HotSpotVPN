#include "preferences.h"
#include <arpa/inet.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static CFStringRef string(const char *text) {
    return CFStringCreateWithCString(NULL, text, kCFStringEncodingUTF8);
}

CFPropertyListRef hpd_copy_preference(const char *key) {
    CFStringRef domain = string(HPD_DOMAIN), user = string("mobile"), name = string(key);
    CFPropertyListRef value = NULL;
    if (domain && user && name) {
        CFPreferencesSynchronize(domain, user, kCFPreferencesAnyHost);
        value = CFPreferencesCopyValue(name, domain, user, kCFPreferencesAnyHost);
    }
    if (domain) CFRelease(domain);
    if (user) CFRelease(user);
    if (name) CFRelease(name);
    return value;
}

int hpd_set_preference(const char *key, CFPropertyListRef value) {
    CFStringRef domain = string(HPD_DOMAIN), user = string("mobile"), name = string(key);
    int result = 0;
    if (domain && user && name) {
        CFPreferencesSetValue(name, value, domain, user, kCFPreferencesAnyHost);
        result = CFPreferencesSynchronize(domain, user, kCFPreferencesAnyHost);
    }
    if (domain) CFRelease(domain);
    if (user) CFRelease(user);
    if (name) CFRelease(name);
    return result;
}

static int enabled(void) {
    CFTypeRef value = hpd_copy_preference("Enabled");
    int result = value && CFGetTypeID(value) == CFBooleanGetTypeID()
        && CFBooleanGetValue(value);
    if (value) CFRelease(value);
    return result;
}

static int hotspot_enabled(void) {
    int fd = open("/var/mobile/Library/Preferences/com.evgeniy.hotspotvpn.plist", O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 16384) {
        close(fd); return 0;
    }
    size_t length = (size_t)st.st_size, offset = 0;
    uint8_t *bytes = malloc(length);
    if (!bytes) { close(fd); return 0; }
    while (offset < length) {
        ssize_t count = read(fd, bytes + offset, length - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) break;
        offset += (size_t)count;
    }
    close(fd);
    CFDataRef data = offset == length ? CFDataCreate(NULL, bytes, (CFIndex)length) : NULL;
    free(bytes);
    if (!data) return 0;
    CFPropertyListRef plist = CFPropertyListCreateWithData(NULL, data, kCFPropertyListImmutable, NULL, NULL);
    CFRelease(data);
    int result = 0;
    if (plist && CFGetTypeID(plist) == CFDictionaryGetTypeID()) {
        CFStringRef key = string("Enabled");
        CFTypeRef value = key ? CFDictionaryGetValue((CFDictionaryRef)plist, key) : NULL;
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
