# PcPs4: Windows D3D11 on the PS4 via DXVK — status

Branch: `dxvk/host`. Chain: Windows game (PE) → PcPs4 loader/Win32 layer → unmodified DXVK 1.10.3 DLLs →
Vulkan shim (`compat/pcps4_vulkan_shim.cpp`) → `vulkan-ps4` driver (GNM) → PS4 GPU.

## What works on a real PS4 (firmware 13.02, GoldHEN)

- Runs a real Windows x64 PE (`game.exe`) and the real DXVK `d3d11.dll`/`dxgi.dll`; no crashes.
- DXVK creates a Vulkan device on the PS4 GPU at feature level D3D_FEATURE_LEVEL_11_0, creates a swapchain,
  and submits/flips ~240 frames at 60 Hz.
- GPU buffer fills, render-pass clears and `vkCmdClearColorImage` return the correct pixels (in-driver self-test).

## Bugs found and fixed on the way

| Bug | Effect | Fix |
|---|---|---|
| DLL loader freed the file buffer that the image's section table points into | silent null-deref at "game load" (v8–v10) | keep the buffer for the module's lifetime |
| CRT stdio shims passed the fake `__iob_func` table to libc | crash in `fflush`/`fwrite` | map fake stdio slots to real streams |
| Windows `CreateThread` called start routines with the System V ABI | every DXVK worker thread jumped to null | MS-ABI trampoline |
| Vulkan shim did not export `vkGetDeviceProcAddr` | null call after device creation | exported through the shim |
| DXVK feature-level 11_0 hard requirements (shaderDrawParameters, hostQueryReset, transformFeedback) not reported | "Requested feature level not supported" | shim reports them and strips what the driver lacks |
| Clear pixel shader read its colour from a constant buffer the compiler folded to a null descriptor | every draw-based clear wrote zeros (black screen) | colour passed as inline push constants |
| Driver never programmed VS→PS interpolant linkage (`SPI_PS_INPUT_CNTL`) | interpolated values read as zero | `sceGnmDrawCmdSetPsInputUsage` at bind time |
| Clear shader code was in `malloc` memory | GPU cannot fetch it | allocated in GPU-visible memory |
| Draw-based clear skipped viewport / rect-list / RT mask state | clear covered nothing | state set as in the working GNM pass |
| Crash handler dumped almost nothing | could not locate crashes | single-write dump of registers + stack |

## Tooling that made this tractable

- `host/`: Mac x86-64 harness that runs the whole path (real PE loader, DXVK DLLs) with the console calls
  stubbed. With MoltenVK it reproduced the thread-ABI crash locally. See `host/` and the notes in the commit log.
- In-driver hardware self-test (`selftest_run` in the shim): reads GPU results back through the CPU and logs
  to `/data/pcps4_<ver>_windows_game_log.txt`.
- Driver diagnostics: `/data/vk_ps4_breadcrumb.log` (commands, images, scanout probes).

## Root cause of the black screen / zero UBO + texture reads (found v43)

