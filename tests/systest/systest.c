/* systest: file locks, CPU clocks, TCP, UDP, socket options and handle limits, through the Windows API.
 * Results: Z:\data\pcps4\games\systest\result.txt, flushed line by line. Last line starts with DONE. */
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <mswsock.h>
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define DIR_ "Z:\\data\\pcps4\\games\\systest\\"
#ifndef SIO_KEEPALIVE_VALS
#define SIO_KEEPALIVE_VALS _WSAIOW(IOC_VENDOR, 4)
#endif

static HANDLE g_log;
static CRITICAL_SECTION g_cs;
static volatile LONG g_beat, g_watchdog_up;
static const char *volatile g_phase = "start";
static int g_pass, g_fail;

static void say( const char *fmt, ... )
{
    char b[500];
    DWORD done;
    int n;
    va_list a;

    va_start( a, fmt );
    n = _vsnprintf( b, sizeof(b) - 1, fmt, a );
    va_end( a );
    if (n < 0) n = sizeof(b) - 1;
    EnterCriticalSection( &g_cs );
    WriteFile( g_log, b, n, &done, NULL );
    FlushFileBuffers( g_log );
    LeaveCriticalSection( &g_cs );
}

static void check( const char *name, int ok, const char *fmt, ... )
{
    char d[220] = "";
    va_list a;

    if (fmt) { va_start( a, fmt ); _vsnprintf( d, sizeof(d) - 1, fmt, a ); va_end( a ); }
    if (ok) g_pass++; else g_fail++;
    g_beat++;
    say( "%s %s  %s\n", ok ? "PASS" : "FAIL", name, d );
}

