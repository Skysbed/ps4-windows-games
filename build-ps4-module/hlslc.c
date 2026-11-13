#include <windows.h>
#include <stdio.h>
typedef struct ID3DBlobVtbl ID3DBlobVtbl;
typedef struct { const ID3DBlobVtbl *lpVtbl; } Blob;
struct ID3DBlobVtbl { void *qi, *addref, *release; void *(WINAPI *GetBufferPointer)(Blob*); SIZE_T (WINAPI *GetBufferSize)(Blob*); };
typedef HRESULT (WINAPI *compile_fn)(const void*,SIZE_T,const char*,const void*,void*,const char*,const char*,UINT,UINT,Blob**,Blob**);
static const char src[] =
"struct VSI { float3 pos : POSITION; float4 col : COLOR; };\n"
"struct VSO { float4 pos : SV_POSITION; float4 col : COLOR; };\n"
"VSO vs_main(VSI i) { VSO o; o.pos = float4(i.pos, 1.0); o.col = i.col; return o; }\n"
"float4 ps_main(VSO i) : SV_TARGET { return i.col; }\n";
static int emit(compile_fn c, FILE *f, const char *entry, const char *target, const char *name)
{
    Blob *code = 0, *err = 0; unsigned i; const unsigned char *p; SIZE_T n;
    HRESULT hr = c(src, sizeof(src)-1, "smoke.hlsl", 0, 0, entry, target, 0, 0, &code, &err);
    if (FAILED(hr) || !code) { printf("compile %s failed %08lx: %s\n", entry, hr, err ? (char*)err->lpVtbl->GetBufferPointer(err) : ""); return 1; }
    p = code->lpVtbl->GetBufferPointer(code); n = code->lpVtbl->GetBufferSize(code);
    fprintf(f, "static const unsigned char %s[%u] = {", name, (unsigned)n);
    for (i = 0; i < n; i++) fprintf(f, "%s0x%02x,", i % 16 ? "" : "\n    ", p[i]);
    fprintf(f, "\n};\n"); printf("%s: %u bytes\n", name, (unsigned)n); return 0;
}
int main(int argc, char **argv)
{
    HMODULE m = LoadLibraryA("d3dcompiler_47.dll"); compile_fn c; FILE *f; int r;
    if (!m) { printf("no d3dcompiler\n"); return 2; }
    c = (compile_fn)GetProcAddress(m, "D3DCompile");
    f = fopen(argc > 1 ? argv[1] : "smoke_shaders.h", "w"); if (!f) return 3;
    r = emit(c, f, "vs_main", "vs_4_0", "g_vs_dxbc") | emit(c, f, "ps_main", "ps_4_0", "g_ps_dxbc");
    fclose(f); return r;
}
