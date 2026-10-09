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

## Open problems

1. **Nothing DXVK draws reaches the screen yet.** The remaining stages to prove on hardware: texture sampling
   through descriptor tables (the copy-to-screen pass), constant-buffer reads, interpolants, vertex input.
2. A filled (DMA) render-target clear on DXVK's 640x360 backbuffer hard-hangs the console at the first flip
   while the same fill is fine on a 64x64 test image — unexplained; the draw-based clear is used instead.
3. C++ exception unwinding (Windows x64 SEH dispatcher) is not implemented; DXVK throws on some error paths.
4. Only the synthetic `win_dxvk_clear.c` test has run; no real game yet.

## Patches

`gpulab/patches/vulkan-ps4-hardware-v22.patch` is the cumulative diff against the vendor `vulkan-ps4` checkout
(f4d940b) used for the builds (plus the earlier `vulkan-ps4-hardware.patch`).
