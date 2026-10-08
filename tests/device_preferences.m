/* Device-only integration helper: exercise the installed Settings setters and CC writer. */
#import <Foundation/Foundation.h>
#include <stdio.h>
#include <string.h>
#import <Preferences/PSSpecifier.h>
#import <Preferences/PSListController.h>
#import <objc/runtime.h>
#import <dlfcn.h>
#import <roothide.h>
#import "../src/preferences.h"

@protocol HPDSettings
- (id)readDNSPreference:(PSSpecifier *)specifier;
- (void)setDNSPreference:(id)value specifier:(PSSpecifier *)specifier;
@end

int main(int argc, char **argv) {
    @autoreleasepool {
        if (argc == 2 && !strcmp(argv[1], "panel")) {
            NSBundle *bundle = [NSBundle bundleWithPath:jbroot([NSString stringWithUTF8String:"/Library/PreferenceBundles/HotspotVPNDNSPrefs.bundle"])];
            if (![bundle load]) return 1;
            PSListController *controller = class_createInstance([bundle principalClass], 0);
            NSArray *specifiers = [controller specifiers];
            if (specifiers.count != 5) { fprintf(stderr, "Expected 5 Settings specifiers, got %lu\n", (unsigned long)specifiers.count); return 2; }
            unsigned fields = 0;
            for (PSSpecifier *specifier in specifiers) {
                NSString *key = [specifier propertyForKey:[NSString stringWithUTF8String:"key"]];
                if (!key) continue;
                id value = [(id<HPDSettings>)controller readDNSPreference:specifier];
                if (!value) return 2;
                printf("%s=%s\n", key.UTF8String, [[value description] UTF8String]);
                fields++;
            }
            [controller release];
            if (fields != 3) return 2;
            puts("Settings panel: 5 specifiers, 3 bound values");
        } else if (argc == 3 && !strcmp(argv[1], "cc")) {
            void *bundle = dlopen(jbroot("/Library/ControlCenter/Bundles/HotspotVPNCC.bundle/HotspotVPNCC"), RTLD_NOW);
            void (*setEnabled)(BOOL) = bundle ? dlsym(bundle, "TSSetEnabled") : NULL;
            if (!setEnabled) { fprintf(stderr, "CC load failed: %s\n", dlerror()); return 1; }
            setEnabled(!strcmp(argv[2], "true"));
        } else if (argc == 4 && !strcmp(argv[1], "set")) {
            NSBundle *bundle = [NSBundle bundleWithPath:jbroot([NSString stringWithUTF8String:"/Library/PreferenceBundles/HotspotVPNDNSPrefs.bundle"])];
            if (![bundle load]) { fprintf(stderr, "Settings bundle load failed\n"); return 1; }
            Class cls = [bundle principalClass];
            id<HPDSettings> controller = (id)class_createInstance(cls, 0);
            if (!controller) return 1;
            PSSpecifier *specifier = [PSSpecifier emptyGroupSpecifier];
            [specifier setProperty:[NSString stringWithUTF8String:argv[2]] forKey:[NSString stringWithUTF8String:"key"]];
            id value = !strcmp(argv[2], "Enabled") ? (id)[NSNumber numberWithBool:!strcmp(argv[3], "true")]
                : (id)[NSString stringWithUTF8String:argv[3]];
            [controller setDNSPreference:value specifier:specifier];
            if (![[controller readDNSPreference:specifier] isEqual:value]) return 2;
            [(id)controller release];
        } else if (argc != 1) {
            fprintf(stderr, "Usage: device_preferences [panel | cc true|false | set KEY VALUE]\n"); return 1;
        }
        uint8_t dns[4];
        if (hpd_selected_dns(dns)) printf("%u.%u.%u.%u\n", dns[0], dns[1], dns[2], dns[3]);
        else puts("system");
    }
    return 0;
}
