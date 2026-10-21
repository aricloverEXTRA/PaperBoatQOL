@prism(type='hlsl', name='PaperBoat Depth Debug', version='1.0.0', description='Debug visualization of scene depth for post-pass plumbing validation', author='PaperBoat')

// Fullscreen post pass that samples the scene depth buffer the engine exposes on
// tile 1 during post-processing (SelectTextureFbDepth) and renders it as a
// grayscale ramp (near = bright). This exists to verify the depth-exposure
// plumbing end to end; it is not a lighting effect. Requires MSAA to be off,
// since a multisampled depth SRV cannot be sampled.

struct PSInput {
    float4 position : SV_POSITION;
    @{update_floats(1)} // aMtxSlot vertex attribute (matrix palette index)
@for(i in 0..2)
    @if(o_textures[i])
        float2 uv@{i} : TEXCOORD@{i};
        @{update_floats(2)}
    @end
@end
};

// Mirrors struct TransformUniforms in gfx_rendering_api.h
cbuffer TransformCB : register(b4) {
    float4 mtx_palette[32]; // 4 float4 rows per slot
    float4 y_scale;
}

// Mirrors struct PerDrawCB in gfx_direct3d_common.h (uv_transform + uCustom keep
// the field offsets correct; only uv_transform is read here).
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
    float4 debug_tint;
}

cbuffer PerFrameCB : register(b0) {
    uint noise_frame;
    float noise_scale;
}

@if(o_textures[0])
    Texture2D g_texture0 : register(t0);
    SamplerState g_sampler0 : register(s0);
@end

// Scene depth, bound by the engine on tile 1 during post passes.
Texture2D<float> g_pbDepth : register(t1);
SamplerState g_pbDepthSampler : register(s1);

PSInput VSMain(float4 position : POSITION, float mtxSlot : MTXSLOT
@for(i in 0..2)
    @if(o_textures[i])
        , float2 uv@{i} : TEXCOORD@{i}
    @end
@end
) {
    PSInput result;

    int mtxBase = int(mtxSlot + 0.5) * 4;
    float4 clipPos = position.x * mtx_palette[mtxBase] + position.y * mtx_palette[mtxBase + 1] +
                     position.z * mtx_palette[mtxBase + 2] + position.w * mtx_palette[mtxBase + 3];

    result.position = float4(clipPos.x, clipPos.y * y_scale.x, (clipPos.z + clipPos.w) / 2.0, clipPos.w);

    @for(i in 0..2)
        @if(o_textures[i])
            result.uv@{i} = float2(uv@{i}.x * uv_transform[@{i}].x + uv_transform[@{i}].y,
                                   uv@{i}.y * uv_transform[@{i}].z + uv_transform[@{i}].w);
        @end
    @end

    return result;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float d = g_pbDepth.Sample(g_pbDepthSampler, input.uv0).r;
    // Near = bright, far = dark. sqrt lifts the mid-range so distant geometry
    // stays visible instead of collapsing toward black.
    float v = pow(saturate(1.0 - d), 0.5);
    return float4(v, v, v, 1.0);
}
