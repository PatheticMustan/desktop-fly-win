cbuffer PerBrainFrame : register(b0) {
    matrix ViewProj;
    matrix ModelWorld;
    float4 FlashParams;
};

struct GS_INPUT {
    float3 Position : POSITION;
    float4 Color : COLOR0;
    float Size : PSIZE;
};

struct PS_INPUT {
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 UV : TEXCOORD0;
};

[maxvertexcount(4)]
void main(point GS_INPUT input[1], inout TriangleStream<PS_INPUT> stream) {
    float3 center = input[0].Position;
    float4 col = input[0].Color;
    float halfSize = input[0].Size * 0.5f;

    // View-aligned billboard vectors
    float4 centerProj = mul(float4(center, 1.0f), ViewProj);

    float2 offsets[4] = {
        float2(-halfSize,  halfSize),
        float2( halfSize,  halfSize),
        float2(-halfSize, -halfSize),
        float2( halfSize, -halfSize)
    };

    float2 uvs[4] = {
        float2(-1.0f,  1.0f),
        float2( 1.0f,  1.0f),
        float2(-1.0f, -1.0f),
        float2( 1.0f, -1.0f)
    };

    PS_INPUT p;
    p.Color = col;

    for (int i = 0; i < 4; ++i) {
        float4 pos = centerProj;
        pos.xy += offsets[i];
        p.Position = pos;
        p.UV = uvs[i];
        stream.Append(p);
    }
    stream.RestartStrip();
}
