cbuffer PerFrame : register(b0) {
    matrix ViewProj;
    float3 LightDir;
    float pad0;
    float4 LightColor;
    float4 AmbientColor;
    float3 EyePos;
    float pad1;
};

struct VS_INPUT {
    // Per-vertex (Slot 0)
    float3 Position : POSITION;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;

    // Per-instance (Slot 1)
    row_major float4x4 InstanceWorld : INSTANCE_WORLD;
    float4 InstanceDiffuse : INSTANCE_DIFFUSE;
    float4 InstanceSpecular : INSTANCE_SPECULAR;
    float InstanceShininess : INSTANCE_SHININESS;
    float InstanceUseTexture : INSTANCE_USE_TEXTURE;
    float2 padInstance : INSTANCE_PAD;
};

struct PS_INPUT {
    float4 Position : SV_POSITION;
    float3 WorldPos : POSITION0;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
    float4 DiffuseColor : COLOR1;
    float4 SpecularColor : COLOR2;
    float Shininess : TEXCOORD1;
    float UseTexture : TEXCOORD2;
};

PS_INPUT main(VS_INPUT input) {
    PS_INPUT output;
    float4 worldPos = mul(float4(input.Position, 1.0f), input.InstanceWorld);
    output.WorldPos = worldPos.xyz;
    output.Position = mul(worldPos, ViewProj);
    output.Normal = normalize(mul(input.Normal, (float3x3)input.InstanceWorld));
    output.TexCoord = input.TexCoord;
    output.Color = input.Color;
    output.DiffuseColor = input.InstanceDiffuse;
    output.SpecularColor = input.InstanceSpecular;
    output.Shininess = input.InstanceShininess;
    output.UseTexture = input.InstanceUseTexture;
    return output;
}
