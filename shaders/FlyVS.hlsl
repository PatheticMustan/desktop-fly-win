cbuffer PerFrame : register(b0) {
    matrix ViewProj;
    float3 LightDir;
    float pad0;
    float4 LightColor;
    float4 AmbientColor;
    float3 EyePos;
    float pad1;
};

cbuffer PerObject : register(b1) {
    matrix World;
    float4 DiffuseColor;
    float4 SpecularColor;
    float Shininess;
    float UseTexture;
    float2 pad2;
};

struct VS_INPUT {
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
};

struct PS_INPUT {
    float4 Position : SV_POSITION;
    float3 WorldPos : POSITION0;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    float4 worldPos = mul(float4(input.Position, 1.0f), World);
    output.WorldPos = worldPos.xyz;
    output.Position = mul(worldPos, ViewProj);
    output.Normal = normalize(mul(input.Normal, (float3x3)World));
    output.TexCoord = input.TexCoord;
    output.Color = input.Color;
    return output;
}
