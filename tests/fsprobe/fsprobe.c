/* What the FS selector looks like from a Wine thread: normally, after a handled CPU fault, and after being paused. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#define RESULT "Z:\\data\\pcps4\\games\\fsprobe\\result.txt"
static HANDLE g_log;
static void say( const char *fmt, ... )
{
    char b[300]; DWORD done; int n; va_list a;
    va_start( a, fmt ); n = _vsnprintf( b, sizeof(b) - 1, fmt, a ); va_end( a );
    WriteFile( g_log, b, n, &done, NULL ); FlushFileBuffers( g_log );
}
static inline unsigned fs_sel( void ) { unsigned short v; __asm__ volatile( "movw %%fs,%0" : "=r" (v) ); return v; }
void * volatile g_resume;
static LONG CALLBACK veh( EXCEPTION_POINTERS *e )
{
    if (!g_resume) return EXCEPTION_CONTINUE_SEARCH;
    e->ContextRecord->Rip = (DWORD64)g_resume; g_resume = NULL;
    return EXCEPTION_CONTINUE_EXECUTION;
}
static __attribute__((noinline)) void fault( void )
{
    __asm__ volatile( "leaq 1f(%%rip),%%rax\n movq %%rax,%0\n xorl %%ecx,%%ecx\n movq (%%rcx),%%rax\n1:\n" : "=m" (g_resume) : : "rax", "rcx", "memory", "cc" );
}
static volatile LONG g_stop; static volatile unsigned g_worker_min = 0xffff, g_worker_max, g_worker_zero;
static DWORD WINAPI worker( void *arg )
{
    while (!g_stop)
    {
        unsigned v = fs_sel();
        if (v < g_worker_min) g_worker_min = v;
        if (v > g_worker_max) g_worker_max = v;
        if (!v) g_worker_zero++;
    }
    return 0;
}
int main( void )
{
    HANDLE t; int i; CONTEXT c;
    g_log = CreateFileA( RESULT, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if (g_log == INVALID_HANDLE_VALUE) return 3;
    say( "INFO FS selector at start: 0x%x\n", fs_sel() );
    Sleep( 1 );
    say( "INFO after a system call: 0x%x\n", fs_sel() );
    AddVectoredExceptionHandler( 1, veh );
    for (i = 0; i < 200; i++) fault();
    say( "INFO after 200 handled faults: 0x%x\n", fs_sel() );
    t = CreateThread( NULL, 0, worker, NULL, 0, NULL );
    Sleep( 30 );
    for (i = 0; i < 2000; i++)
    {
        SuspendThread( t );
        c.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_SEGMENTS;
        GetThreadContext( t, &c );
        ResumeThread( t );
    }
    Sleep( 10 );
    g_stop = 1; WaitForSingleObject( t, 3000 );
    say( "INFO worker across 2000 pauses: min 0x%x max 0x%x, reads of null %u\n", g_worker_min, g_worker_max, g_worker_zero );
    say( "DONE ok pass=1 fail=0\n" );
    ExitProcess( 0 );
}
