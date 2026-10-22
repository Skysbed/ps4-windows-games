struct VSOut { float4 pos : SV_POSITION; float4 col : COLOR; };
VSOut main(uint vid : SV_VertexID) {
    float2 p[3] = { float2(0.0, 0.8), float2(-0.8, -0.8), float2(0.8, -0.8) };
    VSOut o;
    o.pos = float4(p[vid], 0.0, 1.0);
    o.col = float4(1.0, 0.0, 0.0, 1.0); /* red */
    return o;
}