The shader compiler (`opengnm-psbc`) had **no descriptor layout**: every uniform-buffer, texture and sampler
access was compiled against a null descriptor, so all of them read zeros (the compiled ISA literally zeroes the
V#/T# registers before use). Fixed in `gpulab/patches/opengnm-psbc-descriptors.patch`:

- `PsbcCompileOptions` takes the pipeline's descriptor layout (per-binding offset/size) and a 32-bit address high word.
- One `PTR_INDIRECTRESOURCETABLE` input slot is emitted per descriptor set (apislot = set index).
- GFX7/GFX8 register limits were wrong (12 SGPRs!), which made any non-trivial shader fail to compile; corrected.

Driver side (`vk_ps4_desc_tables.c`): descriptors are mirrored into GPU-visible memory at the layout the shader
expects and the set address (low 32 bits) is written to the shader's user-data register at bind time. Descriptor
pools now track and release their sets (they previously leaked).

An independent Codex session (see PR history) produced v34-v42 from a clean, unpatched vendor driver: those builds
lack the GPU-visible allocator in `vulkan-ps4-hardware-v22.patch` and hard-crash for that reason; they are superseded.

## On-screen output CONFIRMED on hardware (v68, 2026-10-22)

With the NDEBUG psbc fix in place, v68 ran on a real PS4 and **displayed on the TV**: the synthetic
`win_dxvk_clear.c` test cycled colour and each colour reached scanout. Breadcrumb evidence from the live
run: `QueuePresent: scanout px[mid]=aa00aa00` (green) alternating with `=aa0000aa` (blue) across frames
~2017/2558/3103/3644/3952 (AARRGGBB), `present_blit: drew textured blit into swapchain 1920x1080` every
frame, past 478 frames / 4,200+ GPU submits with no crash. Also confirmed green on screen. This is the
full chain working end to end: Windows D3D11 PE -> real DXVK -> Vulkan shim -> vulkan-ps4 (GNM) -> PS4 GPU
-> display. The driver-side present blit and the NDEBUG shader-compile fix are both validated on hardware.

Caveat: launching still needed a relaunch to get past the separate intermittent startup crash (below),
and only the synthetic clear test has run — no real game yet.

## Intermittent startup crash — FIXED (v71, 2026-10-22)

Root-caused from the crash dump: `r14` held the driver's breadcrumb format string
`"BeginRenderPass: rt0 %ux%u swap=%d area=%ux%u"` and the fault was inside `%u` number formatting
(deterministic registers: `rbp=0x4`, `rax=0xa`, `r12=0xcccccccccccccccd` = the divide-by-10 magic).
The driver's `vk_ps4_log` wrote to one shared `FILE*` + counter with **no locking**, and DXVK logs
from several threads at once; two concurrent `vfprintf`s corrupted the stream's internal state and
crashed inside the formatter. Deterministic in its registers, intermittent in when it fired (only on a
collision), and it surfaced either as the SIGSEGV or as a hang mid-write. Fixed by guarding every
`vk_ps4_log*` operation with a mutex (vulkan-ps4 `src/vk_ps4_log.c`). v70 had also serialized the PcPs4
DLL loader (reentrant lock, `compat/pcps4_dll.c`) — kept as hygiene, but it was not the cause.

Verified on hardware: v71 launched first try with no crash, ran the full test to completion, and the
bundled Windows PE called `ExitProcess(0)` and returned cleanly (no on-close crash either):
`finished: 4/4 passed`, `DXVK scanout RGB probe = PASS` (flips=240, nonblack=15, color_changes=7).

## Open problems

1. **Only the synthetic clear test has run — no real game yet.** The next milestone is driving a real
   D3D11/D3D9 title (or a heavier sample) through the same path.
2. A filled (DMA) render-target clear on DXVK's 640x360 backbuffer hard-hangs the console at the first flip
   while the same fill is fine on a 64x64 test image — unexplained; the draw-based clear is used instead.
3. C++ exception unwinding (Windows x64 SEH dispatcher) is not implemented; DXVK throws on some error paths.
4. Only the synthetic `win_dxvk_clear.c` test has run; no real game yet.

## Black-screen root cause — fully isolated on hardware (v49–v57)

The synthetic `win_dxvk_clear.c` test (clear backbuffer to a cycling RED/GREEN/BLUE, present 240 frames)
reaches scanout **black**. A systematic on-device bisection found exactly why — and exactly where it is *not*:

1. **The shader-compile crash is gone.** v48 crashed at the first real `psbc_compile_shader` (`rip=0`); that was
   the hands-free runner's PRX thread-local-storage, not psbc. The standalone package (v49+) compiles DXVK's
   shaders fine — v49 ran to completion, flipping 478 frames.
2. **The clear works.** The 640x360 D3D backbuffer reads `px[0]=ff0000ff` (red) via the RT probe. `ClearRenderTargetView`
   lands correctly.
3. **VideoOut rejects a 640x360 scanout buffer** (`VideoOutOpen` error `0x80290005`), so the swapchain must be a full
   display mode (1920x1080) and the backbuffer is scaled into it at present — a scale is unavoidable.
4. **DXVK presents via its own blitter draw**, not `vkCmdBlitImage` (the driver records zero Blit/Copy-image calls).
   The blitter pass targets the 1920x1080 swapchain (`swap=1`) and issues `BindPipeline + PushConstants + Draw` with
   `verts=3`, viewport `1920x1080` — all correct.
5. **The blitter's source descriptor is valid.** Its combined-image-sampler T# base is `0x218a30000` — exactly the
   red backbuffer's RT base — with the same format words as the working self-test texture.
6. **Cache coherency is not the cause.** Adding a synchronous `ACQUIRE_MEM` (CB flush + texture-cache invalidate) at
   both the pipeline barrier and `EndRenderPass` left scanout fully black.
7. **The scanout path works.** A CPU memset of the VideoOut buffer to blue right before the flip **showed blue on the
   TV** — the swapchain buffer is the scanout target and writes to it display.
8. **A GPU draw into the swapchain reaches scanout.** Forcing the driver's own draw-based clear (embedded fullscreen VS
   + clear PS) to GREEN on the swapchain image **showed green on the TV**. So the swapchain RT descriptor is correct and
   GPU draws display.
