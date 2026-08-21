struct PS_INPUT {
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
    float2 UV : TEXCOORD0;
};

float4 main(PS_INPUT input) : SV_TARGET {
    float distSq = dot(input.UV, input.UV);
    if (distSq > 1.0f) discard;

    float alpha = 1.0f - smoothstep(0.2f, 1.0f, distSq);
    float4 col = input.Color;
    return float4(col.rgb * col.a * alpha, col.a * alpha);
}
