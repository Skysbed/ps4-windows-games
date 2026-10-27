// Real-game render pattern: vertex buffer (POSITION+COLOR) AND a cbuffer
// transform in the same VS. Exercises fetch-shader + descriptor coexistence.
cbuffer T : register(b0) { float2 offset; float2 pad; };
struct VSIn  { float3 pos : POSITION; float4 col : COLOR; };
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };
VSOut main(VSIn i) {
    VSOut o;
    o.pos = float4(i.pos.xy + offset, i.pos.z, 1.0);
    o.col = i.col;
    return o;
}