9. **psbc handles the pattern DXVK uses.** The in-driver self-test's texture pipeline uses `spv_vs_vert` (a `gl_VertexIndex`
   fullscreen triangle, `vkCmdDraw(3,…)`, no vertex buffer) + `spv_fs_tex_frag` (combined-image-sampler) — the *same*
   shape as DXVK's blit — and samples correctly (`D texture px=ff4080ff`).

**Conclusion:** everything up to and including a driver GPU draw-into-swapchain + texture sampling works. The black screen
is specifically **DXVK's psbc-compiled present-blit shaders producing no visible output** (something DXVK-specific:
gamma / specialization / resource-binding model in its blitter), while the driver's own equivalent draw works.

### The fix (next step): a driver-side present blit
Bypass DXVK's blitter: when a render pass renders into a swapchain image, have the driver do the fullscreen textured blit
itself, reusing the self-test's proven shaders (`spv_vs_vert` + `spv_fs_tex_frag`) with the backbuffer's T#/S# (available
from the bound descriptor set at that pass) as source and the swapchain as target. All the pieces are verified to work
(embedded fullscreen VS + texture PS, `gl_VertexIndex` geometry, texture sampling, GPU-draw-to-swapchain, scanout).

### Separate issue: intermittent startup crash
Some launches crash before/around DLL load (`rip=0`, poison registers) regardless of the graphics path; relaunching
clears it. Enabling the GoldHEN klog/TTY server (port 3232) would let the kernel fault be captured live.

## Patches

`gpulab/patches/vulkan-ps4-hardware-v22.patch` is the cumulative diff against the vendor `vulkan-ps4` checkout
(f4d940b) used for the builds (plus the earlier `vulkan-ps4-hardware.patch`). It now also carries the synchronous
`ACQUIRE_MEM` cache-coherency fixes and the on-device diagnostics (RT-as-texture conversion, per-pass RT dims, draw
coverage, descriptor-set dumps) used for the bisection above.

## v46 scanout acceptance gate

The bundled Windows executable's exit status is now reported separately from the DXVK scanout probe. Before
starting the PE, PcPs4 removes the previous `/data/vk_ps4_breadcrumb.log` so stale scanout samples cannot
make a new run pass. After the PE returns, the wrapper parses the driver's `QueuePresent: flip done` and
`QueuePresent: scanout ... px[mid]=...` records. The scanout result passes only when there are at least two
flips and two samples, at least two samples contain non-black RGB (alpha is ignored), and the RGB sample
changes at least once. This deliberately verifies that changing colour reached the scanout buffer; it does
not replace visual inspection or a real-game test.

The repository CI can build the core package and run host tests for this parser. The DXVK-on-PS4 package
still requires the external patched `vulkan-ps4` / `libpsbc.orbis.a` / `libopengnm.a` libraries described by
`tools/build_dxvk_local.sh`; the ordinary OpenOrbis workflow does not build those dependencies.

## Hands-free test workflow (ps4debug, port 744)

For headless iteration (no controller/TV): GoldHEN's BinLoader (9020/9090) only
listens while the Payloader menu is open, but ps4debug (port 744) is persistent.
`tools/hostdev/ps4debug_launch.py` connects to it, resolves
`sceSystemServiceLaunchApp` in SceShellUI (`libSceSystemService.sprx` base +
export offset `0x4600`, NID `l4FB3wNa-Ac`), and RPC-calls it to launch the
installed title. The app then runs and renders normally even with the TV off
(VideoOut scans out to memory regardless); results are read from the driver's
scanout pixel probe in the logs, not the screen. Key detail: the call needs a
non-NULL (zeroed) param struct as the 3rd arg — with NULL it returns 0x80940005;
with a param struct it returns a positive app id and launches.