static DWORD WINAPI watchdog( void *arg )
{
    LONG last = -1;
    int idle = 0;

    InterlockedExchange( &g_watchdog_up, 1 );
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

/* ---- file locks ---------------------------------------------------------------------------- */
static void test_locks( void )
{
    const char *path = DIR_ "lock.tmp";
    OVERLAPPED o = { 0 };
    HANDLE a, b;
    DWORD done, e;
    BOOL r;

    g_phase = "file locks";
    a = CreateFileA( path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS, 0, NULL );
    b = CreateFileA( path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL );
    if (a == INVALID_HANDLE_VALUE || b == INVALID_HANDLE_VALUE) { check( "lock: open two handles", 0, "error %lu", GetLastError() ); return; }
    WriteFile( a, "0123456789012345", 16, &done, NULL );

    r = LockFile( a, 0, 0, 8, 0 );
    check( "lock: exclusive lock", r, "error %lu", r ? 0 : GetLastError() );
    SetLastError( 0 );
    r = LockFile( b, 0, 0, 8, 0 ); e = GetLastError();
    check( "lock: second handle is refused", !r && e == ERROR_LOCK_VIOLATION, "returned %d, error %lu", r, e );
    if (r) UnlockFile( b, 0, 0, 8, 0 );
    r = LockFile( b, 8, 0, 8, 0 );
    check( "lock: non-overlapping range is allowed", r, "error %lu", r ? 0 : GetLastError() );
    if (r) UnlockFile( b, 8, 0, 8, 0 );
    SetFilePointer( b, 0, NULL, FILE_BEGIN );
    { char c[8]; SetLastError( 0 ); r = ReadFile( b, c, 4, &done, NULL ); e = GetLastError(); }
    say( "INFO lock: read of a locked range %s (regular Wine does not enforce this either)\n", r ? "allowed" : "refused" );
    r = UnlockFile( a, 0, 0, 8, 0 );
    check( "lock: unlock", r, "error %lu", r ? 0 : GetLastError() );
    r = LockFile( b, 0, 0, 8, 0 );
    check( "lock: available after unlock", r, "error %lu", r ? 0 : GetLastError() );
    if (r) UnlockFile( b, 0, 0, 8, 0 );

    r = LockFileEx( a, LOCKFILE_FAIL_IMMEDIATELY, 0, 8, 0, &o );
    check( "lock: shared lock", r, "error %lu", r ? 0 : GetLastError() );
    { OVERLAPPED p = { 0 }; BOOL r2 = LockFileEx( b, LOCKFILE_FAIL_IMMEDIATELY, 0, 8, 0, &p );
      check( "lock: second shared lock is allowed", r2, "error %lu", r2 ? 0 : GetLastError() );
      if (r2) UnlockFileEx( b, 0, 8, 0, &p ); }
    { OVERLAPPED p = { 0 }; BOOL r2; SetLastError( 0 );
      r2 = LockFileEx( b, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 8, 0, &p ); e = GetLastError();
      check( "lock: exclusive refused while shared", !r2, "returned %d, error %lu", r2, e );
      if (r2) UnlockFileEx( b, 0, 8, 0, &p ); }
    if (r) UnlockFileEx( a, 0, 8, 0, &o );
    CloseHandle( a );
    CloseHandle( b );
    DeleteFileA( path );
}

/* ---- CPU clocks ---------------------------------------------------------------------------- */
static uint64_t ft( FILETIME f ) { return ((uint64_t)f.dwHighDateTime << 32) | f.dwLowDateTime; }

static uint64_t thread_ms( void )
{
    FILETIME c, x, k, u;
    GetThreadTimes( GetCurrentThread(), &c, &x, &k, &u );
    return (ft( k ) + ft( u )) / 10000;
}

static uint64_t process_ms( void )
{
    FILETIME c, x, k, u;
    GetProcessTimes( GetCurrentProcess(), &c, &x, &k, &u );
    return (ft( k ) + ft( u )) / 10000;
}

static void test_clocks( void )
{
    LARGE_INTEGER f, s, n;
    uint64_t t0, p0, spin_t, spin_p, sleep_t;
    volatile uint64_t x = 0;

    g_phase = "CPU clocks";
    QueryPerformanceFrequency( &f );
    t0 = thread_ms(); p0 = process_ms();
    QueryPerformanceCounter( &s );
    do { x += 3; QueryPerformanceCounter( &n ); } while ((n.QuadPart - s.QuadPart) * 1000 / f.QuadPart < 400);
    spin_t = thread_ms() - t0; spin_p = process_ms() - p0;
    g_beat++;
    check( "clock: thread CPU time counts a 400 ms spin", spin_t >= 200 && spin_t <= 700, "%llu ms", (unsigned long long)spin_t );
    check( "clock: process CPU time counts a 400 ms spin", spin_p >= 200, "%llu ms", (unsigned long long)spin_p );
    t0 = thread_ms();
    Sleep( 400 );
    sleep_t = thread_ms() - t0;
    check( "clock: thread CPU time ignores a 400 ms sleep", sleep_t <= 100, "%llu ms", (unsigned long long)sleep_t );
}

/* ---- TCP ----------------------------------------------------------------------------------- */
static SOCKET g_tcp_tx;
static DWORD WINAPI late_sender( void *arg )
{
    Sleep( 150 ); send( g_tcp_tx, "AAAA", 4, 0 );
    Sleep( 150 ); send( g_tcp_tx, "BBBB", 4, 0 );
    return 0;
}

static void test_tcp( void )
{
    struct sockaddr_in addr = { 0 };
    int len = sizeof(addr), n, v;
    SOCKET l, c, s;
    char b[64];
    u_long nb;

    g_phase = "TCP connect";
    l = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK ); addr.sin_port = 0;
    if (bind( l, (struct sockaddr *)&addr, sizeof(addr) ) || listen( l, 4 ) || getsockname( l, (struct sockaddr *)&addr, &len ))
    { check( "tcp: listen on loopback", 0, "error %d", WSAGetLastError() ); return; }
    c = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    n = connect( c, (struct sockaddr *)&addr, sizeof(addr) );
    check( "tcp: connect", !n, "error %d", n ? WSAGetLastError() : 0 );
    if (n) return;
    s = accept( l, NULL, NULL );
    check( "tcp: accept", s != INVALID_SOCKET, "error %d", s == INVALID_SOCKET ? WSAGetLastError() : 0 );
    if (s == INVALID_SOCKET) return;
    { DWORD t = 4000; setsockopt( s, SOL_SOCKET, SO_RCVTIMEO, (char *)&t, sizeof(t) ); setsockopt( c, SOL_SOCKET, SO_RCVTIMEO, (char *)&t, sizeof(t) ); }

    g_phase = "TCP data";
    send( c, "hello", 5, 0 );
    n = recv( s, b, sizeof(b), MSG_PEEK );
    check( "tcp: MSG_PEEK sees data", n == 5 && !memcmp( b, "hello", 5 ), "got %d", n );
    n = recv( s, b, sizeof(b), 0 );
    check( "tcp: data is still there after peek", n == 5 && !memcmp( b, "hello", 5 ), "got %d", n );

    g_phase = "TCP MSG_WAITALL";
    g_tcp_tx = c;
    CloseHandle( CreateThread( NULL, 0, late_sender, NULL, 0, NULL ) );
    n = recv( s, b, 8, MSG_WAITALL );
    say( "INFO tcp: MSG_WAITALL returned %d of 8 bytes (regular Wine does not implement it either)\n", n );
    { int have = n > 0 ? n : 0; while (have < 8) { n = recv( s, b + have, 8 - have, 0 ); if (n <= 0) break; have += n; } }
    Sleep( 50 );

    g_phase = "TCP nonblocking";
    nb = 1; ioctlsocket( s, FIONBIO, &nb );
    {
        DWORD t0 = GetTickCount();
        n = recv( s, b, sizeof(b), 0 );
        v = WSAGetLastError();
        check( "tcp: nonblocking recv returns at once", n < 0 && v == WSAEWOULDBLOCK && GetTickCount() - t0 < 200, "got %d, error %d, %lu ms", n, v, GetTickCount() - t0 );
    }
    nb = 0; ioctlsocket( s, FIONBIO, &nb );

    g_phase = "TCP options";
    v = 1; n = setsockopt( c, IPPROTO_TCP, TCP_NODELAY, (char *)&v, sizeof(v) );
    { int g = 0, gl = sizeof(g); int m = getsockopt( c, IPPROTO_TCP, TCP_NODELAY, (char *)&g, &gl );
      check( "tcp: TCP_NODELAY set and read back", !n && !m && g == 1, "set %d get %d value %d", n, m, g ); }
    v = 1; n = setsockopt( c, SOL_SOCKET, SO_KEEPALIVE, (char *)&v, sizeof(v) );
    { int g = 0, gl = sizeof(g); int m = getsockopt( c, SOL_SOCKET, SO_KEEPALIVE, (char *)&g, &gl );
      check( "tcp: SO_KEEPALIVE set and read back", !n && !m && g == 1, "set %d get %d value %d", n, m, g ); }
    { struct { ULONG on, time, interval; } ka = { 1, 30000, 1000 }; DWORD ret = 0;
      n = WSAIoctl( c, SIO_KEEPALIVE_VALS, &ka, sizeof(ka), NULL, 0, &ret, NULL, NULL );
      check( "tcp: keepalive timing", !n, "error %d", n ? WSAGetLastError() : 0 ); }
    g_phase = "TCP small-packet latency";
    {
        LARGE_INTEGER f, t0, t1;
        DWORD to = 1000;
        int rounds = 0, one = 1;
        setsockopt( s, IPPROTO_TCP, TCP_NODELAY, (char *)&one, sizeof(one) );
        setsockopt( s, SOL_SOCKET, SO_RCVTIMEO, (char *)&to, sizeof(to) );
        setsockopt( c, SOL_SOCKET, SO_RCVTIMEO, (char *)&to, sizeof(to) );
        QueryPerformanceFrequency( &f ); QueryPerformanceCounter( &t0 );
        for (; rounds < 200; rounds++)
        {
            g_beat++;
            if (send( c, "p", 1, 0 ) != 1 || recv( s, b, 1, 0 ) != 1) break;
            if (send( s, "q", 1, 0 ) != 1 || recv( c, b, 1, 0 ) != 1) break;
        }
        QueryPerformanceCounter( &t1 );
        { double ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
          check( "tcp: small packets are not held back after keepalive", rounds == 200 && ms / 200 < 5.0, "%d of 200 round trips, avg %.2f ms", rounds, rounds ? ms / rounds : 0 ); }
    }
    v = 128 * 1024; n = setsockopt( s, SOL_SOCKET, SO_RCVBUF, (char *)&v, sizeof(v) );
    { int g = 0, gl = sizeof(g); int m = getsockopt( s, SOL_SOCKET, SO_RCVBUF, (char *)&g, &gl );
      check( "tcp: SO_RCVBUF set and read back", !n && !m && g >= 64 * 1024, "set %d get %d value %d", n, m, g ); }

    g_phase = "TCP shutdown";
    shutdown( c, SD_SEND );
    n = recv( s, b, sizeof(b), 0 );
    check( "tcp: peer sees end of stream", n == 0, "got %d, error %d", n, n < 0 ? WSAGetLastError() : 0 );
    closesocket( s ); closesocket( c ); closesocket( l );
}

