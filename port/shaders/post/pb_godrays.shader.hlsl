@prism(type='hlsl', name='PaperBoat God Rays', version='1.0.0', description='Screen-space volumetric light shafts from the sun', author='PaperBoat')

// Fullscreen post pass that adds radial light shafts ("god rays") streaming from
// the sun. The port projects the world sun direction to screen space on the CPU
// each frame and hands the result in through register 31, so this pass needs no
// matrix math of its own and is robust to the N64 pipeline's matrix conventions
// and to widescreen. Rays are gathered by marching through the scene colour
// toward the sun and weighting samples by depth so geometry occludes the shafts.
//
// Parameters:
//   uCustom[30] = (enabled, intensity, density, decay)
//   uCustom[31] = (weight, threshold, sunUvX, sunUvY)
// The port clears uCustom[30].x when the sun is off-screen or behind the camera.

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

// Mirrors struct PerDrawCB in gfx_direct3d_common.h.
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

static const int kSamples = 24;

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
    float3 color = g_texture0.Sample(g_sampler0, input.uv0).rgb;

    float4 p = uCustom[30];
    if (p.x < 0.5) {
        return float4(color, 1.0);
    }

    uint w, h;
    g_pbDepth.GetDimensions(w, h);
    // No valid depth SRV: the shafts cannot be occluded, so bail to passthrough.
    if (w == 0 || h == 0) {
        return float4(color, 1.0);
    }

    float2 sunUv = uCustom[31].zw;
    float intensity = p.y;
    float density = p.z;
    float decay = p.w;
    float weight = uCustom[31].x;
    float threshold = uCustom[31].y;

    float dc = g_pbDepth.Sample(g_pbDepthSampler, input.uv0).r;
    // Sky/background carries no geometry: let rays pass through it (no occlusion).
    float sky = (dc >= 0.9999) ? 1.0 : 0.0;

    // March from this pixel toward the sun. density < 1 shortens the effective ray
    // length for a softer falloff.
    float2 delta = (input.uv0 - sunUv) * (density / (float)kSamples);

    float2 uv = input.uv0;
    float illum = 0.0;
    float decayFactor = 1.0;
    [unroll]
    for (int i = 0; i < kSamples; i++) {
        uv -= delta;

        float3 sampleColor = g_texture0.Sample(g_sampler0, uv).rgb;
        float sampleDepth = g_pbDepth.Sample(g_pbDepthSampler, uv).r;

        // Occlusion: only unoccluded (or sky) samples contribute. A sample much
        // nearer than the centre pixel is geometry in front of the ray.
        float visible = 1.0;
        if (sky < 0.5 && sampleDepth < 0.9999) {
            visible = (sampleDepth >= dc - 0.002) ? 1.0 : 0.0;
        }

        float lum = dot(sampleColor, float3(0.299, 0.587, 0.114));
        illum += saturate(lum - threshold) * visible * weight * decayFactor;

        decayFactor *= decay;
    }

    illum /= (float)kSamples;
    illum *= intensity;
    // Fade shafts in away from the sun's origin so the source is not a hotspot.
    illum *= smoothstep(0.0, 0.6, length(sunUv - input.uv0));

    // Additive shafts tinted toward the warm sun colour.
    float3 shaftColor = normalize(float3(1.0, 0.97, 0.9));
    return float4(color + shaftColor * illum, 1.0);
}
