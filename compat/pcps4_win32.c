#define _POSIX_C_SOURCE 200112L
#include "pcps4_win32.h"

#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <unistd.h>
#include <dirent.h>

#ifndef S_OK
#define S_OK ((HRESULT)0)
#endif

#define TLS_KEYS 128
#define TLS_THREADS 64
static volatile uint32_t g_tls_bits[TLS_KEYS / 32];
static volatile uint32_t g_tls_lock;
static struct { uint32_t tid; uintptr_t v[TLS_KEYS]; } g_tls[TLS_THREADS];

static void spin_lock(volatile uint32_t *p){ while(__atomic_exchange_n(p,1,__ATOMIC_ACQUIRE)!=0) (void)pcps4_sleep_ms(0); }
static void spin_unlock(volatile uint32_t *p){ __atomic_store_n(p,0,__ATOMIC_RELEASE); }

static int tls_slot(uint32_t tid, int create){
    int free_slot=-1;
    for(int i=0;i<TLS_THREADS;++i){
        if(g_tls[i].tid==tid) return i;
        if(free_slot<0 && g_tls[i].tid==0) free_slot=i;
    }
    if(create && free_slot>=0){ g_tls[free_slot].tid=tid; memset(g_tls[free_slot].v,0,sizeof(g_tls[free_slot].v)); return free_slot; }
    return -1;
}
PCPS4_MS_ABI DWORD pcps4_abi_TlsAlloc(void){
    spin_lock(&g_tls_lock);
    for(DWORD i=0;i<TLS_KEYS;++i){
        uint32_t w=i/32u,b=1u<<(i%32u);
        if((g_tls_bits[w]&b)==0){ g_tls_bits[w]|=b; spin_unlock(&g_tls_lock); return i; }
    }
    spin_unlock(&g_tls_lock); return 0xffffffffu;
}
PCPS4_MS_ABI BOOL pcps4_abi_TlsFree(DWORD i){
    if(i>=TLS_KEYS) return FALSE;
    spin_lock(&g_tls_lock);
    uint32_t w=i/32u,b=1u<<(i%32u);
    if((g_tls_bits[w]&b)==0){ spin_unlock(&g_tls_lock); return FALSE; }
    g_tls_bits[w]&=~b;
    for(int n=0;n<TLS_THREADS;++n) g_tls[n].v[i]=0;
    spin_unlock(&g_tls_lock); return TRUE;
}
PCPS4_MS_ABI LPVOID pcps4_abi_TlsGetValue(DWORD i){
    if(i>=TLS_KEYS) return NULL;
    uint32_t tid=pcps4_get_current_thread_id(); spin_lock(&g_tls_lock);
    int s=tls_slot(tid,0); LPVOID v=s>=0?(LPVOID)g_tls[s].v[i]:NULL; spin_unlock(&g_tls_lock); return v;
}
PCPS4_MS_ABI BOOL pcps4_abi_TlsSetValue(DWORD i, LPVOID v){
    if(i>=TLS_KEYS) return FALSE;
    uint32_t tid=pcps4_get_current_thread_id(); spin_lock(&g_tls_lock);
    int s=tls_slot(tid,1);
    if(s<0){ spin_unlock(&g_tls_lock); return FALSE; }
    g_tls[s].v[i]=(uintptr_t)v; spin_unlock(&g_tls_lock); return TRUE;
}

PCPS4_MS_ABI LONG pcps4_abi_InterlockedExchange(volatile LONG *v,LONG e){return __atomic_exchange_n(v,e,__ATOMIC_SEQ_CST);}
PCPS4_MS_ABI LONG pcps4_abi_InterlockedCompareExchange(volatile LONG *d,LONG e,LONG c){__atomic_compare_exchange_n(d,&c,e,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST);return c;}
PCPS4_MS_ABI LONG pcps4_abi_InterlockedExchangeAdd(volatile LONG *v,LONG a){return __atomic_fetch_add(v,a,__ATOMIC_SEQ_CST);}