/* ---- a connection that never answers must not stop the other threads --------------------- */
static SOCKET g_dead;
static volatile LONG g_dead_done;
static void dead_addr( struct sockaddr_in *a )
{
    memset( a, 0, sizeof(*a) );
    a->sin_family = AF_INET; a->sin_port = htons( 81 ); a->sin_addr.s_addr = inet_addr( "10.255.255.1" );
}
static DWORD WINAPI dead_connect( void *arg )
{
    struct sockaddr_in a;
    dead_addr( &a );
    connect( g_dead, (struct sockaddr *)&a, sizeof(a) );
    InterlockedExchange( &g_dead_done, 1 );
    return 0;
}
static void test_connect( void )
{
    struct sockaddr_in a;
    LARGE_INTEGER f, t0, t1, end;
    double worst = 0, ms;
    u_long nb = 1;
    SOCKET n;
    HANDLE th;
    int r, e;

    g_phase = "non-blocking connect";
    QueryPerformanceFrequency( &f );
    n = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    ioctlsocket( n, FIONBIO, &nb );
    dead_addr( &a );
    QueryPerformanceCounter( &t0 );
    r = connect( n, (struct sockaddr *)&a, sizeof(a) ); e = WSAGetLastError();
    QueryPerformanceCounter( &t1 );
    ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
    check( "connect: non-blocking connect returns at once", ms < 300 && (r == 0 || e == WSAEWOULDBLOCK || e == WSAENETUNREACH || e == WSAEHOSTUNREACH), "%.1f ms, returned %d, error %d", ms, r, e );
    closesocket( n );

    g_phase = "slow connect in another thread";
    g_dead = socket( AF_INET, SOCK_STREAM, IPPROTO_TCP );
    th = CreateThread( NULL, 0, dead_connect, NULL, 0, NULL );
    Sleep( 100 );
    QueryPerformanceCounter( &t0 );
    end.QuadPart = t0.QuadPart + f.QuadPart;
    do
    {
        HANDLE ev;
        QueryPerformanceCounter( &t0 );
        ev = CreateEventA( NULL, FALSE, FALSE, NULL );   /* a round trip through wineserver */
        CloseHandle( ev );
        QueryPerformanceCounter( &t1 );
        ms = (t1.QuadPart - t0.QuadPart) * 1000.0 / f.QuadPart;
        if (ms > worst) worst = ms;
        g_beat++;
        Sleep( 5 );
    } while (t1.QuadPart < end.QuadPart);
    check( "connect: a slow connect does not freeze other threads", worst < 250, "worst wineserver call %.1f ms", worst );
    closesocket( g_dead );
    WaitForSingleObject( th, 3000 );
    g_beat++;
    CloseHandle( th );
}

