/*
 * PcPs4 GPU Lab (round 2).
 *
 * One app that answers, in a single install, the questions that decide whether
 * Windows games can render through the PS4 GPU:
 *
 *   T0  system probe        console mode, usable memory, CPU speed
 *   T1  solid colours       red triangle on green, shaders take NO inputs
 *   T2  gradient + const    the vendor sample: constant-buffer clear (white)
 *                           and interpolated colour (orange gradient)
 *   T3  runtime compile     SPIR-V compiled to GPU code ON the console, then drawn
 *   T4  Vulkan probe        does the Vulkan driver start, what does it support
 *   T5  PS4 Pro (NEO)       same as T1 with shaders built for the Pro GPU
 *
 * SELF-HEALING: before each test a "RUN n" line is written (and flushed to the
 * filesystem). If the console freezes or the app dies inside a test, the next
 * launch sees RUN without DONE, marks that test crashed and skips it, then
 * carries on with the rest. Delete /data/gpulab_v1_progress.txt to start over.
 *
 * Output: /data/gpulab_v1_log.txt (appended across launches).
 */

#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <orbis/libkernel.h>

#include <gnm/drawcommandbuffer.h>
#include <gnm/gpuaddr/gpuaddr.h>
#include <gnm/platform.h>

#include "u/fs.h"
#include "u/utility.h"

#include "displayctx.h"
#include "memalloc.h"
#include "misc.h"

#include "psbc_compile.h"
#include <vulkan/vulkan.h>
#include "vk_ps4_log.h" /* driver-internal breadcrumbs -> /data/vk_ps4_breadcrumb.log */

#define LAB_VERSION "v3"
#define LOG_PATH "/data/gpulab_" LAB_VERSION "_log.txt"
#define PROGRESS_PATH "/data/gpulab_" LAB_VERSION "_progress.txt"

/* ------------------------------------------------------------------------ */
/* logging + self-healing progress                                           */
/* ------------------------------------------------------------------------ */

static FILE* g_log = NULL;

static void lab_log(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	printf("\n");
	if (g_log) {
		va_start(ap, fmt);
		vfprintf(g_log, fmt, ap);
		va_end(ap);
		fputc('\n', g_log);
		fflush(g_log);
	}
}

enum {
	T_SYSTEM = 0,
	T_SOLID,
	T_GRADIENT,
	T_RUNTIME,
	T_VULKAN,
	T_NEO,
	T_COUNT
};

static const char* const k_test_names[T_COUNT] = {
	"T0 system probe",
	"T1 solid colours (no shader inputs)",
	"T2 constant-buffer clear + interpolated colour",
	"T3 shader compiled on the console",
	"T4 Vulkan driver probe",
	"T5 PS4 Pro (NEO) shaders",
};

/* 0 = not attempted, 1 = started and never finished (crash), 2 = pass, 3 = fail */
static int g_status[T_COUNT];

static void progress_append(const char* line) {
	int fd = open(PROGRESS_PATH, O_WRONLY | O_CREAT | O_APPEND, 0666);
	if (fd < 0) {
		return;
	}
	(void)!write(fd, line, strlen(line));
	fsync(fd);
	close(fd);
}

static void progress_load(void) {
	memset(g_status, 0, sizeof(g_status));

	FILE* f = fopen(PROGRESS_PATH, "r");
	if (!f) {
		return;
	}
	char line[128];
	int complete = 0;
	while (fgets(line, sizeof(line), f)) {
		int n = 0, code = 0;
		if (!strncmp(line, "COMPLETE", 8)) {
			complete = 1;
		} else if (sscanf(line, "RUN %d", &n) == 1 && n >= 0 && n < T_COUNT) {
			g_status[n] = 1;
		} else if (sscanf(line, "DONE %d %d", &n, &code) == 2 && n >= 0 &&
				   n < T_COUNT) {
			g_status[n] = code ? 3 : 2;
		}
	}
	fclose(f);

	if (complete) {
		/* A previous launch finished every test: start a fresh pass. */
		unlink(PROGRESS_PATH);
		memset(g_status, 0, sizeof(g_status));
		lab_log("=== previous pass was complete; starting a fresh pass ===");
	}
}

static void test_begin(int n) {
	char line[32];
	snprintf(line, sizeof(line), "RUN %d\n", n);
	progress_append(line);
	lab_log("--- %s : START", k_test_names[n]);
}

static void test_end(int n, int failed, const char* why) {
	char line[32];
	snprintf(line, sizeof(line), "DONE %d %d\n", n, failed ? 1 : 0);
	progress_append(line);
	g_status[n] = failed ? 3 : 2;
	lab_log("--- %s : %s%s%s", k_test_names[n], failed ? "FAIL" : "PASS",
			why ? " : " : "", why ? why : "");
}

/* ------------------------------------------------------------------------ */
/* GPU plumbing                                                              */
/* ------------------------------------------------------------------------ */

