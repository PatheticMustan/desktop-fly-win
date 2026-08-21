cbuffer PerBrainFrame : register(b0) {
    matrix ViewProj;
    matrix ModelWorld;
    float4 FlashParams; // x: isFlash, yzw: unused
};

struct VS_INPUT {
    // Slot 0: Billboard Quad vertex
    float2 QuadPos : POSITION;
    float2 UV : TEXCOORD0;

    // Slot 1: Per-point instance
    float3 Center : INSTANCE_POS;
    float4 Color : INSTANCE_COLOR;
    float Size : INSTANCE_SIZE;
};

struct PS_INPUT {
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 UV : TEXCOORD0;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    
    // Transform center to clip space
    float4 worldPos = mul(float4(input.Center, 1.0f), ModelWorld);
    float4 clipPos = mul(worldPos, ViewProj);

    // Expand billboard quad in clip space
    float halfSize = input.Size * 0.5f;
    clipPos.xy += input.QuadPos * halfSize;

    output.Position = clipPos;
    output.Color = input.Color;
    output.UV = input.UV;
    return output;
}