#define CS_SLOTS 256
static struct { uintptr_t addr; volatile uint32_t lock; uint32_t owner; uint32_t recursion; } g_cs[CS_SLOTS];
static volatile uint32_t g_cs_registry;
static void cs_registry(void){spin_lock(&g_cs_registry);}
static void cs_registry_unlock(void){spin_unlock(&g_cs_registry);}
static void *cs_find(void *addr,int create){
    struct { uintptr_t addr; volatile uint32_t lock; uint32_t owner; uint32_t recursion; } *free_slot=NULL;
    for(int i=0;i<CS_SLOTS;++i){ if(g_cs[i].addr==(uintptr_t)addr) return &g_cs[i]; if(!free_slot && g_cs[i].addr==0) free_slot=(void*)&g_cs[i]; }
    if(create && free_slot){ memset(free_slot,0,sizeof(*free_slot)); free_slot->addr=(uintptr_t)addr; return free_slot; }
    return NULL;
}
PCPS4_MS_ABI void pcps4_abi_InitializeCriticalSection(void *cs){ if(!cs)return; cs_registry(); (void)cs_find(cs,1); cs_registry_unlock(); }
PCPS4_MS_ABI BOOL pcps4_abi_InitializeCriticalSectionAndSpinCount(void *cs,DWORD spin){(void)spin;pcps4_abi_InitializeCriticalSection(cs);return cs!=NULL;}
PCPS4_MS_ABI void pcps4_abi_DeleteCriticalSection(void *cs){if(!cs)return;cs_registry();void *p=cs_find(cs,0);if(p)memset(p,0,sizeof(g_cs[0]));cs_registry_unlock();}
PCPS4_MS_ABI void pcps4_abi_EnterCriticalSection(void *cs){
    if(!cs)return; cs_registry(); void *p=cs_find(cs,1); cs_registry_unlock(); if(!p)return;
    uint32_t tid=pcps4_get_current_thread_id(); struct {uintptr_t addr;volatile uint32_t lock;uint32_t owner;uint32_t recursion;} *s=p;
    if(s->owner==tid){++s->recursion;return;}
    for(;;){uint32_t e=0;if(__atomic_compare_exchange_n(&s->lock,&e,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){s->owner=tid;s->recursion=1;return;} (void)pcps4_sleep_ms(0);}
}
PCPS4_MS_ABI BOOL pcps4_abi_TryEnterCriticalSection(void *cs){
    if(!cs)return FALSE; cs_registry(); void *p=cs_find(cs,1); cs_registry_unlock(); if(!p)return FALSE;
    uint32_t tid=pcps4_get_current_thread_id(); struct {uintptr_t addr;volatile uint32_t lock;uint32_t owner;uint32_t recursion;} *s=p;
    if(s->owner==tid){++s->recursion;return TRUE;} uint32_t e=0;
    if(__atomic_compare_exchange_n(&s->lock,&e,1,0,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)){s->owner=tid;s->recursion=1;return TRUE;} return FALSE;
}
PCPS4_MS_ABI void pcps4_abi_LeaveCriticalSection(void *cs){
    if(!cs)return;cs_registry();void *p=cs_find(cs,0);cs_registry_unlock();if(!p)return;
    struct {uintptr_t addr;volatile uint32_t lock;uint32_t owner;uint32_t recursion;} *s=p; if(s->owner!=pcps4_get_current_thread_id()||s->recursion==0)return;
    if(--s->recursion==0){s->owner=0;__atomic_store_n(&s->lock,0,__ATOMIC_RELEASE);}
}

static int prot(DWORD p){switch(p&0xffu){case PAGE_NOACCESS:return PROT_NONE;case PAGE_READONLY:return PROT_READ;case PAGE_READWRITE:return PROT_READ|PROT_WRITE;case PAGE_EXECUTE_READ:return PROT_READ|PROT_EXEC;case PAGE_EXECUTE_READWRITE:return PROT_READ|PROT_WRITE|PROT_EXEC;default:return PROT_READ|PROT_WRITE;}}
PCPS4_MS_ABI BOOL pcps4_abi_VirtualProtect(LPVOID a,SIZE_T n,DWORD p,DWORD *old){
    long ps=sysconf(_SC_PAGESIZE); if(!a||!n||ps<=0)return FALSE; uintptr_t s=(uintptr_t)a;uintptr_t e=s+n; s&=~((uintptr_t)ps-1u);e=(e+(uintptr_t)ps-1u)&~((uintptr_t)ps-1u); if(old)*old=PAGE_READWRITE; return mprotect((void*)s,e-s,prot(p))==0;
}
PCPS4_MS_ABI SIZE_T pcps4_abi_VirtualQuery(const void *a,void *b,SIZE_T n){
    struct {void *BaseAddress;void *AllocationBase;DWORD AllocationProtect;SIZE_T RegionSize;DWORD State;DWORD Protect;DWORD Type;} i;
    if(!a||!b||n<sizeof(i))return 0;memset(&i,0,sizeof(i));i.BaseAddress=(void*)a;i.AllocationBase=(void*)a;i.AllocationProtect=PAGE_READWRITE;i.RegionSize=4096;i.State=MEM_COMMIT;i.Protect=PAGE_READWRITE;i.Type=0x20000u;memcpy(b,&i,sizeof(i));return sizeof(i);
}
PCPS4_MS_ABI BOOL pcps4_abi_FlushInstructionCache(HANDLE p,const void *a,SIZE_T n){(void)p;__builtin___clear_cache((char*)a,(char*)a+n);return TRUE;}

static int strw(const char *s,LPSTR b,DWORD z){
    size_t n=strlen(s);if(!b||z==0)return (int)(n+1);if(n+1>z){if(z>1)memcpy(b,s,z-1);b[z-1]=0;return 0;}memcpy(b,s,n+1);return (int)n;
}
static int strww(const char *s,LPWSTR b,DWORD z){
    size_t n=strlen(s);if(!b||z==0)return (int)(n+1);if(n+1>z){for(size_t i=0;i+1<z;++i)b[i]=(WCHAR)(unsigned char)s[i];b[z-1]=0;return 0;}for(size_t i=0;i<n;++i)b[i]=(WCHAR)(unsigned char)s[i];b[n]=0;return (int)n;
}
static char g_cwd[512]="/app0/game";
PCPS4_MS_ABI DWORD pcps4_abi_GetCurrentDirectoryA(DWORD z,LPSTR b){return (DWORD)strw("C:\\game",b,z);}
PCPS4_MS_ABI DWORD pcps4_abi_GetCurrentDirectoryW(DWORD z,LPWSTR b){return (DWORD)strww("C:\\game",b,z);}
PCPS4_MS_ABI BOOL pcps4_abi_SetCurrentDirectoryA(LPCSTR p){if(!p||!p[0])return FALSE;strncpy(g_cwd,p,sizeof(g_cwd)-1);g_cwd[sizeof(g_cwd)-1]=0;return TRUE;}
PCPS4_MS_ABI BOOL pcps4_abi_SetCurrentDirectoryW(LPCWSTR p){char u[512];if(pcps4_wide_to_utf8(p,u,sizeof(u))!=0)return FALSE;return pcps4_abi_SetCurrentDirectoryA(u);}
PCPS4_MS_ABI DWORD pcps4_abi_GetTempPathA(DWORD z,LPSTR b){int n=strw("C:\\temp\\",b,z);return n>0&&b&&z>(DWORD)n?(DWORD)n:(DWORD)(strlen("C:\\temp\\")+1);}
PCPS4_MS_ABI DWORD pcps4_abi_GetTempPathW(DWORD z,LPWSTR b){int n=strww("C:\\temp\\",b,z);return n>0&&b&&z>(DWORD)n?(DWORD)n:(DWORD)(strlen("C:\\temp\\")+1);}
PCPS4_MS_ABI UINT pcps4_abi_GetSystemDirectoryA(LPSTR b,UINT z){int n=strw("C:\\Windows\\System32",b,z);return n>0?(UINT)n:0;}
PCPS4_MS_ABI UINT pcps4_abi_GetSystemDirectoryW(LPWSTR b,UINT z){int n=strww("C:\\Windows\\System32",b,z);return n>0?(UINT)n:0;}
PCPS4_MS_ABI void pcps4_abi_GetSystemTimeAsFileTime(uint64_t *ft){struct timespec t;if(!ft||clock_gettime(CLOCK_REALTIME,&t)!=0)return;*ft=116444736000000000ULL+(uint64_t)t.tv_sec*10000000ULL+(uint64_t)t.tv_nsec/100ULL;}
PCPS4_MS_ABI void pcps4_abi_GetSystemInfo(void *p){
    struct {uint16_t arch;uint16_t reserved;DWORD page;void *min;void *max;uintptr_t mask;DWORD cpus;DWORD gran;uint16_t level;uint16_t revision;} i;
    if(!p)return;memset(&i,0,sizeof(i));i.arch=9;i.page=4096;i.min=(void*)0x10000;i.max=(void*)0x00007ffffffff000ULL;i.mask=0xff;i.cpus=8;i.gran=65536;i.level=6;memcpy(p,&i,sizeof(i));
}
PCPS4_MS_ABI BOOL pcps4_abi_IsProcessorFeaturePresent(DWORD f){(void)f;return TRUE;}
PCPS4_MS_ABI BOOL pcps4_abi_IsDebuggerPresent(void){return FALSE;}

static int host_path(LPCSTR p,char *o,size_t z){
    if(!p||!o||z<2)return -1;
    if((p[0]=='C'||p[0]=='c')&&p[1]==':'){p+=2;while(*p=='/'||*p=='\\')++p;return snprintf(o,z,"/app0/%s",p)<(int)z?0:-1;}
    if(p[0]=='/'&&(strncmp(p,"/app0/",6)==0||strncmp(p,"/data/",6)==0)){strncpy(o,p,z-1);o[z-1]=0;return 0;}
    if(snprintf(o,z,"/app0/%s",p)<(int)z)return 0; return -1;
}

#define FIND_MAGIC 0x46494e44u
typedef struct {
    uint32_t magic;
    DIR *dir;
    char pattern[260];
} pcps4_find_handle;

typedef struct {
    DWORD dwFileAttributes;
    uint64_t ftCreationTime;
    uint64_t ftLastAccessTime;
    uint64_t ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    CHAR cFileName[260];
    CHAR cAlternateFileName[14];
} pcps4_find_data_a;

typedef struct {
    DWORD dwFileAttributes;
    uint64_t ftCreationTime;
    uint64_t ftLastAccessTime;
    uint64_t ftLastWriteTime;
    DWORD nFileSizeHigh;
    DWORD nFileSizeLow;
    DWORD dwReserved0;
    DWORD dwReserved1;
    WCHAR cFileName[260];
    WCHAR cAlternateFileName[14];
} pcps4_find_data_w;

static int find_match(const char *pattern, const char *name)
{
    while (*pattern) {
        if (*pattern == '*') {
            ++pattern;
            if (!*pattern) return 1;
            while (*name) {
                if (find_match(pattern, name))
                    return 1;
                ++name;
            }
            return 0;
        }
        if (!*name) return 0;
        if (*pattern != '?' &&
            ((*pattern >= 'A' && *pattern <= 'Z')
             ? (char)(*pattern - 'A' + 'a')
             : *pattern) !=
            ((*name >= 'A' && *name <= 'Z')
             ? (char)(*name - 'A' + 'a')
             : *name))
            return 0;
        ++pattern;
        ++name;
    }
    return *name == '\0';
}

static void find_fill(void *out, const char *name, const struct stat *st)
{
    pcps4_find_data_a d;
    memset(&d,0,sizeof(d));
    d.dwFileAttributes = (st && S_ISDIR(st->st_mode)) ? 0x10u : 0x80u;
    if (st) {
        uint64_t size=(uint64_t)st->st_size;
        d.nFileSizeHigh=(DWORD)(size>>32);
        d.nFileSizeLow=(DWORD)size;
    }
    strncpy(d.cFileName,name,sizeof(d.cFileName)-1);
    memcpy(out,&d,sizeof(d));
}

static int split_find_path(LPCSTR path, char *directory, size_t dir_size,
                           char *pattern, size_t pattern_size)
{
    char host[1024], *slash;
    if (host_path(path,host,sizeof(host))!=0) return -1;
    slash=strrchr(host,'/');
    if(!slash) return -1;
    *slash='\0';
    strncpy(directory,host,dir_size-1); directory[dir_size-1]='\0';
    strncpy(pattern,slash+1,pattern_size-1); pattern[pattern_size-1]='\0';
    return 0;
}

static int find_next_a(pcps4_find_handle *h, void *out)
{
    struct dirent *ent;
    if(!h||h->magic!=FIND_MAGIC||!out)return 0;
    while((ent=readdir(h->dir))!=NULL){
        char full[1024];
        struct stat st;
        if(!find_match(h->pattern,ent->d_name))continue;
        snprintf(full,sizeof(full),"%s/%s", ".", ent->d_name);
        /* directory already scoped by opendir; use entry metadata where possible. */
        memset(&st,0,sizeof(st));
        if(fstatat(dirfd(h->dir),ent->d_name,&st,0)!=0)
            continue;
        find_fill(out,ent->d_name,&st);
        return 1;
    }
    return 0;
}

PCPS4_MS_ABI HANDLE pcps4_abi_FindFirstFileA(LPCSTR path, void *out)
{
    char directory[1024], pattern[260];
    pcps4_find_handle *h;
    if(!out||split_find_path(path,directory,sizeof(directory),pattern,sizeof(pattern))!=0)
        return INVALID_HANDLE_VALUE;
    h=calloc(1,sizeof(*h)); if(!h)return INVALID_HANDLE_VALUE;
    h->dir=opendir(directory);
    if(!h->dir){free(h);return INVALID_HANDLE_VALUE;}
    h->magic=FIND_MAGIC;
    strncpy(h->pattern,pattern,sizeof(h->pattern)-1);
    if(!find_next_a(h,out)){
        closedir(h->dir); free(h); return INVALID_HANDLE_VALUE;
    }
    return h;
}

PCPS4_MS_ABI HANDLE pcps4_abi_FindFirstFileW(LPCWSTR path, void *out)
{
    char utf8[1024];
    if(pcps4_wide_to_utf8(path,utf8,sizeof(utf8))!=0)return INVALID_HANDLE_VALUE;
    pcps4_find_data_a temp;
    HANDLE h=pcps4_abi_FindFirstFileA(utf8,&temp);
    if(h==INVALID_HANDLE_VALUE)return h;
    pcps4_find_handle *fh=(pcps4_find_handle*)h;
    pcps4_find_data_w w; memset(&w,0,sizeof(w));
    w.dwFileAttributes=temp.dwFileAttributes;
    w.ftCreationTime=temp.ftCreationTime; w.ftLastAccessTime=temp.ftLastAccessTime;
    w.ftLastWriteTime=temp.ftLastWriteTime; w.nFileSizeHigh=temp.nFileSizeHigh; w.nFileSizeLow=temp.nFileSizeLow;
    for(size_t i=0;i<sizeof(w.cFileName)/sizeof(w.cFileName[0])-1&&temp.cFileName[i];++i)w.cFileName[i]=(WCHAR)(unsigned char)temp.cFileName[i];
    memcpy(out,&w,sizeof(w));
    return fh;
}

PCPS4_MS_ABI BOOL pcps4_abi_FindNextFileA(HANDLE handle, void *out)
{
    return find_next_a((pcps4_find_handle*)handle,out) ? TRUE : FALSE;
}
PCPS4_MS_ABI BOOL pcps4_abi_FindNextFileW(HANDLE handle, void *out)
{
    pcps4_find_data_a a;
    if(!find_next_a((pcps4_find_handle*)handle,&a))return FALSE;
    pcps4_find_data_w w; memset(&w,0,sizeof(w));
    w.dwFileAttributes=a.dwFileAttributes; w.ftCreationTime=a.ftCreationTime; w.ftLastAccessTime=a.ftLastAccessTime;
    w.ftLastWriteTime=a.ftLastWriteTime; w.nFileSizeHigh=a.nFileSizeHigh; w.nFileSizeLow=a.nFileSizeLow;
    for(size_t i=0;i<259&&a.cFileName[i];++i)w.cFileName[i]=(WCHAR)(unsigned char)a.cFileName[i];
    memcpy(out,&w,sizeof(w)); return TRUE;
}
PCPS4_MS_ABI BOOL pcps4_abi_FindClose(HANDLE handle)
{
    pcps4_find_handle *h=(pcps4_find_handle*)handle;
    if(!h||h->magic!=FIND_MAGIC)return FALSE;
    h->magic=0; if(h->dir)closedir(h->dir); free(h); return TRUE;
}

PCPS4_MS_ABI BOOL pcps4_abi_GetFileAttributesExA(LPCSTR path,DWORD info_level,void*out)
{
    struct stat st; char host[1024];
    if(info_level!=0||!out||host_path(path,host,sizeof(host))!=0||stat(host,&st)!=0)return FALSE;
    pcps4_find_data_a d; memset(&d,0,sizeof(d));
    d.dwFileAttributes=S_ISDIR(st.st_mode)?0x10u:0x80u;
    d.nFileSizeHigh=(DWORD)((uint64_t)st.st_size>>32); d.nFileSizeLow=(DWORD)st.st_size;
    memcpy(out,&d,sizeof(d)); return TRUE;
}
PCPS4_MS_ABI BOOL pcps4_abi_GetFileAttributesExW(LPCWSTR path,DWORD level,void*out)
{
    char u[1024]; if(pcps4_wide_to_utf8(path,u,sizeof(u))!=0)return FALSE;
    return pcps4_abi_GetFileAttributesExA(u,level,out);
}
PCPS4_MS_ABI DWORD pcps4_abi_GetLogicalDrives(void){return 1u;}
PCPS4_MS_ABI UINT pcps4_abi_GetDriveTypeA(LPCSTR root){(void)root;return 3u;}
PCPS4_MS_ABI UINT pcps4_abi_GetDriveTypeW(LPCWSTR root){(void)root;return 3u;}

PCPS4_MS_ABI DWORD pcps4_abi_GetFileAttributesA(LPCSTR p){char h[1024];struct stat s;if(host_path(p,h,sizeof(h))!=0||stat(h,&s)!=0)return 0xffffffffu;return S_ISDIR(s.st_mode)?0x10u:0x80u;}
PCPS4_MS_ABI DWORD pcps4_abi_GetFileAttributesW(LPCWSTR p){char u[1024];if(pcps4_wide_to_utf8(p,u,sizeof(u))!=0)return 0xffffffffu;return pcps4_abi_GetFileAttributesA(u);}

typedef intptr_t LRESULT; typedef uintptr_t WPARAM; typedef intptr_t LPARAM;
typedef LRESULT (PCPS4_MS_ABI *wndproc_t)(HANDLE,UINT,WPARAM,LPARAM);
typedef struct {uint32_t cbSize;uint32_t style;wndproc_t wndproc;int32_t cbClsExtra;int32_t cbWndExtra;HANDLE hInstance;HANDLE hIcon;HANDLE hCursor;HANDLE hbrBackground;const char *menu;const char *class_name;HANDLE hIconSm;} WNDA;
typedef struct {uint32_t atom;char name[128];wndproc_t proc;} WCLASS;
typedef struct {uint32_t magic;wndproc_t proc;int width;int height;intptr_t user;} WIN;
typedef struct {HANDLE hwnd;UINT message;UINT pad;WPARAM wParam;LPARAM lParam;DWORD time;int32_t x;int32_t y;DWORD extra;} MSGX;
typedef struct {int32_t left,top,right,bottom;} RECTX;
#define WIN_MAGIC 0x57594e58u
static WCLASS g_classes[32]; static volatile uint32_t g_window_lock; static volatile uint32_t g_quit;
static WCLASS *class_find(const char *n){for(int i=0;i<32;++i)if(g_classes[i].atom&&strcmp(g_classes[i].name,n)==0)return &g_classes[i];return NULL;}
PCPS4_MS_ABI WORD pcps4_abi_RegisterClassExA(const void *p){const WNDA *w=p;if(!w||!w->class_name)return 0;spin_lock(&g_window_lock);WCLASS *e=class_find(w->class_name);if(e){spin_unlock(&g_window_lock);return (WORD)e->atom;}for(int i=0;i<32;++i)if(!g_classes[i].atom){g_classes[i].atom=(uint32_t)i+1;strncpy(g_classes[i].name,w->class_name,127);g_classes[i].name[127]=0;g_classes[i].proc=w->wndproc;WORD a=(WORD)g_classes[i].atom;spin_unlock(&g_window_lock);return a;}spin_unlock(&g_window_lock);return 0;}
PCPS4_MS_ABI WORD pcps4_abi_RegisterClassExW(const void *p){const WNDA *w=p;void *np=NULL;const void *nptr=NULL;char n[128];if(!w)return 0;memcpy(&nptr,(const uint8_t*)p+offsetof(WNDA,class_name),sizeof(nptr));if(!nptr||pcps4_wide_to_utf8((const WCHAR*)nptr,n,sizeof(n))!=0)return 0;WNDA x=*w;x.class_name=n;np=&x;(void)np;return pcps4_abi_RegisterClassExA(&x);}
PCPS4_MS_ABI BOOL pcps4_abi_UnregisterClassA(LPCSTR n,HANDLE i){(void)i;if(!n)return FALSE;spin_lock(&g_window_lock);WCLASS *c=class_find(n);if(c)memset(c,0,sizeof(*c));spin_unlock(&g_window_lock);return c!=NULL;}
PCPS4_MS_ABI BOOL pcps4_abi_UnregisterClassW(LPCWSTR n,HANDLE i){char u[128];if(pcps4_wide_to_utf8(n,u,sizeof(u))!=0)return FALSE;return pcps4_abi_UnregisterClassA(u,i);}
PCPS4_MS_ABI HANDLE pcps4_abi_CreateWindowExA(DWORD ex,LPCSTR cn,LPCSTR wn,DWORD st,int x,int y,int width,int height,HANDLE pa,HANDLE me,HANDLE in,LPVOID param){(void)ex;(void)wn;(void)st;(void)x;(void)y;(void)pa;(void)me;(void)in;(void)param;if(!cn)return NULL;spin_lock(&g_window_lock);WCLASS *c=class_find(cn);wndproc_t proc=c?c->proc:NULL;spin_unlock(&g_window_lock);if(!proc)return NULL;WIN *w=calloc(1,sizeof(*w));if(!w)return NULL;w->magic=WIN_MAGIC;w->proc=proc;w->width=width>0?width:1280;w->height=height>0?height:720;return w;}
PCPS4_MS_ABI HANDLE pcps4_abi_CreateWindowExW(DWORD ex,LPCWSTR cn,LPCWSTR wn,DWORD st,int x,int y,int width,int height,HANDLE pa,HANDLE me,HANDLE in,LPVOID param){char c[128],w[256];if(pcps4_wide_to_utf8(cn,c,sizeof(c))!=0)return NULL;if(wn&&pcps4_wide_to_utf8(wn,w,sizeof(w))!=0)return NULL;return pcps4_abi_CreateWindowExA(ex,c,wn?w:NULL,st,x,y,width,height,pa,me,in,param);}
static int valid_win(HANDLE h){WIN *w=h;return w&&w->magic==WIN_MAGIC;}
PCPS4_MS_ABI BOOL pcps4_abi_IsWindow(HANDLE h){return valid_win(h);}
PCPS4_MS_ABI BOOL pcps4_abi_DestroyWindow(HANDLE h){if(!valid_win(h))return FALSE;((WIN*)h)->magic=0;free(h);return TRUE;}
PCPS4_MS_ABI BOOL pcps4_abi_ShowWindow(HANDLE h,int c){(void)c;return valid_win(h);}
PCPS4_MS_ABI BOOL pcps4_abi_UpdateWindow(HANDLE h){return valid_win(h);}
PCPS4_MS_ABI LRESULT pcps4_abi_DefWindowProcA(HANDLE h,DWORD m,WPARAM w,LPARAM l){(void)h;(void)m;(void)w;(void)l;return 0;}
PCPS4_MS_ABI LRESULT pcps4_abi_DefWindowProcW(HANDLE h,DWORD m,WPARAM w,LPARAM l){return pcps4_abi_DefWindowProcA(h,m,w,l);}
PCPS4_MS_ABI BOOL pcps4_abi_PeekMessageA(void *out,HANDLE h,DWORD a,DWORD b,DWORD rem){(void)h;(void)a;(void)b;if(!out||!__atomic_load_n(&g_quit,__ATOMIC_ACQUIRE))return FALSE;MSGX m={0};m.message=0x12;memcpy(out,&m,sizeof(m));if(rem&1)__atomic_store_n(&g_quit,0,__ATOMIC_RELEASE);return TRUE;}
PCPS4_MS_ABI BOOL pcps4_abi_PeekMessageW(void *o,HANDLE h,DWORD a,DWORD b,DWORD r){return pcps4_abi_PeekMessageA(o,h,a,b,r);}
PCPS4_MS_ABI int pcps4_abi_GetMessageA(void *o,HANDLE h,DWORD a,DWORD b){if(pcps4_abi_PeekMessageA(o,h,a,b,1))return 0;if(o)memset(o,0,sizeof(MSGX));(void)pcps4_sleep_ms(1);return 1;}
PCPS4_MS_ABI BOOL pcps4_abi_TranslateMessage(const void *m){(void)m;return TRUE;}
PCPS4_MS_ABI LRESULT pcps4_abi_DispatchMessageA(const void *p){const MSGX *m=p;if(!m||!m->hwnd||!valid_win(m->hwnd))return 0;WIN *w=m->hwnd;return w->proc?w->proc(m->hwnd,m->message,m->wParam,m->lParam):0;}
PCPS4_MS_ABI LRESULT pcps4_abi_DispatchMessageW(const void *p){return pcps4_abi_DispatchMessageA(p);}
PCPS4_MS_ABI void pcps4_abi_PostQuitMessage(int code){(void)code;__atomic_store_n(&g_quit,1,__ATOMIC_RELEASE);}
PCPS4_MS_ABI BOOL pcps4_abi_GetClientRect(HANDLE h,void *p){if(!valid_win(h)||!p)return FALSE;WIN*w=h;RECTX r={0,0,w->width,w->height};memcpy(p,&r,sizeof(r));return TRUE;}
PCPS4_MS_ABI BOOL pcps4_abi_GetWindowRect(HANDLE h,void *p){return pcps4_abi_GetClientRect(h,p);}
PCPS4_MS_ABI HANDLE pcps4_abi_GetDC(HANDLE h){return valid_win(h)?h:NULL;}
PCPS4_MS_ABI int pcps4_abi_ReleaseDC(HANDLE h,HANDLE dc){return valid_win(h)&&h==dc?1:0;}
PCPS4_MS_ABI BOOL pcps4_abi_SetForegroundWindow(HANDLE h){return valid_win(h);}
PCPS4_MS_ABI HANDLE pcps4_abi_GetActiveWindow(void){return NULL;}
PCPS4_MS_ABI HANDLE pcps4_abi_GetFocus(void){return NULL;}
PCPS4_MS_ABI HANDLE pcps4_abi_SetFocus(HANDLE h){return valid_win(h)?h:NULL;}
PCPS4_MS_ABI intptr_t pcps4_abi_SetWindowLongPtrA(HANDLE h,int idx,intptr_t v){if(!valid_win(h)||idx!=-21)return 0;WIN*w=h;intptr_t old=w->user;w->user=v;return old;}
PCPS4_MS_ABI intptr_t pcps4_abi_SetWindowLongPtrW(HANDLE h,int i,intptr_t v){return pcps4_abi_SetWindowLongPtrA(h,i,v);}
PCPS4_MS_ABI intptr_t pcps4_abi_GetWindowLongPtrA(HANDLE h,int i){if(!valid_win(h)||i!=-21)return 0;return ((WIN*)h)->user;}
PCPS4_MS_ABI intptr_t pcps4_abi_GetWindowLongPtrW(HANDLE h,int i){return pcps4_abi_GetWindowLongPtrA(h,i);}
PCPS4_MS_ABI BOOL pcps4_abi_SetWindowTextA(HANDLE h,LPCSTR t){(void)t;return valid_win(h);}
PCPS4_MS_ABI BOOL pcps4_abi_SetWindowTextW(HANDLE h,LPCWSTR t){(void)t;return valid_win(h);}

PCPS4_MS_ABI HRESULT pcps4_abi_CoInitializeEx(LPVOID r,DWORD f){(void)r;(void)f;return S_OK;}
PCPS4_MS_ABI void pcps4_abi_CoUninitialize(void){}
PCPS4_MS_ABI LPVOID pcps4_abi_CoTaskMemAlloc(SIZE_T n){return malloc(n);}
PCPS4_MS_ABI LPVOID pcps4_abi_CoTaskMemRealloc(LPVOID p,SIZE_T n){return realloc(p,n);}
PCPS4_MS_ABI void pcps4_abi_CoTaskMemFree(LPVOID p){free(p);}
PCPS4_MS_ABI UINT pcps4_abi_timeGetTime(void){return (UINT)pcps4_monotonic_ms();}
PCPS4_MS_ABI UINT pcps4_abi_timeBeginPeriod(UINT p){(void)p;return 0;}
PCPS4_MS_ABI UINT pcps4_abi_timeEndPeriod(UINT p){(void)p;return 0;}
