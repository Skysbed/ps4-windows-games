#ifndef _PCPS4_WINE_COMPAT_H
#include <machine/segments.h>
#include <machine/trap.h>
#define _PCPS4_WINE_COMPAT_H
/* device type classes (sys/conf.h) */
#ifndef D_TAPE
#define D_TAPE 1
#define D_DISK 2
#define D_TTY  3
#define D_MEM  4
#endif
/* ioctls */
#ifndef FIODTYPE
#define FIODTYPE 0x4004667a
#endif
#ifndef SIOCATMARK
#define SIOCATMARK 0x40047307
#endif
#ifndef TIOCGWINSZ
#define TIOCGWINSZ 0x40087468
#endif
/* mount flags (sys/mount.h) */
#ifndef MNT_RDONLY
#define MNT_RDONLY 0x0000000000000001ULL
#define MNT_LOCAL  0x0000000000001000ULL
#define MNT_NOWAIT 2
#define MNT_WAIT   1
#endif
/* kern.clockrate (sys/time.h / sysctl) */
#ifndef KERN_CLOCKRATE
#define KERN_CLOCKRATE 12
#endif
#ifndef _PCPS4_CLOCKINFO
#define _PCPS4_CLOCKINFO
struct clockinfo { int hz; int tick; int spare; int stathz; int profhz; };
#endif
/* cpu time states (sys/resource.h) */
#ifndef CPUSTATES
#define CPUSTATES 5
#define CP_USER 0
#define CP_NICE 1
#define CP_SYS  2
#define CP_INTR 3
#define CP_IDLE 4
#endif
#endif

/* functions OpenOrbis does not prototype */
struct statfs;
int getmntinfo(struct statfs **, int);
int pthread_getthreadid_np(void);
#ifndef _PCPS4_BPF
#define _PCPS4_BPF
#define _PCPS4_LINUX_FILTER_H  /* suppress the shim header if also included */
struct sock_filter { unsigned short code; unsigned char jt; unsigned char jf; unsigned int k; };
struct sock_fprog { unsigned short len; struct sock_filter *filter; };
#define BPF_LD 0x00
#define BPF_W 0x00
#define BPF_ABS 0x20
#define BPF_JMP 0x05
#define BPF_JEQ 0x10
#define BPF_K 0x00
#define BPF_RET 0x06
#define BPF_STMT(code,k) { (unsigned short)(code), 0, 0, (unsigned int)(k) }
#define BPF_JUMP(code,k,jt,jf) { (unsigned short)(code), (unsigned char)(jt), (unsigned char)(jf), (unsigned int)(k) }
#define SKF_AD_OFF (-0x1000)
#define SKF_AD_IFINDEX 8
#define SKF_NET_OFF (-0x100000)
#define SKF_LL_OFF  (-0x200000)
#endif
