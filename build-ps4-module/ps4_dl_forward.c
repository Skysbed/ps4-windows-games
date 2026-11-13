/*
 * PS4: route this module's dlopen/dlsym/dlclose to ntdll's PRX loader by its
 * own, unambiguous entry points.
 *
 * The toolchain's libkernel stub also defines dlopen/dlsym/dlclose, so when a
 * module imports those names create-fself can tag them as libkernel's, and
 * the console then binds them to a loader that cannot load anything.  Linking
 * with --wrap for the three names and forwarding here leaves no name to guess.
 */
#include "pw_wine_dl.h"

void *__wrap_dlopen( const char *path, int mode ) { (void)mode; return pw_wine_dl_open( path ); }
void *__wrap_dlsym( void *handle, const char *name ) { return pw_wine_dl_sym( handle, name ); }
int   __wrap_dlclose( void *handle )                  { return pw_wine_dl_close( handle ); }
