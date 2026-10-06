/* Rootless iOS adapter; verified on iPhone 11 Pro Max / iOS 16.6.1. */
#include "dhcp_dns.h"
#include <CoreFoundation/CoreFoundation.h>
#include <arpa/inet.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include "bpf_compat.h"
#include <net/if.h>
#include <stdatomic.h>
#include <pthread.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <syslog.h>
#include <unistd.h>

#define PREFS "/var/mobile/Library/Preferences/local.hotspotvpndns.plist"
#define MAX_PACKET 4096
static _Thread_local int busy;
static _Atomic unsigned rewrites;
static ssize_t (*orig_sendto)(int,const void *,size_t,int,const struct sockaddr *,socklen_t);
static ssize_t (*orig_sendmsg)(int,const struct msghdr *,int);
static ssize_t (*orig_write)(int,const void *,size_t);
static ssize_t (*orig_writev)(int,const struct iovec *,int);
static int (*orig_ioctl)(int,unsigned long,...);
static pthread_mutex_t bpf_lock = PTHREAD_MUTEX_INITIALIZER;
static struct { int active,fd; dev_t dev,rdev; ino_t ino; } bpf_fds[64];

static void log_event(const char *format,...) {
    char message[256];
    va_list args;
    va_start(args,format); vsnprintf(message,sizeof(message),format,args); va_end(args);
    syslog(LOG_NOTICE,"[HotspotVPN DNS] %s",message);
    int fd=open("/var/mobile/Library/Logs/HotspotVPNDNS.log",O_WRONLY|O_APPEND|O_CREAT|O_NOFOLLOW,0644);
    if(fd<0) return;
    struct stat st;
    if(!fstat(fd,&st) && S_ISREG(st.st_mode) && st.st_size<131072) {
        size_t n=strlen(message);
        message[n++]='\n';
        (void)write(fd,message,n);
    }
    close(fd);
}

static int bridge_name(const char *s) {
    if (strncmp(s,"bridge",6) || !s[6]) return 0;
    for (s += 6; *s; s++) if (*s < '0' || *s > '9') return 0;
    return 1;
}

static CFTypeRef dictionary_value(CFDictionaryRef dict,const char *name) {
    CFStringRef key=CFStringCreateWithCString(NULL,name,kCFStringEncodingUTF8);
    if(!key) return NULL;
    CFTypeRef value=CFDictionaryGetValue(dict,key);
    CFRelease(key);
    return value;
}

static int selected_dns(uint8_t dns[4]) {
    int fd = open(PREFS, O_RDONLY | O_NOFOLLOW);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size <= 0 || st.st_size > 16384) {
        close(fd); return 0;
    }
    size_t len = (size_t)st.st_size;
    uint8_t *bytes = malloc(len);
    if (!bytes) { close(fd); return 0; }
    size_t off = 0;
    while (off < len) {
        ssize_t n = read(fd, bytes + off, len - off);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        off += (size_t)n;
    }
    close(fd);
    CFDataRef data = off == len ? CFDataCreate(NULL,bytes,(CFIndex)len) : NULL;
    free(bytes);
    if (!data) return 0;
    CFPropertyListRef plist = CFPropertyListCreateWithData(NULL,data,kCFPropertyListImmutable,NULL,NULL);
    CFRelease(data);
    if (!plist) return 0;
    int valid = 0;
    if (CFGetTypeID(plist) == CFDictionaryGetTypeID()) {
        CFDictionaryRef dict = (CFDictionaryRef)plist;
        CFTypeRef enabled = dictionary_value(dict,"Enabled");
        if (enabled && CFGetTypeID(enabled) == CFBooleanGetTypeID() && CFBooleanGetValue(enabled)) {
            CFStringRef mode = dictionary_value(dict,"Mode");
            char text[32]="1.1.1.1",mode_text[32];
            int text_valid=1;
            if (mode && CFGetTypeID(mode) == CFStringGetTypeID()) {
                if(!CFStringGetCString(mode,mode_text,sizeof(mode_text),kCFStringEncodingUTF8)) text_valid=0;
                else if(!strcmp(mode_text,"google")) strcpy(text,"8.8.8.8");
                else if(!strcmp(mode_text,"custom")) {
                    CFStringRef value=dictionary_value(dict,"CustomDNS");
                    text_valid=value && CFGetTypeID(value)==CFStringGetTypeID() && CFStringGetCString(value,text,sizeof(text),kCFStringEncodingUTF8);
                } else if(strcmp(mode_text,"cloudflare")) text_valid=0;
            } else if (mode) text_valid=0;
            if (text_valid &&
                inet_pton(AF_INET,text,dns) == 1 && dns[0] != 0 && dns[0] != 127 && dns[0] < 224) valid = 1;
        }
    }
    CFRelease(plist);
    return valid;
}

