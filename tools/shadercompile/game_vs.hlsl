// Player triangle generated from SV_VertexID, positioned/colored by a cbuffer.
// Proves the constant-buffer path (the channel every real game uses for
// transforms) end to end through DXVK -> psbc -> GCN.
cbuffer Params : register(b0)
{
    float2 offset;    // player centre, clip space -1..1
    float2 halfsize;  // triangle half-extent
    float4 color;     // fill colour
};
struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };
VSOut main(uint vid : SV_VertexID)
{
    float2 tri[3] = { float2(0.0, 1.0), float2(-1.0, -1.0), float2(1.0, -1.0) };
    float2 p = offset + tri[vid] * halfsize;
    VSOut o;
    o.pos = float4(p, 0.0, 1.0);
    o.col = color;
    return o;
}
