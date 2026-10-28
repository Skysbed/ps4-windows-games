// MVP as 4 float4 ROWS (no float4x4 matrix type — psbc's matrix-UBO path hangs
// the GPU; plain float4 cbuffer reads are proven to work). clip_i = dot(row_i, p).
cbuffer M : register(b0) { float4 r0; float4 r1; float4 r2; float4 r3; };
struct VSIn  { float3 pos : POSITION; float4 col : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };
VSOut main(VSIn i) {
    float4 p = float4(i.pos, 1.0);
    VSOut o;
    o.pos = float4(dot(r0,p), dot(r1,p), dot(r2,p), dot(r3,p));
    o.col = i.col;
    return o;
}
