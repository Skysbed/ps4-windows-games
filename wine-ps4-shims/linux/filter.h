#ifndef _PCPS4_LINUX_FILTER_H
#define _PCPS4_LINUX_FILTER_H
#include <stdint.h>
struct sock_filter { unsigned short code; unsigned char jt; unsigned char jf; unsigned int k; };
struct sock_fprog { unsigned short len; struct sock_filter *filter; };
#define BPF_LD   0x00
#define BPF_W    0x00
#define BPF_ABS  0x20
#define BPF_JMP  0x05
#define BPF_JEQ  0x10
#define BPF_K    0x00
#define BPF_RET  0x06
#define BPF_STMT(code,k)        { (unsigned short)(code), 0, 0, (unsigned int)(k) }
#define BPF_JUMP(code,k,jt,jf)  { (unsigned short)(code), (unsigned char)(jt), (unsigned char)(jf), (unsigned int)(k) }
#define SKF_AD_OFF     (-0x1000)
#define SKF_AD_IFINDEX 8
#endif
