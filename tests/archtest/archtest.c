/* archtest: small Windows program that checks what real games need from the PS4 Wine port.
 *
 *  T1  Suspend all threads, read their contexts, scan their stacks, resume (what Boehm/.NET GCs do),
 *      with threads spinning in PE code, in system calls, blocked, and using the heap.
 *      Checks: Rsp is on the thread's own stack, registers in the context are right, and every
 *      callee-saved register survives being paused.
 *  T2  CPU faults reach the Windows exception handler with the right code.
 *      The last group (#GP) is what the PS4 kernel reports as SIGBUS.
 *
 * Results: Z:\data\pcps4\games\archtest\result.txt, flushed line by line. Last line starts with DONE.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>

#define RESULT "Z:\\data\\pcps4\\games\\archtest\\result.txt"
#define ITERATIONS 2000
#define MAX_SECONDS 30

static HANDLE g_log = INVALID_HANDLE_VALUE;
static CRITICAL_SECTION g_log_cs;
static volatile LONG g_beat;
static const char *volatile g_phase = "start";
static int g_pass, g_fail;

static void say( const char *fmt, ... )
{
    char b[600];
    DWORD done;
    int n;
    va_list a;

    va_start( a, fmt );
    n = _vsnprintf( b, sizeof(b) - 1, fmt, a );
    va_end( a );
    if (n < 0) n = sizeof(b) - 1;
    EnterCriticalSection( &g_log_cs );
    WriteFile( g_log, b, n, &done, NULL );
    FlushFileBuffers( g_log );
    LeaveCriticalSection( &g_log_cs );
}

static void check( const char *name, int ok, const char *detail )
{
    if (ok) g_pass++; else g_fail++;
    say( "%s %s%s%s\n", ok ? "PASS" : "FAIL", name, detail && *detail ? "  " : "", detail ? detail : "" );
}

static DWORD WINAPI watchdog( void *arg )
{
    LONG last = -1;
    int idle = 0;

    for (;;)
    {
        LONG now;
        Sleep( 1000 );
        now = g_beat;
        if (now != last) { last = now; idle = 0; continue; }
        if (++idle < 15) continue;
        say( "FAIL HANG no progress for 15 s in: %s\n", g_phase );
        say( "DONE hang pass=%d fail=%d\n", g_pass, g_fail + 1 );
        TerminateProcess( GetCurrentProcess(), 2 );
    }
    return 0;
}

/* ---- register checkers. Each callee-saved register holds seed ^ k. ------------------------------ */
void spin_check( volatile LONG *stop, uint64_t seed, volatile LONG64 *bad );
void sys_check( volatile LONG *stop, uint64_t seed, volatile LONG64 *bad, void (WINAPI *fn)(DWORD) );
extern char spin_loop_start[], spin_loop_end[];

#define PUSHES "push %rbx\npush %rbp\npush %rsi\npush %rdi\npush %r12\npush %r13\npush %r14\npush %r15\n"
#define POPS   "pop %r15\npop %r14\npop %r13\npop %r12\npop %rdi\npop %rsi\npop %rbp\npop %rbx\nret\n"
#define SET(reg,k) "mov %rax," reg "\nxor $" k "," reg "\n"
#define CMP(reg,k) "mov %rax,%r10\nxor $" k ",%r10\ncmp %r10," reg "\njne 8f\n"