typedef struct {
	MemoryAllocator garlic;
	DisplayContext display;
	GnmRenderTarget rt[2];
	int ready;
	int dead; /* GPU stopped answering: no further GPU tests are safe */
} Gpu;

static Gpu g_gpu;

static int g_gpu_base_ready = 0;

static int gpu_init(void) {
	if (g_gpu.ready) {
		return 0;
	}
	if (!g_gpu_base_ready) {
		g_gpu.garlic = memalloc_init(
			192 * 1024 * 1024,
			ORBIS_KERNEL_PROT_CPU_READ | ORBIS_KERNEL_PROT_CPU_RW |
				ORBIS_KERNEL_PROT_GPU_READ | ORBIS_KERNEL_PROT_GPU_WRITE,
			ORBIS_KERNEL_WC_GARLIC
		);
		if (!initclearutility(&g_gpu.garlic)) {
			lab_log("gpu_init: initclearutility failed");
			return -1;
		}
		g_gpu_base_ready = 1;
	}
	if (!displayctx_init(&g_gpu.display)) {
		lab_log("gpu_init: displayctx_init failed");
		return -2;
	}
	lab_log(
		"display: %ux%u, gpu mode %d", g_gpu.display.screenw,
		g_gpu.display.screenh, (int)gnmGpuMode()
	);
	for (int i = 0; i < 2; i += 1) {
		if (!initcolortarget(
				&g_gpu.rt[i], &g_gpu.garlic, g_gpu.display.screenw,
				g_gpu.display.screenh, GNM_FMT_R8G8B8A8_SRGB, gnmGpuMode()
			)) {
			lab_log("gpu_init: initcolortarget %d failed", i);
			return -3;
		}
	}
	if (!displayctx_setrts(&g_gpu.display, g_gpu.rt, 2)) {
		lab_log("gpu_init: displayctx_setrts failed");
		return -4;
	}
	g_gpu.ready = 1;
	return 0;
}

/* Release the screen so another component (the Vulkan swapchain) can own it. */
static void gpu_release_display(void) {
	if (g_gpu.ready) {
		displayctx_destroy(&g_gpu.display);
		g_gpu.ready = 0;
		lab_log("display released");
	}
}

typedef struct {
	GnmVsShader* vs;
	GnmPsShader* ps;
	GnmPsShader* clearps; /* NULL -> vendor constant-buffer clear (white) */
} Scene;

typedef struct {
	GnmCommandBuffer cmd;
	const uint32_t* addr;
	uint32_t size;
	volatile uint64_t* label;
} Frame;

/* Full-screen pass with a pixel shader that takes no inputs. */
static void clear_with_ps(GnmCommandBuffer* cmd, GnmRenderTarget* rt,
						  GnmPsShader* ps) {
	const GnmDbRenderControl dbrenderctrl = {0};
	const GnmDepthStencilControl depthstencilctrl = {
		.zfunc = GNM_DEPTH_COMPARE_NEVER,
		.stencilfunc = GNM_DEPTH_COMPARE_NEVER,
		.stencilbackfunc = GNM_DEPTH_COMPARE_NEVER,
	};
	gnmDrawCmdSetDbRenderControl(cmd, &dbrenderctrl);
	gnmDrawCmdSetDepthStencilControl(cmd, &depthstencilctrl);
	gnmDrawCmdSetEmbeddedVsShader(cmd, GNM_EMBEDDED_VSH_FULLSCREEN, 0);
	gnmDrawCmdSetPsShader(cmd, &ps->registers);
	setupviewport(cmd, 0, 0, rt->size.width, rt->size.height, 0.5, 0.5);
	gnmDrawCmdSetRenderTarget(cmd, 0, rt);
	gnmDrawCmdSetRenderTargetMask(cmd, 0xf);
	gnmDrawCmdSetPrimitiveType(cmd, GNM_PT_RECTLIST);
	gnmDrawCmdDrawIndexAuto(cmd, 3);
}

