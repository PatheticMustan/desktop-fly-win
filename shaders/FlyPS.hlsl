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

Texture2D DiffuseTexture : register(t0);
SamplerState LinearSampler : register(s0);

struct PS_INPUT {
    float4 Position : SV_POSITION;
    float3 WorldPos : POSITION0;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float4 baseColor = DiffuseColor;
    if (UseTexture > 0.5f) {
        float4 texColor = DiffuseTexture.Sample(LinearSampler, input.TexCoord);
        baseColor *= texColor;
    }

    float3 N = normalize(input.Normal);
    float3 L = normalize(LightDir);
    float3 V = normalize(EyePos - input.WorldPos);
    float3 H = normalize(L + V);

    float NdotL = max(0.0f, dot(N, L));
    float3 diffuse = baseColor.rgb * (AmbientColor.rgb + LightColor.rgb * NdotL);

    float NdotH = max(0.0f, dot(N, H));
    float specFactor = pow(NdotH, max(1.0f, Shininess * 128.0f)) * (NdotL > 0.0f ? 1.0f : 0.0f);
    float3 specular = SpecularColor.rgb * LightColor.rgb * specFactor;

    float3 finalRgb = diffuse + specular;
    float alpha = baseColor.a;

    // Premultiplied alpha for DirectComposition
    return float4(finalRgb * alpha, alpha);
}