__asm__(
    ".text\n.globl spin_check\n.globl spin_loop_start\n.globl spin_loop_end\n"
    "spin_check:\n"
    PUSHES
    "sub $0x28,%rsp\nmovups %xmm6,(%rsp)\nmovups %xmm7,0x10(%rsp)\n"
    "1:\nmov %rdx,%rax\n"
    SET("%rbx","1") SET("%rbp","2") SET("%rsi","3") SET("%rdi","4")
    SET("%r12","5") SET("%r13","6") SET("%r14","7") SET("%r15","8")
    "mov %rdx,%r9\nxor $9,%r9\nmovq %r9,%xmm6\nmov %rdx,%r9\nxor $10,%r9\nmovq %r9,%xmm7\n"
    "spin_loop_start:\n"
    "2:\ncmpl $0,(%rcx)\njne 9f\nmov %rdx,%rax\n"
    CMP("%rbx","1") CMP("%rbp","2") CMP("%rsi","3") CMP("%rdi","4")
    CMP("%r12","5") CMP("%r13","6") CMP("%r14","7") CMP("%r15","8")
    "movq %xmm6,%r9\nmov %rdx,%r10\nxor $9,%r10\ncmp %r10,%r9\njne 8f\n"
    "movq %xmm7,%r9\nmov %rdx,%r10\nxor $10,%r10\ncmp %r10,%r9\njne 8f\n"
    "jmp 2b\n"
    "spin_loop_end:\n"
    "8:\nlock incq (%r8)\njmp 1b\n"
    "9:\nmovups (%rsp),%xmm6\nmovups 0x10(%rsp),%xmm7\nadd $0x28,%rsp\n"
    POPS

    ".globl sys_check\n"
    "sys_check:\n"
    PUSHES
    "sub $0x48,%rsp\nmov %rcx,0x20(%rsp)\nmov %rdx,0x28(%rsp)\nmov %r8,0x30(%rsp)\nmov %r9,0x38(%rsp)\n"
    "1:\nmov 0x28(%rsp),%rax\n"
    SET("%rbx","1") SET("%rbp","2") SET("%rsi","3") SET("%rdi","4")
    SET("%r12","5") SET("%r13","6") SET("%r14","7") SET("%r15","8")
    "2:\nmov 0x20(%rsp),%rax\ncmpl $0,(%rax)\njne 9f\n"
    "xor %ecx,%ecx\ncall *0x38(%rsp)\n"
    "mov 0x28(%rsp),%rax\n"
    CMP("%rbx","1") CMP("%rbp","2") CMP("%rsi","3") CMP("%rdi","4")
    CMP("%r12","5") CMP("%r13","6") CMP("%r14","7") CMP("%r15","8")
    "jmp 2b\n"
    "8:\nmov 0x30(%rsp),%rax\nlock incq (%rax)\njmp 1b\n"
    "9:\nadd $0x48,%rsp\n"
    POPS
);

/* ---- T1: pause and inspect threads ------------------------------------------------------------- */
enum { K_SPIN, K_SLEEP, K_WAIT, K_HEAP, K_BLOCKED, K_COUNT };
static const char *kind_name[K_COUNT] = { "spin", "sleep0", "wait0", "heap", "blocked" };

typedef struct
{
    int kind;
    HANDLE h;
    uint64_t seed, low, high;
    volatile LONG64 bad;
    volatile LONG ready;
    uint64_t rsp_out, ctx_bad, in_loop;
} worker_t;

static volatile LONG g_stop;
static HANDLE g_event, g_never;

static void WINAPI do_wait( DWORD x ) { (void)x; WaitForSingleObject( g_never, 0 ); }
static void WINAPI do_heap( DWORD x )
{
    void *p = HeapAlloc( GetProcessHeap(), 0, 64 + (x & 64) );
    if (p) HeapFree( GetProcessHeap(), 0, p );
}

static DWORD WINAPI worker( void *arg )
{
    worker_t *w = arg;
    MEMORY_BASIC_INFORMATION m;
    NT_TIB *tib = (NT_TIB *)NtCurrentTeb();

    VirtualQuery( &m, &m, sizeof(m) );
    w->low = (uint64_t)m.AllocationBase;
    w->high = (uint64_t)tib->StackBase;
    w->ready = 1;
    switch (w->kind)
    {
    case K_SPIN:    spin_check( &g_stop, w->seed, &w->bad ); break;
    case K_SLEEP:   sys_check( &g_stop, w->seed, &w->bad, (void (WINAPI *)(DWORD))Sleep ); break;
    case K_WAIT:    sys_check( &g_stop, w->seed, &w->bad, do_wait ); break;
    case K_HEAP:    sys_check( &g_stop, w->seed, &w->bad, do_heap ); break;
    case K_BLOCKED: WaitForSingleObject( g_event, INFINITE ); break;
    }
    return 0;
}