static int build_frame(Frame* fr, GnmRenderTarget* rt, const Scene* sc) {
	const size_t cmdmemsize = GNM_INDIRECT_BUFFER_MAX_BYTESIZE;
	void* cmdmem =
		memalloc_alloc(&g_gpu.garlic, cmdmemsize, GNM_ALIGNMENT_BUFFER_BYTES);
	if (!cmdmem) {
		return -1;
	}
	fr->cmd = gnmCmdInit(cmdmem, cmdmemsize, NULL, NULL);
	GnmCommandBuffer* cmd = &fr->cmd;

	if (sc->clearps) {
		clear_with_ps(cmd, rt, sc->clearps);
	} else {
		const float white[4] = {1.0f, 1.0f, 1.0f, 1.0f};
		clearcolortarget(cmd, rt, white);
	}
	gnmDrawCmdWaitGraphicsWrite(
		cmd, GNM_ACQUIRE_TARGET_CB0 | GNM_ACQUIRE_TARGET_DB
	);

	const GnmPrimitiveSetup primsetup = {
		.cullmode = GNM_CULL_NONE,
		.frontface = GNM_FACE_CCW,
		.frontmode = GNM_FILL_SOLID,
		.backmode = GNM_FILL_SOLID,
		.provokemode = GNM_PROVOKINGVTX_FIRST,
	};
	gnmDrawCmdSetPrimitiveSetup(cmd, &primsetup);
	gnmDrawCmdSetRenderTarget(cmd, 0, rt);
	gnmDrawCmdSetRenderTargetMask(cmd, 0xf);
	setupviewport(
		cmd, 0, 0, g_gpu.display.screenw, g_gpu.display.screenh, 0.5f, 0.5f
	);
	gnmDrawCmdSetVsShader(cmd, &sc->vs->registers, 0);
	gnmDrawCmdSetPsShader(cmd, &sc->ps->registers);
	gnmDrawCmdSetPsInputUsage(
		cmd, gnmVsShaderExportSemanticTable(sc->vs), sc->vs->numexportsemantics,
		gnmPsShaderInputSemanticTable(sc->ps), sc->ps->numinputsemantics
	);
	gnmDrawCmdSetPrimitiveType(cmd, GNM_PT_TRILIST);
	gnmDrawCmdDrawIndexAuto(cmd, 3);

	uint64_t* label =
		memalloc_alloc(&g_gpu.garlic, sizeof(uint64_t), sizeof(uint64_t));
	if (!label) {
		return -2;
	}
	fr->label = label;
	gnmDrawCmdEventWriteEop(
		cmd, GNM_CACHE_FLUSH_TS, (uint64_t)label, GNM_DATA_SEL_SEND_DATA64, 1
	);
	fr->addr = cmd->beginptr;
	fr->size = (cmd->cmdptr - cmd->beginptr) * sizeof(uint32_t);
	return 0;
}

/* Show a scene for `frames` frames, double buffered. 0 = ok, <0 = failure. */
static int show_scene(const Scene* sc, int frames) {
	Frame fr[2];
	for (int i = 0; i < 2; i += 1) {
		if (build_frame(&fr[i], &g_gpu.rt[i], sc) != 0) {
			lab_log("show_scene: build_frame %d failed", i);
			return -1;
		}
	}

	for (int n = 0; n < frames; n += 1) {
		const int idx = n & 1;
		const uint32_t* ccb = NULL;
		uint32_t ccbsize = 0;

		*fr[idx].label = 9;
		const uint32_t* dcb = fr[idx].addr;
		uint32_t dcbsize = fr[idx].size;
		sceGnmSubmitCommandBuffers(1, &dcb, &dcbsize, &ccb, &ccbsize);

		const uint64_t t0 = sceKernelGetProcessTime();
		while (*fr[idx].label != 1) {
			if (sceKernelGetProcessTime() - t0 > 3000000ull) {
				lab_log("show_scene: GPU did not finish frame %d in 3 s (HANG)", n);
				g_gpu.dead = 1;
				return -2;
			}
		}
		if (!displayctx_flip(&g_gpu.display, idx)) {
			lab_log("show_scene: flip failed at frame %d", n);
			return -3;
		}
		sceGnmSubmitDone();
	}
	return 0;
}

static int load_scene(
	Scene* sc, const char* vs, const char* ps, const char* clearps
) {
	memset(sc, 0, sizeof(*sc));
	if (!loadvshader(&sc->vs, &g_gpu.garlic, vs)) {
		lab_log("load_scene: vertex shader %s failed", vs);
		return -1;
	}
	if (!loadpshader(&sc->ps, &g_gpu.garlic, ps)) {
		lab_log("load_scene: pixel shader %s failed", ps);
		return -2;
	}
	if (clearps && !loadpshader(&sc->clearps, &g_gpu.garlic, clearps)) {
		lab_log("load_scene: clear shader %s failed", clearps);
		return -3;
	}
	return 0;
}

/* ------------------------------------------------------------------------ */
/* tests                                                                     */
/* ------------------------------------------------------------------------ */

#define FRAMES 180 /* about 3 seconds */