static int hotspot_server(const uint8_t server[4]) {
    struct ifaddrs *all = NULL;
    if (getifaddrs(&all)) return 0;
    int found = 0;
    for (struct ifaddrs *it = all; it; it = it->ifa_next) {
        if (!it->ifa_addr || it->ifa_addr->sa_family != AF_INET || !bridge_name(it->ifa_name)) continue;
        const struct sockaddr_in *a = (const struct sockaddr_in *)it->ifa_addr;
        if (!memcmp(&a->sin_addr,server,4)) { found = 1; break; }
    }
    freeifaddrs(all);
    return found;
}

static uint8_t *udp_copy(int fd,const void *p,size_t n,const struct sockaddr *to,socklen_t tolen) {
    if (!p || n < 241 || n > MAX_PACKET) return NULL;
    struct sockaddr_in peer, local;
    socklen_t size = sizeof(peer);
    if (!to) {
        if (getpeername(fd,(struct sockaddr *)&peer,&size)) return NULL;
        to = (const struct sockaddr *)&peer; tolen = size;
    }
    if (tolen < sizeof(struct sockaddr_in) || to->sa_family != AF_INET ||
        ((const struct sockaddr_in *)to)->sin_port != htons(68)) return NULL;
    size = sizeof(local);
    if (getsockname(fd,(struct sockaddr *)&local,&size) || size < sizeof(local) ||
        local.sin_family != AF_INET || local.sin_port != htons(67)) return NULL;
    int type; size = sizeof(type);
    if (getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&size) || type != SOCK_DGRAM) return NULL;
    uint8_t server[4], dns[4];
    if (!hpd_server_id(p,n,server) || !hotspot_server(server) || !selected_dns(dns)) return NULL;
    uint8_t *copy = malloc(n);
    if (!copy) return NULL;
    memcpy(copy,p,n);
    if (hpd_rewrite_dhcp(copy,n,dns,server) != 1) { free(copy); return NULL; }
    return copy;
}

static int hooked_ioctl(int fd,unsigned long request,...) {
    /* Darwin arm64 passes variadic arguments on the stack. Match libc's ABI. */
    va_list args;
    va_start(args,request);
    void *arg=va_arg(args,void *);
    va_end(args);
    int result=orig_ioctl(fd,request,arg);
    int saved=errno;
    if (request==BIOCSETIF && result==0 && fd>=0) {
        struct stat st;
        struct ifreq *ifr=arg;
        int active=ifr && memchr(ifr->ifr_name,0,IFNAMSIZ) && bridge_name(ifr->ifr_name)
            && !fstat(fd,&st) && S_ISCHR(st.st_mode);
        unsigned slot=(unsigned)fd%64;
        pthread_mutex_lock(&bpf_lock);
        bpf_fds[slot].active=active;
        bpf_fds[slot].fd=fd;
        if(active) { bpf_fds[slot].dev=st.st_dev; bpf_fds[slot].rdev=st.st_rdev; bpf_fds[slot].ino=st.st_ino; }
        pthread_mutex_unlock(&bpf_lock);
    }
    errno=saved;
    return result;
}

static int hotspot_bpf(int fd) {
    if(fd<0) return 0;
    struct stat st;
    if(fstat(fd,&st) || !S_ISCHR(st.st_mode)) return 0;
    unsigned slot=(unsigned)fd%64;
    pthread_mutex_lock(&bpf_lock);
    int matched=bpf_fds[slot].active && bpf_fds[slot].fd==fd
        && bpf_fds[slot].dev==st.st_dev && bpf_fds[slot].rdev==st.st_rdev
        && bpf_fds[slot].ino==st.st_ino;
    pthread_mutex_unlock(&bpf_lock);
    if(matched) return 1;
    struct ifreq ifr={0};
    unsigned dlt;
    return !ioctl(fd,BIOCGETIF,&ifr) && bridge_name(ifr.ifr_name)
        && !ioctl(fd,BIOCGDLT,&dlt) && dlt==DLT_EN10MB;
}

static uint8_t *bpf_copy(int fd,const void *p,size_t n) {
    if (!p || n < 14 + 20 + 8 + 241 || n > MAX_PACKET) return NULL;
    if (!hotspot_bpf(fd)) return NULL;
    uint8_t dns[4];
    if (!selected_dns(dns)) return NULL;
    uint8_t *copy = malloc(n);
    if (!copy) return NULL;
    memcpy(copy,p,n);
    int rewritten=hpd_rewrite_ethernet(copy,n,dns,NULL);
    if (rewritten != 1) { free(copy); return NULL; }
    return copy;
}