static void test_suspend( void )
{
    worker_t w[K_COUNT];
    LARGE_INTEGER freq, a, b, start;
    double worst = 0, total = 0;
    uint64_t gets = 0, susp_fail = 0, get_fail = 0, res_fail = 0, stack_bytes = 0;
    volatile uint64_t sink = 0;
    int i, it, done = 0;
    char d[200];

    QueryPerformanceFrequency( &freq );
    g_event = CreateEventA( NULL, TRUE, FALSE, NULL );
    g_never = CreateEventA( NULL, TRUE, FALSE, NULL );
    ZeroMemory( w, sizeof(w) );
    g_phase = "T1 starting threads";
    for (i = 0; i < K_COUNT; i++)
    {
        w[i].kind = i;
        w[i].seed = 0x1122334455667700ull + ((uint64_t)i << 32);
        w[i].h = CreateThread( NULL, 0, worker, &w[i], 0, NULL );
        while (!w[i].ready) { Sleep( 1 ); g_beat++; }
    }
    Sleep( 50 );
    QueryPerformanceCounter( &start );

    for (it = 0; it < ITERATIONS; it++)
    {
        CONTEXT c[K_COUNT];

        g_phase = "T1 SuspendThread";
        for (i = 0; i < K_COUNT; i++)
            if (SuspendThread( w[i].h ) == (DWORD)-1) susp_fail++;
        g_beat++;
        for (i = 0; i < K_COUNT; i++)
        {
            g_phase = "T1 GetThreadContext";
            c[i].ContextFlags = CONTEXT_FULL;
            QueryPerformanceCounter( &a );
            if (!GetThreadContext( w[i].h, &c[i] )) { get_fail++; g_beat++; continue; }
            QueryPerformanceCounter( &b );
            {
                double ms = (b.QuadPart - a.QuadPart) * 1000.0 / freq.QuadPart;
                total += ms; if (ms > worst) worst = ms;
            }
            gets++; g_beat++;
            if (c[i].Rsp < w[i].low || c[i].Rsp >= w[i].high) { w[i].rsp_out++; continue; }

            g_phase = "T1 scanning stack";
            {
                const volatile uint64_t *p = (const uint64_t *)(c[i].Rsp & ~7ull);
                const uint64_t *end = (const uint64_t *)w[i].high;
                int n = 0;
                while (p < end && n++ < 4096) { sink += *p++; stack_bytes += 8; }
            }
            if (i == K_SPIN && c[i].Rip >= (DWORD64)spin_loop_start && c[i].Rip < (DWORD64)spin_loop_end)
            {
                uint64_t s = w[i].seed;
                w[i].in_loop++;
                if (c[i].Rbx != (s ^ 1) || c[i].Rbp != (s ^ 2) || c[i].Rsi != (s ^ 3) || c[i].Rdi != (s ^ 4) ||
                    c[i].R12 != (s ^ 5) || c[i].R13 != (s ^ 6) || c[i].R14 != (s ^ 7) || c[i].R15 != (s ^ 8) ||
                    c[i].Xmm6.Low != (s ^ 9) || c[i].Xmm7.Low != (s ^ 10))
                    w[i].ctx_bad++;
            }
        }
        g_phase = "T1 ResumeThread";
        for (i = 0; i < K_COUNT; i++)
            if (ResumeThread( w[i].h ) == (DWORD)-1) res_fail++;
        g_beat++;
        done = it + 1;
        QueryPerformanceCounter( &b );
        if ((b.QuadPart - start.QuadPart) / freq.QuadPart >= MAX_SECONDS) break;
    }
    QueryPerformanceCounter( &b );

    g_phase = "T1 stopping threads";
    InterlockedExchange( &g_stop, 1 );
    SetEvent( g_event );
    for (i = 0; i < K_COUNT; i++)
    {
        DWORD r = WaitForSingleObject( w[i].h, 8000 );
        g_beat++;
        _snprintf( d, sizeof(d), "%s", kind_name[i] );
        if (r != WAIT_OBJECT_0) { say( "FAIL thread %s did not end\n", d ); g_fail++; }
    }

    say( "INFO %d pause rounds in %.1f s, %I64u context reads, avg %.3f ms, worst %.1f ms, %I64u KB of stacks read (%I64u)\n",
         done, (b.QuadPart - start.QuadPart) / (double)freq.QuadPart, gets, gets ? total / gets : 0, worst,
         stack_bytes / 1024, (uint64_t)(sink & 1) );
    _snprintf( d, sizeof(d), "suspend=%I64u get=%I64u resume=%I64u errors", susp_fail, get_fail, res_fail );
    check( "T1 suspend/get/resume calls", !susp_fail && !get_fail && !res_fail, d );
    for (i = 0; i < K_COUNT; i++)
    {
        char name[64];
        _snprintf( name, sizeof(name), "T1 %s: Rsp on its own stack", kind_name[i] );
        _snprintf( d, sizeof(d), "%I64u bad", w[i].rsp_out );
        check( name, !w[i].rsp_out, d );
        if (i != K_BLOCKED)
        {
            _snprintf( name, sizeof(name), "T1 %s: registers survive pause", kind_name[i] );
            _snprintf( d, sizeof(d), "%I64d corrupted", (LONG64)w[i].bad );
            check( name, !w[i].bad, d );
        }
    }
    _snprintf( d, sizeof(d), "%I64u bad of %I64u samples in the loop", w[K_SPIN].ctx_bad, w[K_SPIN].in_loop );
    check( "T1 context values match the thread", !w[K_SPIN].ctx_bad && w[K_SPIN].in_loop, d );
    for (i = 0; i < K_COUNT; i++) CloseHandle( w[i].h );
}