static void test_system(void) {
	test_begin(T_SYSTEM);

	lab_log("neo mode (PS4 Pro): %d", (int)sceKernelIsNeoMode());
	lab_log("cpu mode: %d", (int)sceKernelGetCpumode());
	lab_log("tsc frequency: %llu Hz", (unsigned long long)sceKernelGetTscFrequency());
	lab_log(
		"direct memory size: %lld MB",
		(long long)(sceKernelGetDirectMemorySize() / (1024 * 1024))
	);

	/* How much direct (GPU-visible) memory can this app actually claim? */
	const size_t mb = 1024 * 1024;
	size_t sizes[] = {128, 256, 512, 768, 1024, 1536, 2048, 3072};
	size_t best = 0;
	for (unsigned i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i += 1) {
		off_t phys = 0;
		int rc = sceKernelAllocateDirectMemory(
			0, sceKernelGetDirectMemorySize(), sizes[i] * mb, 0x200000, 0, &phys
		);
		if (rc == 0) {
			best = sizes[i];
			sceKernelReleaseDirectMemory(phys, sizes[i] * mb);
		} else {
			lab_log("direct alloc %zu MB: failed 0x%x", sizes[i], (unsigned)rc);
			break;
		}
	}
	lab_log("largest single direct allocation: %zu MB", best);

	/* Ordinary (CPU) heap. */
	size_t total = 0;
	void* blocks[64];
	int nb = 0;
	while (nb < 64) {
		void* p = malloc(64 * mb);
		if (!p) {
			break;
		}
		memset(p, 0xA5, 4096);
		blocks[nb++] = p;
		total += 64;
	}
	lab_log("malloc heap reachable: %zu MB (stopped at %d blocks)", total, nb);
	for (int i = 0; i < nb; i += 1) {
		free(blocks[i]);
	}

	/* Crude CPU speed number so we can compare against a PC later. */
	const uint64_t t0 = sceKernelGetProcessTime();
	volatile uint32_t x = 2463534242u;
	for (uint32_t i = 0; i < 200000000u; i += 1) {
		uint32_t v = x;
		v ^= v << 13;
		v ^= v >> 17;
		v ^= v << 5;
		x = v;
	}
	const uint64_t dt = sceKernelGetProcessTime() - t0;
	lab_log(
		"cpu: 200M xorshift iterations in %llu ms (%.1f Miter/s)",
		(unsigned long long)(dt / 1000), 200.0 / ((double)dt / 1e6)
	);

	test_end(T_SYSTEM, 0, NULL);
}

static void test_solid(const char* label, int idx, const char* vs,
					   const char* ps, const char* clearps) {
	test_begin(idx);
	lab_log("%s: expect a RED triangle on a GREEN background", label);
	Scene sc;
	if (g_gpu.dead || gpu_init() != 0 || load_scene(&sc, vs, ps, clearps) != 0) {
		test_end(idx, 1, "setup failed");
		return;
	}
	int rc = show_scene(&sc, FRAMES);
	test_end(idx, rc != 0, rc ? "render failed" : "rendered (check the colours on screen)");
}

static void test_gradient(void) {
	test_begin(T_GRADIENT);
	lab_log("T2: three scenes on a WHITE background (constant-buffer clear):");
	lab_log("T2a: ORANGE-to-YELLOW gradient triangle (the vendor sample)");
	lab_log("T2b: triangle using only the interpolated RED channel (expect plain RED)");
	lab_log("T2c: triangle using only the interpolated GREEN channel (expect a GREEN gradient, dark at the bottom-left)");
	Scene sc;
	if (g_gpu.dead || gpu_init() != 0) {
		test_end(T_GRADIENT, 1, "setup failed");
		return;
	}
	int bad = 0;
	const char* pix[3] = {
		"/app0/assets/misc/tri.frag.sb", "/app0/assets/misc/interp_r.frag.sb",
		"/app0/assets/misc/interp_g.frag.sb"
	};
	for (int i = 0; i < 3; i += 1) {
		if (load_scene(&sc, "/app0/assets/misc/tri.vert.sb", pix[i], NULL) != 0) {
			bad += 1;
			continue;
		}
		lab_log("T2: showing scene %c", 'a' + i);
		if (show_scene(&sc, 120) != 0) {
			bad += 1;
			break;
		}
	}
	test_end(T_GRADIENT, bad != 0, bad ? "a scene failed" : "rendered (check the colours on screen)");
}

static int read_all(const char* path, void** data, size_t* size) {
	return readfile(path, data, size);
}

static int write_all(const char* path, const void* data, size_t size) {
	FILE* f = fopen(path, "wb");
	if (!f) {
		return -1;
	}
	size_t w = fwrite(data, 1, size, f);
	fclose(f);
	return w == size ? 0 : -2;
}

static int compile_to_file(const char* spvpath, PsbcStage stage,
						   const char* outpath) {
	void* spv = NULL;
	size_t spvsize = 0;
	if (read_all(spvpath, &spv, &spvsize) != 0) {
		lab_log("compile: cannot read %s", spvpath);
		return -1;
	}
	PsbcCompileOptions opts = {
		.target = PSBC_TARGET_PS4_BASE,
		.stage = stage,
		.entrypoint = "main",
		.optimise = true,
	};
	PsbcShaderOutput out = {0};
	const uint64_t t0 = sceKernelGetProcessTime();
	PsbcResult r = psbc_compile_shader((const uint32_t*)spv, spvsize, &opts, &out);
	const uint64_t dt = sceKernelGetProcessTime() - t0;
	free(spv);
	lab_log(
		"compile %s -> %s: %s, %zu bytes, %llu ms", spvpath, outpath,
		psbc_result_string(r), out.size, (unsigned long long)(dt / 1000)
	);
	if (r != PSBC_RESULT_OK) {
		return -2;
	}
	int wr = write_all(outpath, out.data, out.size);
	psbc_free_output(&out);
	return wr;
}

