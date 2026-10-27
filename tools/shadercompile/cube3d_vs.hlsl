// 3D cube: vertex buffer (POSITION+COLOR) transformed by an MVP matrix cbuffer.
// Tests depth + perspective + per-frame matrix upload — core real-3D rendering.
cbuffer M : register(b0) { float4x4 mvp; };
struct VSIn  { float3 pos : POSITION; float4 col : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };
VSOut main(VSIn i) {
    VSOut o;
    o.pos = mul(mvp, float4(i.pos, 1.0));
    o.col = i.col;
    return o;
}
