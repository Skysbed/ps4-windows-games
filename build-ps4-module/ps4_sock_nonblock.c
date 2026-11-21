/* SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Make a socket really non-blocking on the PS4. The kernel refuses FIONBIO to an application (EACCES, measured),
 * so fcntl(F_SETFL, O_NONBLOCK) sets the flag F_GETFL reports while recv and connect go on blocking. libSceNet's
 * own socket option does it (approach documented by OrbisDesk, platform/ps4/pw_wine_net.c, LGPL-2.1-or-later).
 * wineserver runs inside the process and assumes its sockets never block: a blocking connect would stop it, and
 * every thread waiting on it, until the connection timed out. */
#include <fcntl.h>
#include <stdio.h>

extern int sceNetInit( void );
extern int sceNetSetsockopt( int, int, int, const void *, unsigned int );

enum { PS4_NET_SOL_SOCKET = 0xffff, PS4_NET_SO_NBIO = 0x1200 };

int ps4_socket_nonblock( int fd )
{
    static int initialized, reported;
    int on = 1, ret;

    if (!__atomic_exchange_n( &initialized, 1, __ATOMIC_ACQ_REL )) sceNetInit();  /* already initialized is fine */
    fcntl( fd, F_SETFL, O_NONBLOCK );  /* keeps F_GETFL truthful */
    ret = sceNetSetsockopt( fd, PS4_NET_SOL_SOCKET, PS4_NET_SO_NBIO, &on, sizeof(on) );
    if (ret < 0 && !__atomic_exchange_n( &reported, 1, __ATOMIC_ACQ_REL ))
        fprintf( stderr, "pcps4: SO_NBIO refused on fd %d: 0x%08x\n", fd, (unsigned int)ret );
    return ret < 0 ? -1 : 0;
}