static void test_runtime(void) {
	test_begin(T_RUNTIME);
	lab_log("T3: shaders compiled on the console; expect RED triangle on GREEN");
	if (g_gpu.dead || gpu_init() != 0) {
		test_end(T_RUNTIME, 1, "gpu setup failed");
		return;
	}
	psbc_init();
	int a = compile_to_file(
		"/app0/assets/misc/rt.vert.bin", PSBC_STAGE_VERTEX, "/data/gpulab_rt.vert.sb"
	);
	int b = compile_to_file(
		"/app0/assets/misc/rt.frag.bin", PSBC_STAGE_FRAGMENT, "/data/gpulab_rt.frag.sb"
	);
	int c = compile_to_file(
		"/app0/assets/misc/rtclear.frag.bin", PSBC_STAGE_FRAGMENT,
		"/data/gpulab_rtclear.frag.sb"
	);
	psbc_shutdown();
	if (a || b || c) {
		test_end(T_RUNTIME, 1, "runtime compile failed");
		return;
	}
	Scene sc;
	if (load_scene(
			&sc, "/data/gpulab_rt.vert.sb", "/data/gpulab_rt.frag.sb",
			"/data/gpulab_rtclear.frag.sb"
		) != 0) {
		test_end(T_RUNTIME, 1, "could not load the runtime-compiled shaders");
		return;
	}
	int rc = show_scene(&sc, FRAMES);
	test_end(T_RUNTIME, rc != 0, rc ? "render failed" : "compiled and rendered on the console");
}

/*
 * T4: a real Vulkan triangle through the vulkan-ps4 driver. Each step is logged
 * BEFORE it runs, so if the console dies the log's last line names the step.
 */
#define VKSTEP(msg) lab_log("vk: %s", msg)
#define VKCHECK(call, msg)                                              \
	do {                                                                \
		VkResult vkr_ = (call);                                         \
		if (vkr_ != VK_SUCCESS) {                                       \
			lab_log("vk: %s FAILED (VkResult %d)", msg, (int)vkr_);     \
			test_end(T_VULKAN, 1, msg);                                 \
			return;                                                     \
		}                                                               \
	} while (0)

static uint32_t* load_spv(const char* path, size_t* size) {
	void* d = NULL;
	if (readfile(path, &d, size) != 0) {
		return NULL;
	}
	return (uint32_t*)d;
}