static uint8_t *gather(const struct iovec *v,int count,size_t *n) {
    if (!v || count < 1 || count > 16) return NULL;
    *n = 0;
    for (int i = 0; i < count; i++) {
        if (v[i].iov_len > MAX_PACKET - *n || (!v[i].iov_base && v[i].iov_len)) return NULL;
        *n += v[i].iov_len;
    }
    if (*n < 241) return NULL;
    uint8_t *p = malloc(*n);
    if (!p) return NULL;
    size_t off = 0;
    for (int i = 0; i < count; i++) {
        if (v[i].iov_len) memcpy(p + off,v[i].iov_base,v[i].iov_len);
        off += v[i].iov_len;
    }
    return p;
}

static void done(uint8_t *copy,ssize_t result,size_t n) {
    if (copy && result >= 0 && (size_t)result == n) {
        unsigned count = atomic_fetch_add(&rewrites,1) + 1;
        if (count == 1 || !(count % 32)) log_event("DHCP responses rewritten: %u",count);
    }
    free(copy);
}

static ssize_t hooked_sendto(int fd,const void *p,size_t n,int flags,const struct sockaddr *to,socklen_t len) {
    if (busy) return orig_sendto(fd,p,n,flags,to,len);
    busy = 1;
    int saved = errno;
    uint8_t *copy = udp_copy(fd,p,n,to,len);
    errno = saved;
    ssize_t result = orig_sendto(fd,copy ? copy : p,n,flags,to,len);
    saved = errno; done(copy,result,n); errno = saved;
    busy = 0; return result;
}

static ssize_t hooked_write(int fd,const void *p,size_t n) {
    if (busy) return orig_write(fd,p,n);
    busy = 1;
    int saved = errno;
    uint8_t *copy = bpf_copy(fd,p,n);
    errno = saved;
    ssize_t result = orig_write(fd,copy ? copy : p,n);
    saved = errno; done(copy,result,n); errno = saved;
    busy = 0; return result;
}

static ssize_t hooked_sendmsg(int fd,const struct msghdr *msg,int flags) {
    if (busy || !msg || msg->msg_iovlen < 1 || msg->msg_iovlen > 16) return orig_sendmsg(fd,msg,flags);
    busy = 1;
    int saved = errno;
    size_t n = 0;
    uint8_t *joined = gather(msg->msg_iov,msg->msg_iovlen,&n);
    uint8_t *copy = joined ? udp_copy(fd,joined,n,msg->msg_name,msg->msg_namelen) : NULL;
    free(joined);
    struct msghdr edited = *msg;
    struct iovec v = {copy,n};
    if (copy) { edited.msg_iov = &v; edited.msg_iovlen = 1; }
    errno = saved;
    ssize_t result = orig_sendmsg(fd,copy ? &edited : msg,flags);
    saved = errno; done(copy,result,n); errno = saved;
    busy = 0; return result;
}

static ssize_t hooked_writev(int fd,const struct iovec *v,int count) {
    if (busy) return orig_writev(fd,v,count);
    busy = 1;
    int saved = errno;
    size_t n = 0;
    uint8_t *joined = gather(v,count,&n);
    uint8_t *copy = joined ? bpf_copy(fd,joined,n) : NULL;
    free(joined);
    struct iovec edited = {copy,n};
    errno = saved;
    ssize_t result = orig_writev(fd,copy ? &edited : v,copy ? 1 : count);
    saved = errno; done(copy,result,n); errno = saved;
    busy = 0; return result;
}

__attribute__((constructor)) static void start(void) {
    const char *name = getprogname();
    if (!name || strcmp(name,"bootpd")) return;
    busy = 1;
    void (*hook)(void *,void *,void **) = dlsym(RTLD_DEFAULT,"MSHookFunction");
    const char *paths[] = {
        "/var/jb/Library/Frameworks/CydiaSubstrate.framework/CydiaSubstrate",
        "/var/jb/usr/lib/libsubstrate.dylib"
    };
    for (unsigned i = 0; !hook && i < sizeof(paths)/sizeof(paths[0]); i++) {
        void *lib = dlopen(paths[i],RTLD_NOW | RTLD_GLOBAL);
        if (lib) hook = dlsym(lib,"MSHookFunction");
    }
    if (!hook) {
        log_event("hook API unavailable in %s; inactive",name);
        busy = 0; return;
    }
    unsigned installed = 0;
#define INSTALL(symbol) do { \
    void *target = dlsym(RTLD_DEFAULT,#symbol); \
    if (target) { hook(target,(void *)hooked_##symbol,(void **)&orig_##symbol); \
        if (orig_##symbol) installed++; } \
} while (0)
    INSTALL(ioctl); INSTALL(sendto); INSTALL(sendmsg); INSTALL(write); INSTALL(writev);
#undef INSTALL
    log_event("%u DHCP transport hooks installed in %s pid=%d (runtime CF keys)",installed,name,getpid());
    busy = 0;
}
