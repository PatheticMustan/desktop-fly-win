cbuffer PerShadow : register(b1) {
    float4 ShadowColor;
    float4 ShadowParams; // x: radiusX, y: radiusY, z: opacity, w: unused
};

struct PS_INPUT {
    float4 Position : SV_POSITION;
    float3 WorldPos : POSITION0;
    float3 Normal : NORMAL;
    float2 TexCoord : TEXCOORD0;
    float4 Color : COLOR0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float2 d = (input.TexCoord - 0.5f) * 2.0f;
    float distSq = dot(d, d);
    if (distSq > 1.0f) discard;

    float alpha = (1.0f - smoothstep(0.0f, 1.0f, distSq)) * ShadowParams.z * ShadowColor.a;
    return float4(ShadowColor.rgb * alpha, alpha);
}
