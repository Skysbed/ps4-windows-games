/* Runs under the laptop's regular Wine and writes d3dbench_shaders.h. */
#include <windows.h>
#include <stdio.h>
typedef struct ID3DBlobVtbl ID3DBlobVtbl;
typedef struct { const ID3DBlobVtbl *lpVtbl; } Blob;
struct ID3DBlobVtbl { void *qi, *addref, *release; void *(WINAPI *GetBufferPointer)(Blob *); SIZE_T (WINAPI *GetBufferSize)(Blob *); };
typedef HRESULT (WINAPI *compile_fn)(const void *, SIZE_T, const char *, const void *, void *, const char *, const char *,
                                     UINT, UINT, Blob **, Blob **);
static const char src[] =
"cbuffer C : register(b0) { float4 off; };\n"
"Texture2D t : register(t0); SamplerState s : register(s0);\n"
"struct VSO { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
"VSO vs_main(float2 p : POSITION) { VSO o; o.pos = float4(p * off.xy + off.zw, 0, 1); o.uv = p; return o; }\n"
"float4 ps_a(VSO i) : SV_TARGET { return t.Sample(s, i.uv) + off * 0.01; }\n"
"float4 ps_b(VSO i) : SV_TARGET { return t.Sample(s, i.uv) * 0.5 + off.yxwz * 0.01; }\n";

static int emit(compile_fn c, FILE *f, const char *entry, const char *target, const char *name)
{
    Blob *code = 0, *err = 0; unsigned i; const unsigned char *p; SIZE_T n;
    HRESULT hr = c(src, sizeof(src) - 1, "bench.hlsl", 0, 0, entry, target, 0, 0, &code, &err);
    if (FAILED(hr) || !code) { printf("compile %s failed %08lx: %s\n", entry, hr, err ? (char *)err->lpVtbl->GetBufferPointer(err) : ""); return 1; }
    p = code->lpVtbl->GetBufferPointer(code); n = code->lpVtbl->GetBufferSize(code);
    fprintf(f, "static const unsigned char %s[%u] = {", name, (unsigned)n);
    for (i = 0; i < n; i++) fprintf(f, "%s0x%02x,", i % 16 ? "" : "\n    ", p[i]);
    fprintf(f, "\n};\n"); printf("%s: %u bytes\n", name, (unsigned)n);
    return 0;
}

int main(int argc, char **argv)
{
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll"); compile_fn c; FILE *f; int r;
    if (!m) { printf("no d3dcompiler\n"); return 2; }
    c = (compile_fn)GetProcAddress(m, "D3DCompile");
    f = fopen(argc > 1 ? argv[1] : "d3dbench_shaders.h", "w"); if (!f) return 3;
    r = emit(c, f, "vs_main", "vs_4_0", "g_vs") | emit(c, f, "ps_a", "ps_4_0", "g_ps_a") | emit(c, f, "ps_b", "ps_4_0", "g_ps_b");
    fclose(f);
    return r;
}
