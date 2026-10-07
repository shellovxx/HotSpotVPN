#import <Preferences/PSListController.h>
#import <Preferences/PSSpecifier.h>
#import <notify.h>
#import <objc/runtime.h>
#import "../src/preferences.h"
#define HPDString(s) [NSString stringWithUTF8String:(s)]

// Register the subclass at runtime: Procursus static arm64e class metadata fails on iOS 16.
%config(generator=internal);
%subclass HPDRootListController : PSListController
%new
- (NSMutableArray *)specifiers {
    Ivar ivar = class_getInstanceVariable(objc_getClass("PSListController"), "_specifiers");
    if (!ivar) return nil;
    NSMutableArray *specifiers = object_getIvar(self, ivar);
    if (!specifiers) {
        NSBundle *bundle = [NSBundle bundleWithPath:HPDString("/var/jb/Library/PreferenceBundles/HotspotVPNDNSPrefs.bundle")];
        specifiers = [[self loadSpecifiersFromPlistName:HPDString("Root") target:(PSListController *)self bundle:bundle] retain];
        object_setIvar(self, ivar, specifiers);
    }
    return specifiers;
}
%new
- (id)readDNSPreference:(PSSpecifier *)specifier {
    NSString *key = [specifier propertyForKey:HPDString("key")];
    if (!key) return nil;
    CFPropertyListRef value = hpd_copy_preference([key UTF8String]);
    return value ? [(id)value autorelease] : [specifier propertyForKey:HPDString("default")];
}
%new
- (void)setDNSPreference:(id)value specifier:(PSSpecifier *)specifier {
    NSString *key = [specifier propertyForKey:HPDString("key")];
    if (!key || !value) return;
    if (!hpd_set_preference([key UTF8String], (CFPropertyListRef)value)) {
        NSLog(HPDString("[HotspotVPN DNS] Could not save %@"), key);
        return;
    }
    notify_post(HPD_NOTIFICATION);
}
%end

%ctor { %init; }
