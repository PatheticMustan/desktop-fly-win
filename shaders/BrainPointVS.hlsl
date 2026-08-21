cbuffer PerBrainFrame : register(b0) {
    matrix ViewProj;
    matrix ModelWorld;
    float4 FlashParams; // x: isFlash, yzw: unused
};

struct VS_INPUT {
    float3 Position : POSITION;
    float4 Color : COLOR0;
    float Size : PSIZE;
};

struct GS_INPUT {
    float3 Position : POSITION;
    float4 Color : COLOR0;
    float Size : PSIZE;
};

GS_INPUT main(VS_INPUT input) {
    GS_INPUT output;
    float4 worldPos = mul(float4(input.Position, 1.0f), ModelWorld);
    output.Position = worldPos.xyz;
    output.Color = input.Color;
    output.Size = input.Size;
    return output;
}