## Hands-free teardown: close gracefully, never hard-kill

The hands-free loop is `tools/hostdev/`: `ps4debug_launch.py` (launch), then
`ps4debug_close.py` (close), with `ps4debug_install.py` to install a pushed pkg.

Closing matters as much as launching. `ps4debug_launch.py` saves the live appId
to `/tmp/pcps4_appid`; `ps4debug_close.py` closes the app through LncService
(`sceLncUtilKillApp` offset `0x58d0`, `sceSystemServiceKillApp` `0x4690`) while it
is still alive — the same teardown the shell runs when you back out of a game,
which reclaims the big-app's reserved direct/flexible memory.

A debugger hard-kill (`DebuggingContext.kill_process`) skips that teardown and
**leaks the app's memory**: the next launch then fails with `0x8002000c` (ENOMEM),
and because there is no user-space RPC to reclaim a dead process's direct memory,
that only clears on a power cycle. `sceLncUtilKillApp`/`sceSystemServiceKillApp` on
the already-dead appId return `0x80940004` (app not found) — there is no stale app
record to clear, confirming the wedge is leaked memory, not LncService bookkeeping.
So: always close with `ps4debug_close.py` while the app runs; never hard-kill.

`ps4debug_install.py` resolves libSceAppInstUtil in SceShellUI (`Initialize`
`0x2cd0`, `GetTitleIdFromPkg` `0x3870`, `AppInstallPkg` `0x3110`). `Initialize`
succeeds; the open TODO is the install daemon's jail not seeing `/data`
(`AppInstallPkg` returns `0x80020002` ENOENT for `/data/*.pkg`) — needs the
daemon-visible staging path the shell's own installer uses.

## Intermittent worker-exit crash — root-caused and fixed (v68)

The crash was a live `assert()` inside Mesa's shader compiler. `libpsbc.orbis.a`
was built `-O2 -g` with **no `-DNDEBUG`**, so every assert across spirv_to_nir /
nir / the amd compiler was active. When a DXVK-generated shader tripped a
debug-only sanity assert, `assert` called `abort()`, and `abort()` in this PS4
sandbox reaches a process-exit syscall that is blocked and turns into an
uncatchable `SIGSYS` — taking the whole app down. It was intermittent because
only some shader variants trip an assert; clean runs (`4/4 passed`, scanout
`PASS`) are the runs where none fired. The exit()/abort() override in
`compat/pcps4_fault.c` never helped because psbc resolves libc `abort`/`exit`
through the ELF dynamic linker to libkernel, not the PE-side override.

**Fix:** build psbc in release mode (`-DNDEBUG`), the same configuration Mesa /
RADV ship to production, which handles these shaders without the debug asserts.
`vtn_fail()` still fails a genuinely unsupported shader gracefully (it longjmps
to the `spirv_to_nir` setjmp and returns NULL), so a bad compile no longer
aborts either. See `gpulab/patches/opengnm-psbc-ndebug.patch`:

- `config.orbis.mak`: add `-DNDEBUG`; restore `-Wno-error=` for the C warnings
  clang 16+ promotes to errors (they fire only in GFX11/GFX12 code paths that
  never run on the PS4's older GPU), matching the clang the lib built under before.
- `psbc_ndebug_stubs.c` (new): no-op out-of-line definitions of the NIR
  validation/debug symbols (`nir_validate_shader`, the `nir_metadata_*`
  validation helpers, `nir_validate_progress_*`, and the globals `nir_debug` /
  `nir_debug_print_shader`). Under NDEBUG these become static-inline no-ops, so
  NDEBUG units stop referencing them, but the prebuilt non-NDEBUG driver and
  `psbc_compile.c` still do. The stubs reproduce the NDEBUG no-op behaviour so
  the two link together; `struct blob` uses Mesa's real layout for an ABI-correct
  by-value return.

The rebuilt `libpsbc.orbis.a` drops from 176 MB to 164 MB and from 396 to 2
undefined `__assert` references (the 2 remaining are in crc32 / psbc_stubs glue,
off the shader-compile path). Packaged as v68.
