/*
 * PS4 stand-in for the Unix half of Wine's opengl32.
 *
 * The console has no OpenGL, but wined3d.dll links against opengl32.dll and
 * opengl32's DllMain refuses to continue unless its Unix library loads and
 * process_attach succeeds.  This lets the DLL initialise and answers every
 * real GL entry point with STATUS_NOT_SUPPORTED, so callers fail cleanly
 * instead of the whole process aborting at load time.
 */
typedef int NTSTATUS;
typedef NTSTATUS (*unixlib_entry_t)( void *args );
#define STATUS_SUCCESS        ((NTSTATUS)0)
#define STATUS_NOT_SUPPORTED  ((NTSTATUS)0xC00000BB)

static NTSTATUS gl_ok( void *args )          { (void)args; return STATUS_SUCCESS; }
static NTSTATUS gl_unsupported( void *args ) { (void)args; return STATUS_NOT_SUPPORTED; }

/* enum unix_funcs starts: process_attach, thread_attach, process_detach, ...
 * The table is far larger than the real one (about 3100 entries) so any
 * index the PE side uses lands on a valid function. */
#define GL_ENTRIES 8192
#define GL_TABLE { [0 ... GL_ENTRIES - 1] = gl_unsupported, [0] = gl_ok, [1] = gl_ok, [2] = gl_ok }

__attribute__((visibility("default"))) const unixlib_entry_t __wine_unix_call_funcs[GL_ENTRIES]       = GL_TABLE;
__attribute__((visibility("default"))) const unixlib_entry_t __wine_unix_call_wow64_funcs[GL_ENTRIES] = GL_TABLE;