/* ---- UDP ----------------------------------------------------------------------------------- */
static void test_udp( void )
{
    GUID id = WSAID_WSARECVMSG;
    LPFN_WSARECVMSG recvmsg_fn = NULL;
    struct sockaddr_in addr = { 0 };
    int len = sizeof(addr), n, v, e;
    DWORD ret = 0;
    SOCKET r, t;
    char b[256];

    g_phase = "UDP setup";
    r = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    t = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
    addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl( INADDR_LOOPBACK );
    if (bind( r, (struct sockaddr *)&addr, sizeof(addr) ) || getsockname( r, (struct sockaddr *)&addr, &len ))
    { check( "udp: bind", 0, "error %d", WSAGetLastError() ); return; }
    { DWORD to = 3000; setsockopt( r, SOL_SOCKET, SO_RCVTIMEO, (char *)&to, sizeof(to) ); }

    g_phase = "UDP data";
    memset( b, 'x', 100 );
    sendto( t, b, 100, 0, (struct sockaddr *)&addr, sizeof(addr) );
    n = recv( r, b, sizeof(b), MSG_PEEK );
    check( "udp: MSG_PEEK", n == 100, "got %d, error %d", n, n < 0 ? WSAGetLastError() : 0 );
    n = recv( r, b, sizeof(b), 0 );
    check( "udp: datagram received", n == 100, "got %d, error %d", n, n < 0 ? WSAGetLastError() : 0 );

    g_phase = "UDP truncation";
    sendto( t, b, 100, 0, (struct sockaddr *)&addr, sizeof(addr) );
    WSASetLastError( 0 );
    n = recv( r, b, 10, 0 );
    e = WSAGetLastError();
    check( "udp: short buffer reports WSAEMSGSIZE", n == SOCKET_ERROR && e == WSAEMSGSIZE, "returned %d, error %d", n, e );

    g_phase = "UDP options";
    v = 33; n = setsockopt( t, IPPROTO_IP, IP_TTL, (char *)&v, sizeof(v) );
    { int g = 0, gl = sizeof(g); int m = getsockopt( t, IPPROTO_IP, IP_TTL, (char *)&g, &gl );
      check( "udp: IP_TTL set and read back", !n && !m && g == 33, "set %d (err %d) get %d value %d", n, n ? WSAGetLastError() : 0, m, g ); }
    v = 16; n = setsockopt( t, IPPROTO_IP, IP_TOS, (char *)&v, sizeof(v) );
    { int g = 0, gl = sizeof(g); int m = getsockopt( t, IPPROTO_IP, IP_TOS, (char *)&g, &gl );
      check( "udp: IP_TOS set and read back", !n && !m && g == 16, "set %d (err %d) get %d value %d", n, n ? WSAGetLastError() : 0, m, g ); }
    v = 1; n = setsockopt( t, SOL_SOCKET, SO_BROADCAST, (char *)&v, sizeof(v) );
    { int g = 0, gl = sizeof(g); int m = getsockopt( t, SOL_SOCKET, SO_BROADCAST, (char *)&g, &gl );
      check( "udp: SO_BROADCAST set and read back", !n && !m && g == 1, "set %d get %d value %d", n, m, g ); }

    g_phase = "UDP WSARecvMsg";
    if (WSAIoctl( r, SIO_GET_EXTENSION_FUNCTION_POINTER, &id, sizeof(id), &recvmsg_fn, sizeof(recvmsg_fn), &ret, NULL, NULL ) || !recvmsg_fn)
    { check( "udp: WSARecvMsg available", 0, "error %d", WSAGetLastError() ); }
    else
    {
        char control[256];
        char data[16];
        struct sockaddr_in from;
        WSABUF buf;
        WSAMSG m;
        WSACMSGHDR *cm;
        int found = 0;

        v = 1; n = setsockopt( r, IPPROTO_IP, IP_PKTINFO, (char *)&v, sizeof(v) );
        check( "udp: IP_PKTINFO enabled", !n, "error %d", n ? WSAGetLastError() : 0 );
        memset( b, 'y', 12 );
        sendto( t, b, 12, 0, (struct sockaddr *)&addr, sizeof(addr) );
        memset( &m, 0, sizeof(m) );
        buf.buf = data; buf.len = sizeof(data);
        m.name = (struct sockaddr *)&from; m.namelen = sizeof(from); m.lpBuffers = &buf; m.dwBufferCount = 1;
        m.Control.buf = control; m.Control.len = sizeof(control);
        ret = 0;
        n = recvmsg_fn( r, &m, &ret, NULL, NULL );
        check( "udp: WSARecvMsg receives", !n && ret == 12, "returned %d, %lu bytes, error %d", n, ret, n ? WSAGetLastError() : 0 );
        check( "udp: sender address filled in", !n && from.sin_addr.s_addr == htonl( INADDR_LOOPBACK ), "from %08lx", (unsigned long)ntohl( from.sin_addr.s_addr ) );
        if (!n)
            for (cm = WSA_CMSG_FIRSTHDR( &m ); cm; cm = WSA_CMSG_NXTHDR( &m, cm ))
                if (cm->cmsg_level == IPPROTO_IP && cm->cmsg_type == IP_PKTINFO)
                {
                    IN_PKTINFO *pi = (IN_PKTINFO *)WSA_CMSG_DATA( cm );
                    found = pi->ipi_addr.s_addr == htonl( INADDR_LOOPBACK ) ? 2 : 1;
                }
        check( "udp: IP_PKTINFO control data correct", found == 2, "control %lu bytes, found %d", (unsigned long)m.Control.len, found );

        memset( b, 'z', 100 );
        sendto( t, b, 100, 0, (struct sockaddr *)&addr, sizeof(addr) );
        memset( &m, 0, sizeof(m) );
        buf.buf = data; buf.len = 8;
        m.lpBuffers = &buf; m.dwBufferCount = 1;
        ret = 0;
        WSASetLastError( 0 );
        n = recvmsg_fn( r, &m, &ret, NULL, NULL );
        e = WSAGetLastError();
        check( "udp: WSARecvMsg reports truncation", (n == SOCKET_ERROR && e == WSAEMSGSIZE) || (m.dwFlags & MSG_TRUNC), "returned %d, error %d, flags %lx", n, e, m.dwFlags );
    }
    closesocket( r ); closesocket( t );
}