static void test_vulkan(void) {
	test_begin(T_VULKAN);
	lab_log("T4: expect a RED triangle on a GREEN background, drawn through Vulkan");

	/* The driver's swapchain opens the screen itself. */
	gpu_release_display();

	vk_ps4_log_open(NULL); /* the driver logs every internal step, flushed, to /data/vk_ps4_breadcrumb.log */
	VKSTEP("vkCreateInstance");
	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "PcPs4 GPU Lab",
		.apiVersion = VK_API_VERSION_1_0,
	};
	VkInstanceCreateInfo ici = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app,
	};
	VkInstance inst = VK_NULL_HANDLE;
	VKCHECK(vkCreateInstance(&ici, NULL, &inst), "vkCreateInstance");

	VKSTEP("vkEnumeratePhysicalDevices");
	uint32_t n = 1;
	VkPhysicalDevice phys = VK_NULL_HANDLE;
	VkResult er = vkEnumeratePhysicalDevices(inst, &n, &phys);
	if ((er != VK_SUCCESS && er != VK_INCOMPLETE) || n == 0) {
		test_end(T_VULKAN, 1, "no physical device");
		return;
	}

	VkPhysicalDeviceProperties p;
	vkGetPhysicalDeviceProperties(phys, &p);
	lab_log(
		"vk device: %s, api %u.%u.%u, driver 0x%x", p.deviceName,
		VK_VERSION_MAJOR(p.apiVersion), VK_VERSION_MINOR(p.apiVersion),
		VK_VERSION_PATCH(p.apiVersion), p.driverVersion
	);
	lab_log(
		"vk limits: maxImageDim2D=%u maxBoundDescriptorSets=%u maxPushConstants=%u "
		"maxColorAttachments=%u maxVertexInputAttributes=%u maxUniformBuffer=%u "
		"maxVertexInputBindings=%u maxViewports=%u",
		p.limits.maxImageDimension2D, p.limits.maxBoundDescriptorSets,
		p.limits.maxPushConstantsSize, p.limits.maxColorAttachments,
		p.limits.maxVertexInputAttributes, p.limits.maxUniformBufferRange,
		p.limits.maxVertexInputBindings, p.limits.maxViewports
	);
	VkPhysicalDeviceFeatures f;
	vkGetPhysicalDeviceFeatures(phys, &f);
	lab_log(
		"vk features: geometryShader=%d tessellationShader=%d sampleRateShading=%d "
		"depthClamp=%d fillModeNonSolid=%d samplerAnisotropy=%d independentBlend=%d "
		"multiDrawIndirect=%d shaderFloat64=%d textureCompressionBC=%d",
		f.geometryShader, f.tessellationShader, f.sampleRateShading, f.depthClamp,
		f.fillModeNonSolid, f.samplerAnisotropy, f.independentBlend,
		f.multiDrawIndirect, f.shaderFloat64, f.textureCompressionBC
	);
	VkPhysicalDeviceMemoryProperties mp;
	vkGetPhysicalDeviceMemoryProperties(phys, &mp);
	for (uint32_t i = 0; i < mp.memoryHeapCount; i += 1) {
		lab_log(
			"vk memory heap %u: %llu MB flags 0x%x", i,
			(unsigned long long)(mp.memoryHeaps[i].size / (1024 * 1024)),
			mp.memoryHeaps[i].flags
		);
	}
	uint32_t ne = 0;
	vkEnumerateDeviceExtensionProperties(phys, NULL, &ne, NULL);
	lab_log("vk device extensions: %u", ne);
	if (ne > 0 && ne < 256) {
		VkExtensionProperties* ext = calloc(ne, sizeof(*ext));
		if (ext) {
			vkEnumerateDeviceExtensionProperties(phys, NULL, &ne, ext);
			for (uint32_t i = 0; i < ne; i += 1) {
				lab_log("  vk ext %s v%u", ext[i].extensionName, ext[i].specVersion);
			}
			free(ext);
		}
	}

	VKSTEP("vkCreateDevice (with VK_KHR_swapchain)");
	float prio = 1.0f;
	const char* devexts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	VkDeviceQueueCreateInfo qci = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = 0,
		.queueCount = 1,
		.pQueuePriorities = &prio,
	};
	VkDeviceCreateInfo dci = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &qci,
		.enabledExtensionCount = 1,
		.ppEnabledExtensionNames = devexts,
	};
	VkDevice dev = VK_NULL_HANDLE;
	VKCHECK(vkCreateDevice(phys, &dci, NULL, &dev), "vkCreateDevice");
	VkQueue queue = VK_NULL_HANDLE;
	vkGetDeviceQueue(dev, 0, 0, &queue);

	VKSTEP("vkCreateSwapchainKHR");
	const VkFormat fmt = VK_FORMAT_R8G8B8A8_UNORM;
	const uint32_t W = 1920, H = 1080;
	VkSwapchainCreateInfoKHR sci = {
		.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
		.minImageCount = 2,
		.imageFormat = fmt,
		.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
		.imageExtent = {W, H},
		.imageArrayLayers = 1,
		.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
		.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
		.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
		.presentMode = VK_PRESENT_MODE_FIFO_KHR,
		.clipped = VK_TRUE,
	};
	VkSwapchainKHR sc = VK_NULL_HANDLE;
	VKCHECK(vkCreateSwapchainKHR(dev, &sci, NULL, &sc), "vkCreateSwapchainKHR");

	VKSTEP("vkGetSwapchainImagesKHR");
	uint32_t nimg = 0;
	VKCHECK(vkGetSwapchainImagesKHR(dev, sc, &nimg, NULL), "vkGetSwapchainImagesKHR(count)");
	if (nimg == 0 || nimg > 4) {
		test_end(T_VULKAN, 1, "unexpected swapchain image count");
		return;
	}
	VkImage images[4];
	VKCHECK(vkGetSwapchainImagesKHR(dev, sc, &nimg, images), "vkGetSwapchainImagesKHR");
	lab_log("vk swapchain images: %u", nimg);

	VKSTEP("vkCreateImageView");
	VkImageView views[4];
	for (uint32_t i = 0; i < nimg; i += 1) {
		VkImageViewCreateInfo vci = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = fmt,
			.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1},
		};
		VKCHECK(vkCreateImageView(dev, &vci, NULL, &views[i]), "vkCreateImageView");
	}

	VKSTEP("vkCreateRenderPass");
	VkAttachmentDescription att = {
		.format = fmt,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};
	VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
	VkSubpassDescription sub = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &ref,
	};
	VkRenderPassCreateInfo rpci = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &att,
		.subpassCount = 1,
		.pSubpasses = &sub,
	};
	VkRenderPass rp = VK_NULL_HANDLE;
	VKCHECK(vkCreateRenderPass(dev, &rpci, NULL, &rp), "vkCreateRenderPass");

	VKSTEP("vkCreateFramebuffer");
	VkFramebuffer fbs[4];
	for (uint32_t i = 0; i < nimg; i += 1) {
		VkFramebufferCreateInfo fci = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = rp,
			.attachmentCount = 1,
			.pAttachments = &views[i],
			.width = W,
			.height = H,
			.layers = 1,
		};
		VKCHECK(vkCreateFramebuffer(dev, &fci, NULL, &fbs[i]), "vkCreateFramebuffer");
	}

	VKSTEP("vkCreateShaderModule (compiles SPIR-V on the console)");
	size_t vsz = 0, fsz = 0;
	uint32_t* vspv = load_spv("/app0/assets/misc/rt.vert.bin", &vsz);
	uint32_t* fspv = load_spv("/app0/assets/misc/rt.frag.bin", &fsz);
	if (!vspv || !fspv) {
		test_end(T_VULKAN, 1, "SPIR-V files missing from the package");
		return;
	}
	VkShaderModule vmod = VK_NULL_HANDLE, fmod = VK_NULL_HANDLE;
	VkShaderModuleCreateInfo smci = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = vsz,
		.pCode = vspv,
	};
	uint64_t t0 = sceKernelGetProcessTime();
	VKCHECK(vkCreateShaderModule(dev, &smci, NULL, &vmod), "vkCreateShaderModule(vertex)");
	smci.codeSize = fsz;
	smci.pCode = fspv;
	VKCHECK(vkCreateShaderModule(dev, &smci, NULL, &fmod), "vkCreateShaderModule(fragment)");
	lab_log("vk shader modules compiled in %llu ms",
			(unsigned long long)((sceKernelGetProcessTime() - t0) / 1000));

	VKSTEP("vkCreatePipelineLayout");
	VkPipelineLayoutCreateInfo plci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
	};
	VkPipelineLayout layout = VK_NULL_HANDLE;
	VKCHECK(vkCreatePipelineLayout(dev, &plci, NULL, &layout), "vkCreatePipelineLayout");

	VKSTEP("vkCreateGraphicsPipelines");
	VkPipelineShaderStageCreateInfo stages[2] = {
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		 .stage = VK_SHADER_STAGE_VERTEX_BIT, .module = vmod, .pName = "main"},
		{.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		 .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = fmod, .pName = "main"},
	};
	VkPipelineVertexInputStateCreateInfo vin = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	};
	VkPipelineInputAssemblyStateCreateInfo ia = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
	};
	VkViewport vp = {0, 0, (float)W, (float)H, 0.0f, 1.0f};
	VkRect2D scissor = {{0, 0}, {W, H}};
	VkPipelineViewportStateCreateInfo vps = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &vp,
		.scissorCount = 1,
		.pScissors = &scissor,
	};
	VkPipelineRasterizationStateCreateInfo rs = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		.lineWidth = 1.0f,
	};
	VkPipelineMultisampleStateCreateInfo ms = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
	};
	VkPipelineColorBlendAttachmentState cba = {
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
						  VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};
	VkPipelineColorBlendStateCreateInfo cb = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &cba,
	};
	VkGraphicsPipelineCreateInfo gpci = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = stages,
		.pVertexInputState = &vin,
		.pInputAssemblyState = &ia,
		.pViewportState = &vps,
		.pRasterizationState = &rs,
		.pMultisampleState = &ms,
		.pColorBlendState = &cb,
		.layout = layout,
		.renderPass = rp,
		.subpass = 0,
	};
	VkPipeline pipe = VK_NULL_HANDLE;
	VKCHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpci, NULL, &pipe),
			"vkCreateGraphicsPipelines");

	VKSTEP("command buffers");
	VkCommandPoolCreateInfo cpci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.queueFamilyIndex = 0,
	};
	VkCommandPool pool = VK_NULL_HANDLE;
	VKCHECK(vkCreateCommandPool(dev, &cpci, NULL, &pool), "vkCreateCommandPool");
	VkCommandBuffer cmds[4];
	VkCommandBufferAllocateInfo cbai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = nimg,
	};
	VKCHECK(vkAllocateCommandBuffers(dev, &cbai, cmds), "vkAllocateCommandBuffers");
	for (uint32_t i = 0; i < nimg; i += 1) {
		VkCommandBufferBeginInfo bi = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		};
		VKCHECK(vkBeginCommandBuffer(cmds[i], &bi), "vkBeginCommandBuffer");
		VkClearValue clear = {.color = {{0.0f, 1.0f, 0.0f, 1.0f}}};
		VkRenderPassBeginInfo rpbi = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = rp,
			.framebuffer = fbs[i],
			.renderArea = {{0, 0}, {W, H}},
			.clearValueCount = 1,
			.pClearValues = &clear,
		};
		vkCmdBeginRenderPass(cmds[i], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdBindPipeline(cmds[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
		vkCmdDraw(cmds[i], 3, 1, 0, 0);
		vkCmdEndRenderPass(cmds[i]);
		VKCHECK(vkEndCommandBuffer(cmds[i]), "vkEndCommandBuffer");
	}

	VKSTEP("sync objects");
	VkSemaphoreCreateInfo semci = {.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
	VkSemaphore acq = VK_NULL_HANDLE, done = VK_NULL_HANDLE;
	VKCHECK(vkCreateSemaphore(dev, &semci, NULL, &acq), "vkCreateSemaphore");
	VKCHECK(vkCreateSemaphore(dev, &semci, NULL, &done), "vkCreateSemaphore");

	VKSTEP("render loop (180 frames)");
	for (int frame = 0; frame < FRAMES; frame += 1) {
		uint32_t idx = 0;
		const int dbg = frame < 2;
		if (dbg) lab_log("vk: frame %d - vkAcquireNextImageKHR", frame);
		VkResult r = vkAcquireNextImageKHR(dev, sc, UINT64_MAX, acq, VK_NULL_HANDLE, &idx);
		if (dbg) lab_log("vk: frame %d - acquired image %u (result %d)", frame, idx, (int)r);
		if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
			lab_log("vk: vkAcquireNextImageKHR failed %d at frame %d", (int)r, frame);
			test_end(T_VULKAN, 1, "acquire failed");
			return;
		}
		VkPipelineStageFlags wait = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		VkSubmitInfo si = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &acq,
			.pWaitDstStageMask = &wait,
			.commandBufferCount = 1,
			.pCommandBuffers = &cmds[idx],
			.signalSemaphoreCount = 1,
			.pSignalSemaphores = &done,
		};
		if (dbg) lab_log("vk: frame %d - vkQueueSubmit", frame);
		r = vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE);
		if (dbg) lab_log("vk: frame %d - submit returned %d (GPU finished)", frame, (int)r);
		if (r != VK_SUCCESS) {
			lab_log("vk: vkQueueSubmit failed %d at frame %d", (int)r, frame);
			test_end(T_VULKAN, 1, "queue submit failed");
			return;
		}
		VkPresentInfoKHR pi = {
			.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
			.waitSemaphoreCount = 1,
			.pWaitSemaphores = &done,
			.swapchainCount = 1,
			.pSwapchains = &sc,
			.pImageIndices = &idx,
		};
		if (dbg) lab_log("vk: frame %d - vkQueuePresentKHR", frame);
		r = vkQueuePresentKHR(queue, &pi);
		if (dbg) lab_log("vk: frame %d - present returned %d", frame, (int)r);
		if (r != VK_SUCCESS && r != VK_SUBOPTIMAL_KHR) {
			lab_log("vk: vkQueuePresentKHR failed %d at frame %d", (int)r, frame);
			test_end(T_VULKAN, 1, "present failed");
			return;
		}
		if (frame == 0) {
			lab_log("vk: first frame presented");
		}
	}
	vkQueueWaitIdle(queue);
	lab_log("vk: %d frames presented", FRAMES);

	VKSTEP("teardown");
	vkDeviceWaitIdle(dev);
	vkDestroySwapchainKHR(dev, sc, NULL);
	test_end(T_VULKAN, 0, "Vulkan triangle rendered (check the colours on screen)");
}

