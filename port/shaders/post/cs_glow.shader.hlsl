@prism(type='hlsl', name='PaperBoat Glow Post (D3D11)', version='1.0.0', description='Single-pass bright glow/bloom for emissive surfaces such as lava fire and magic', author='PaperBoat Shader Mod')

@if(o_root_signature)
    #define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | DENY_VERTEX_SHADER_ROOT_ACCESS), CBV(b0, visibility = SHADER_VISIBILITY_PIXEL), DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), DescriptorTable(Sampler(s0), visibility = SHADER_VISIBILITY_PIXEL)"
@end

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv0 : TEXCOORD0;
};

// Mirrors struct TransformUniforms in gfx_rendering_api.h
cbuffer TransformCB : register(b4) {
    float4 mtx_palette[32];
    float4 y_scale;
}

// Mirrors struct PerDrawCB in gfx_direct3d11.cpp (the field layout must match or
// uCustom reads the wrong register).
cbuffer PerDrawCB : register(b1) {
    struct {
        uint width;
        uint height;
        bool linear_filtering;
    } textures[7];
    float4 combiner_inputs[6];
    float4 fog_color;
    float4 grayscale_color;
    float4 uv_transform[2];
    float4 texture_clamp[2];
    float4 fog_params;
    float4 palette_params[2];
    float4 lod_params;
    float4 uCustom[32];
}

Texture2D g_texture0 : register(t0);
SamplerState g_sampler0 : register(s0);

PSInput VSMain(float4 position : POSITION, float mtxSlot : MTXSLOT, float2 uv0 : TEXCOORD0) {
    PSInput result;

    // RSP position transform: matrix palette entry selected per vertex
    int mtxBase = int(mtxSlot + 0.5) * 4;
    float4 clipPos = position.x * mtx_palette[mtxBase] + position.y * mtx_palette[mtxBase + 1] +
                     position.z * mtx_palette[mtxBase + 2] + position.w * mtx_palette[mtxBase + 3];

    // D3D clip space: z in 0..1; y flip is carried in the uniform
    result.position = float4(clipPos.x, clipPos.y * y_scale.x, (clipPos.z + clipPos.w) / 2.0, clipPos.w);
    result.uv0 = float2(uv0.x * uv_transform[0].x + uv_transform[0].y,
                        uv0.y * uv_transform[0].z + uv_transform[0].w);
    return result;
}

@if(o_root_signature)
    [RootSignature(RS)]
@end

// Keeps only the portion of the colour brighter than the threshold, so dim
// scenery stays untouched while bright lava/fire/magic bleeds into a halo.
float3 pb_bright(float3 c, float threshold) {
    float luma = dot(c, float3(0.2126, 0.7152, 0.0722));
    float k = max(luma - threshold, 0.0);
    return c * k;
}

struct PSOutput {
    float4 color : SV_TARGET;
};

PSOutput PSMain(PSInput input, float4 screenSpace : SV_Position) {
    // uCustom[4] = (threshold, intensity, radiusPixels, enabled)
    float threshold = uCustom[4].x;
    float intensity = uCustom[4].y;
    float radius    = uCustom[4].z;
    float enabled   = uCustom[4].w;

    float4 base = g_texture0.Sample(g_sampler0, input.uv0);

    PSOutput output;
    if (enabled < 0.5 || intensity <= 0.0) {
        output.color = float4(base.rgb, 1.0);
        return output;
    }

    // uCustom[1] = (width, height, 1/width, 1/height) - engine built-in.
    float2 texel = uCustom[1].zw * max(radius, 0.0);

    float3 glow = float3(0.0, 0.0, 0.0);
    glow += pb_bright(g_texture0.Sample(g_sampler0, input.uv0 + float2( texel.x,  0.0)).rgb, threshold);
    glow += pb_bright(g_texture0.Sample(g_sampler0, input.uv0 + float2(-texel.x,  0.0)).rgb, threshold);
    glow += pb_bright(g_texture0.Sample(g_sampler0, input.uv0 + float2( 0.0,  texel.y)).rgb, threshold);
    glow += pb_bright(g_texture0.Sample(g_sampler0, input.uv0 + float2( 0.0, -texel.y)).rgb, threshold);
    glow += 0.7071 * pb_bright(g_texture0.Sample(g_sampler0, input.uv0 + float2( texel.x,  texel.y)).rgb, threshold);
    glow += 0.7071 * pb_bright(g_texture0.Sample(g_sampler0, input.uv0 + float2(-texel.x,  texel.y)).rgb, threshold);
    glow += 0.7071 * pb_bright(g_texture0.Sample(g_sampler0, input.uv0 + float2( texel.x, -texel.y)).rgb, threshold);
    glow += 0.7071 * pb_bright(g_texture0.Sample(g_sampler0, input.uv0 + float2(-texel.x, -texel.y)).rgb, threshold);
    glow *= (1.0 / 6.8284);

    float3 color = base.rgb + glow * intensity;
    output.color = float4(clamp(color, 0.0, 1.0), 1.0);
    return output;
}
