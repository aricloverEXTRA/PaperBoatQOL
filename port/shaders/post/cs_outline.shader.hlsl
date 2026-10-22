@prism(type='hlsl', name='PaperBoat Outline Post (D3D11)', version='1.0.0', description='Ink outline edge-detect post pass', author='PaperBoat Shader Mod')

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

static const float3 PB_LUMA = float3(0.2126, 0.7152, 0.0722);

struct PSOutput {
    float4 color : SV_TARGET;
};

PSOutput PSMain(PSInput input, float4 screenSpace : SV_Position) {
    // Texel size comes from the engine built-in uCustom[1] = (w, h, 1/w, 1/h).
    float2 texel = float2(uCustom[1].z, uCustom[1].w);
    float2 uv = input.uv0;

    float4 base = g_texture0.Sample(g_sampler0, uv);

    float lumC = dot(base.rgb, PB_LUMA);
    float lumL = dot(g_texture0.Sample(g_sampler0, uv - float2(texel.x, 0.0)).rgb, PB_LUMA);
    float lumR = dot(g_texture0.Sample(g_sampler0, uv + float2(texel.x, 0.0)).rgb, PB_LUMA);
    float lumD = dot(g_texture0.Sample(g_sampler0, uv - float2(0.0, texel.y)).rgb, PB_LUMA);
    float lumU = dot(g_texture0.Sample(g_sampler0, uv + float2(0.0, texel.y)).rgb, PB_LUMA);

    float edge = length(float2(lumR - lumL, lumU - lumD));

    float threshold = uCustom[5].x;
    float strength = uCustom[5].y;
    float ink = smoothstep(threshold, threshold + 0.15, edge) * strength;

    float3 inkColor = float3(0.02, 0.02, 0.03);
    float3 color = lerp(base.rgb, inkColor, saturate(ink));

    PSOutput output;
    output.color = float4(color, 1.0);
    return output;
}