/* ------------------------------------------------------------------------ */

int main(void) {
	g_log = fopen(LOG_PATH, "a");
	lab_log("=================================================");
	lab_log("PcPs4 GPU Lab %s starting", LAB_VERSION);

	progress_load();
	for (int i = 0; i < T_COUNT; i += 1) {
		if (g_status[i] == 1) {
			lab_log("%s : SKIPPED - it started on a previous launch and never finished (crash/freeze)",
					k_test_names[i]);
			g_status[i] = 3;
		} else if (g_status[i] != 0) {
			lab_log("%s : already done on a previous launch (%s)", k_test_names[i],
					g_status[i] == 2 ? "pass" : "fail");
		}
	}

	if (g_status[T_SYSTEM] == 0) test_system();
	if (g_status[T_SOLID] == 0)
		test_solid("T1", T_SOLID, "/app0/assets/misc/solid.vert.sb",
				   "/app0/assets/misc/solid.frag.sb", "/app0/assets/misc/clearsolid.frag.sb");
	if (g_status[T_GRADIENT] == 0) test_gradient();
	if (g_status[T_RUNTIME] == 0) test_runtime();
	if (g_status[T_VULKAN] == 0) test_vulkan();
	if (g_status[T_NEO] == 0)
		test_solid("T5 (NEO shaders)", T_NEO, "/app0/assets/misc/solid.vert.neo.sb",
				   "/app0/assets/misc/solid.frag.neo.sb", "/app0/assets/misc/clearsolid.frag.neo.sb");

	int pass = 0;
	for (int i = 0; i < T_COUNT; i += 1) {
		pass += g_status[i] == 2;
	}
	lab_log("=================================================");
	lab_log("GPU Lab finished: %d of %d tests passed", pass, T_COUNT);
	for (int i = 0; i < T_COUNT; i += 1) {
		lab_log("  %-50s %s", k_test_names[i],
				g_status[i] == 2 ? "PASS" : (g_status[i] == 3 ? "FAIL/SKIPPED" : "not run"));
	}
	progress_append("COMPLETE\n");

	/* Leave a steady screen: solid green if everything passed, red otherwise. */
	if (!g_gpu.dead && g_gpu.ready) {
		Scene sc;
		if (load_scene(&sc, "/app0/assets/misc/solid.vert.sb",
					   pass == T_COUNT ? "/app0/assets/misc/clearsolid.frag.sb"
									   : "/app0/assets/misc/solid.frag.sb",
					   pass == T_COUNT ? "/app0/assets/misc/clearsolid.frag.sb"
									   : "/app0/assets/misc/solid.frag.sb") == 0) {
			for (;;) {
				if (show_scene(&sc, 60) != 0) {
					break;
				}
			}
		}
	}
	for (;;) {
		sceKernelSleep(1);
	}
	return 0;
}
