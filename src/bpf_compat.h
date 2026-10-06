#ifndef HPD_BPF_COMPAT_H
#define HPD_BPF_COMPAT_H
#include <sys/ioctl.h>
#include <net/if.h>
#if __has_include(<net/bpf.h>)
#include <net/bpf.h>
#else
/* XNU 8796.141.3 bsd/net/bpf.h: iOS 16 SDK omits this header. */
#define BIOCGDLT _IOR('B',106,unsigned int)
#define BIOCGETIF _IOR('B',107,struct ifreq)
#define BIOCSETIF _IOW('B',108,struct ifreq)
#define DLT_EN10MB 1
#endif
#endif
