/* Device-only regression check using the same SpecifiersFromPlist resource
 * bundle as PreferenceLoader's specifiersFromEntry implementation.
 * Compile for arm64 with Foundation, UIKit, objc and RootHide; sign with ldid.
 * `missing` reproduces 1.2.0, `present` expects a 29-point Settings icon.
 */
#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include <roothide.h>
@interface NSObject (IconCheck)
- (id)propertyForKey:(NSString *)key;
@end
int main(int argc,char **argv) { @autoreleasepool {
 if(argc!=2 || (strcmp(argv[1],"present") && strcmp(argv[1],"missing"))) return 1;
 void *handle=dlopen("/System/Library/PrivateFrameworks/Preferences.framework/Preferences",RTLD_NOW);
 if(!handle) { puts(dlerror()); return 2; }
 NSArray *(*parse)(NSDictionary*,id,id,NSString*,NSBundle*,NSString**,NSString**,id,NSMutableArray**) = dlsym(handle,"SpecifiersFromPlist");
 if(!parse) return 3;
 NSString *folder=jbroot(@"/Library/PreferenceLoader/Preferences");
 NSDictionary *plist=[NSDictionary dictionaryWithContentsOfFile:[folder stringByAppendingPathComponent:@"HotspotVPNDNS.plist"]];
 NSString *bundleName=plist[@"entry"][@"bundle"];
 NSString *bundlePath=jbroot([@"/Library/PreferenceBundles" stringByAppendingPathComponent:[bundleName stringByAppendingString:@".bundle"]]);
 NSBundle *bundle=[NSBundle bundleWithPath:bundlePath];
 NSArray *specs=parse(@{@"items":@[plist[@"entry"]]},nil,nil,@"HotspotVPNDNS",bundle,NULL,NULL,nil,NULL);
 if(specs.count!=1) { printf("specifier count=%lu\n",(unsigned long)specs.count); return 4; }
 UIImage *icon=[specs[0] propertyForKey:@"iconImage"];
 printf("PreferenceLoader resource bundle iconImage=%s size=%g,%g scale=%g\n",icon?"yes":"no",icon.size.width,icon.size.height,icon.scale);
 if(!strcmp(argv[1],"missing")) return icon?5:0;
 return icon && icon.size.width==29 && icon.size.height==29 ? 0:6;
} }