/* ---- T2: CPU faults ---------------------------------------------------------------------------- */
void * volatile g_resume;
void *g_ro;
__attribute__((aligned(16))) char g_buf[64];
static volatile DWORD g_code;

static LONG CALLBACK veh( EXCEPTION_POINTERS *e )
{
    if (!g_resume) return EXCEPTION_CONTINUE_SEARCH;
    g_code = e->ExceptionRecord->ExceptionCode;
    e->ContextRecord->Rip = (DWORD64)g_resume;
    g_resume = NULL;
    return EXCEPTION_CONTINUE_EXECUTION;
}

#define FAULT(name, body) \
    static __attribute__((noinline)) void name( void ) { __asm__ volatile( \
        "leaq 1f(%%rip),%%rax\n movq %%rax,%0\n" body "\n1:\n" \
        : "=m" (g_resume) : : "rax", "rcx", "rdx", "xmm0", "memory", "cc" ); }

FAULT( f_null,     "xorl %%ecx,%%ecx\n movq (%%rcx),%%rax" )
FAULT( f_readonly, "movq g_ro(%%rip),%%rcx\n movq $1,(%%rcx)" )
FAULT( f_ud2,      "ud2" )
FAULT( f_int3,     "int3" )
FAULT( f_div0,     "xorl %%edx,%%edx\n movl $1,%%eax\n xorl %%ecx,%%ecx\n divl %%ecx" )
FAULT( f_noncanon, "movabsq $0x8000000000000000,%%rcx\n movq (%%rcx),%%rax" )
FAULT( f_movaps,   "leaq g_buf+1(%%rip),%%rcx\n movaps (%%rcx),%%xmm0" )
FAULT( f_hlt,      "hlt" )

static void run_fault( const char *name, void (*fn)(void), DWORD want )
{
    char d[100], title[80];

    _snprintf( title, sizeof(title), "T2 fault: %s", name );
    g_phase = title;
    say( "RUN  %s\n", title );
    g_beat++;
    g_code = 0;
    fn();
    g_resume = NULL;
    g_beat++;
    _snprintf( d, sizeof(d), "got %08lx want %08lx", g_code, want );
    check( title, g_code == want, d );
}

int main( void )
{
    SYSTEM_INFO si;
    DWORD old;

    InitializeCriticalSection( &g_log_cs );
    g_log = CreateFileA( RESULT, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if (g_log == INVALID_HANDLE_VALUE) return 3;
    GetSystemInfo( &si );
    say( "INFO archtest 1, %lu processors, page %lu\n", si.dwNumberOfProcessors, si.dwPageSize );
    CloseHandle( CreateThread( NULL, 0, watchdog, NULL, 0, NULL ) );

    test_suspend();

    g_phase = "T2 setup";
    g_beat++;
    AddVectoredExceptionHandler( 1, veh );
    g_ro = VirtualAlloc( NULL, 65536, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    VirtualProtect( g_ro, 65536, PAGE_READONLY, &old );
    run_fault( "null read",        f_null,     EXCEPTION_ACCESS_VIOLATION );
    run_fault( "write read-only",  f_readonly, EXCEPTION_ACCESS_VIOLATION );
    run_fault( "illegal opcode",   f_ud2,      EXCEPTION_ILLEGAL_INSTRUCTION );
    run_fault( "breakpoint",       f_int3,     EXCEPTION_BREAKPOINT );
    run_fault( "divide by zero",   f_div0,     EXCEPTION_INT_DIVIDE_BY_ZERO );
    /* #GP: the PS4 kernel delivers these as its SIGBUS (signal 10) */
    run_fault( "non-canonical address (#GP)", f_noncanon, EXCEPTION_ACCESS_VIOLATION );
    run_fault( "misaligned SSE (#GP)",        f_movaps,   EXCEPTION_ACCESS_VIOLATION );
    run_fault( "privileged hlt (#GP)",        f_hlt,      EXCEPTION_PRIV_INSTRUCTION );

    say( "DONE ok pass=%d fail=%d\n", g_pass, g_fail );
    ExitProcess( g_fail ? 1 : 0 );
}
