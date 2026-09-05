cbuffer ObjectData : register(b0)
{
    row_major float4x4 world;
    row_major float4x4 viewProjection;
    float4 color;
    float4 options;
};

struct VertexInput { float3 position : POSITION; float3 normal : NORMAL; };
struct PixelInput { float4 position : SV_POSITION; float3 normal : NORMAL; float3 worldPosition : TEXCOORD0; };

cbuffer SkinData : register(b1)
{
    row_major float4x4 bones[11];
};
struct SkinInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    uint4 joints : BLENDINDICES;
    float4 weights : BLENDWEIGHT;
};
PixelInput VSSkin(SkinInput input)
{
    float4 position = 0;
    float3 normal = 0;
    [unroll] for (uint i = 0; i < 4; ++i)
    {
        position += mul(float4(input.position, 1), bones[input.joints[i]]) * input.weights[i];
        normal += mul(float4(input.normal, 0), bones[input.joints[i]]).xyz * input.weights[i];
    }
    PixelInput output;
    output.position = mul(position, viewProjection);
    output.normal = normalize(normal);
    output.worldPosition = position.xyz;
    return output;
}

PixelInput VSMain(VertexInput input)
{
    PixelInput output;
    float4 position = mul(float4(input.position, 1), world);
    output.position = mul(position, viewProjection);
    // 初期プリミティブの法線は軸方向なので、非一様スケール後も正規化で扱える。
    output.normal = normalize(mul(float4(input.normal, 0), world).xyz);
    output.worldPosition = position.xyz;
    return output;
}

float4 PSMain(PixelInput input) : SV_TARGET
{
    if (options.y > 0.5) return color;
    float light = 0.35 + 0.65 * saturate(dot(normalize(input.normal), normalize(float3(-0.4, 1, -0.6))));
    float3 base = color.rgb;
    if (options.x > 0.5 && input.normal.y > 0.9)
    {
        float2 cell = abs(frac(input.worldPosition.xz + 0.5) - 0.5);
        float2 width = max(fwidth(input.worldPosition.xz), 0.002);
        float grid = 1 - min(saturate(cell.x / width.x), saturate(cell.y / width.y));
        base = lerp(base, base * 1.65, grid * 0.5);
        float axes = 1 - smoothstep(0.02, 0.05, min(abs(input.worldPosition.x), abs(input.worldPosition.z)));
        base = lerp(base, float3(0.30, 0.57, 0.64), axes * 0.65);
    }
    float3 shaded = base * light;
    // glTFのbaseColorは線形色。現在のUNORM出力へモデルだけsRGB変換する。
    if (options.z > 0.5) shaded = pow(max(shaded,0),1.0 / 2.2);
    return float4(shaded, 1);
}