/* ---- how many files can be open ------------------------------------------------------------ */
static void test_handles( void )
{
    static HANDLE h[900];
    const char *path = DIR_ "handles.tmp";
    int i, got = 0;
    DWORD e = 0;
    HANDLE f;

    g_phase = "open files";
    f = CreateFileA( path, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, CREATE_ALWAYS, 0, NULL );
    if (f != INVALID_HANDLE_VALUE) CloseHandle( f );
    for (i = 0; i < 900; i++)
    {
        h[i] = CreateFileA( path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, 0, NULL );
        g_beat++;
        if (h[i] == INVALID_HANDLE_VALUE) { e = GetLastError(); break; }
        got++;
    }
    say( "INFO files open at once: %d of 900%s", got, got == 900 ? "\n" : "" );
    if (got != 900) say( " (stopped with error %lu)\n", e );
    /* a handle only reaches a Unix descriptor when it is used */
    {
        int used = 0;
        for (i = 0; i < got; i++)
        {
            char c; DWORD n;
            if (!ReadFile( h[i], &c, 1, &n, NULL )) { e = GetLastError(); break; }
            used++; g_beat++;
        }
        say( "INFO files actually read through: %d of %d%s", used, got, used == got ? "\n" : "" );
        if (used != got) say( " (stopped with error %lu)\n", e );
        check( "files: 256 open files all usable", used >= 256, "%d usable", used );
    }
    for (i = 0; i < got; i++) { CloseHandle( h[i] ); g_beat++; }
    DeleteFileA( path );
    f = CreateFileA( DIR_ "after.tmp", GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_FLAG_DELETE_ON_CLOSE, NULL );
    check( "files: a new file still opens afterwards", f != INVALID_HANDLE_VALUE, "error %lu", f == INVALID_HANDLE_VALUE ? GetLastError() : 0 );
    if (f != INVALID_HANDLE_VALUE) CloseHandle( f );
}

int main( void )
{
    WSADATA w;
    int i;

    InitializeCriticalSection( &g_cs );
    g_log = CreateFileA( DIR_ "result.txt", GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if (g_log == INVALID_HANDLE_VALUE) return 3;
    CloseHandle( CreateThread( NULL, 0, watchdog, NULL, 0, NULL ) );
    for (i = 0; i < 2000 && !g_watchdog_up; i++) Sleep( 1 );
    say( "INFO systest 1\n" );
    WSAStartup( MAKEWORD( 2, 2 ), &w );

    test_locks();
    test_clocks();
    test_tcp();
    test_connect();
    test_udp();
    test_handles();

    say( "DONE ok pass=%d fail=%d\n", g_pass, g_fail );
    ExitProcess( g_fail ? 1 : 0 );
}
