/* PS4 libvulkan: expose the two Vulkan loader entry points Wine's win32u looks
 * up by name, forwarding to RADV.  win32u dlopens libvulkan.so, dlsyms
 * vkGetInstanceProcAddr and vkGetDeviceProcAddr (both are required), then
 * pulls every other function through them. */
typedef void (*PFN_vkVoidFunction)(void);
static void ps4_ensure_environment(void);
PFN_vkVoidFunction vk_icdGetInstanceProcAddr(void *instance, const char *pName);
PFN_vkVoidFunction vk_common_GetDeviceProcAddr(void *device, const char *pName);

__attribute__((visibility("default")))
PFN_vkVoidFunction vkGetInstanceProcAddr(void *instance, const char *pName) {
    ps4_ensure_environment();
    return vk_icdGetInstanceProcAddr(instance, pName);
}
__attribute__((visibility("default")))
PFN_vkVoidFunction vkGetDeviceProcAddr(void *device, const char *pName) {
    ps4_ensure_environment();
    return vk_common_GetDeviceProcAddr(device, pName);
}

/* A console module never runs the C library's process start-up, so this
 * module's private copy of the program-name variables stays NULL.  Mesa reads
 * them (util_get_process_name, for per-application driver options) and would
 * strlen(NULL).  Give them a value before anything asks. */
extern char *__progname, *__progname_full;

/* This module carries its own C library, whose calloc() skips clearing memory
 * that came straight from mmap(), assuming the OS returns zero-filled pages.
 * On the console mmap() is an emulation over the system's memory services and
 * can hand back memory that is not zeroed, so calloc() returned dirty blocks
 * (regexec then walked a table full of leftovers).  musl's __malloc_replaced
 * switch makes calloc() always clear. */
extern int __malloc_replaced;

static char ps4_program_name[] = "wine";

/* First among constructors: every later calloc() must clear. */
__attribute__((constructor(101))) static void ps4_module_environment_early(void)
{
    __malloc_replaced = 1;
}

/* Last among constructors: the C library's own start-up runs before this and
 * leaves the program name unset, so assign it afterwards (unconditionally --
 * an "if unset" test here would be defeated by a stale non-NULL value). */
__attribute__((constructor(65535))) static void ps4_module_environment_late(void)
{
    __malloc_replaced = 1;
    __progname = ps4_program_name;
    __progname_full = ps4_program_name;
}

/* And once more at the point of use: both entry points pass through here
 * before Mesa can ask for the name. */
static void ps4_ensure_environment(void)
{
    __malloc_replaced = 1;
    if (!__progname)      __progname = ps4_program_name;
    if (!__progname_full) __progname_full = ps4_program_name;
}

/* regexec() from this toolchain's C library faults on the console even on a
 * trivial, correctly compiled pattern (established by a self-test run inside
 * this module: malloc and calloc are sound and regcomp builds a valid
 * automaton, but regexec then reads through a bad pointer).  The cause is not
 * yet known.  Mesa only uses it to match the program name against per-game
 * driver workarounds, none of which apply here, and its own no-regex fallback
 * answers "no match".  Linked with --wrap=regexec so Mesa gets that answer.
 * regcomp/regfree are left real: on a regcomp *failure* Mesa would apply the
 * entry, which is the opposite of what we want. */
int __wrap_regexec( const void *preg, const char *string, unsigned long nmatch, void *pmatch, int eflags )
{
    (void)preg; (void)string; (void)nmatch; (void)pmatch; (void)eflags;
    return 1;   /* REG_NOMATCH */
}
