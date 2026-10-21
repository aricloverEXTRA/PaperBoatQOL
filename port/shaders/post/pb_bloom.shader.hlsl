@prism(type='hlsl', name='PaperBoat Bloom', version='1.0.0', description='Soft glow around bright areas of the game image', author='PaperBoat')

// Fullscreen post pass that adds a soft bloom/glow around the bright parts of the
// game image (tile 0). It bright-passes (isolating pixels above a luminance
// threshold) and gathers them with a compact golden-angle spiral of taps, so a
// single pass gives a wide, smooth glow without a down/upsample chain. Colour-only:
// needs no depth, so it is safe on every backend and with MSAA on or off.
//
// Parameters:
//   uCustom[4] = (enabled, intensity, threshold, radiusPixels)
//   uCustom[5] = (debugView, 0, 0, 0)

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
    float4 uCustom[48];
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

static const float kGoldenAngle = 2.39996323;
static const int kTaps = 16;

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

// Bright-pass with a soft knee so the glow fades in around the threshold instead
// of popping on at a hard cutoff.
float3 brightPass(float3 c, float threshold) {
    float lum = dot(c, float3(0.299, 0.587, 0.114));
    float knee = threshold * 0.5;
    float soft = clamp(lum - threshold + knee, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contrib = max(soft, lum - threshold) / max(lum, 1e-5);
    return c * contrib;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float3 center = g_texture0.Sample(g_sampler0, input.uv0).rgb;

    float4 p = uCustom[4];
    if (p.x < 0.5) {
        return float4(center, 1.0);
    }

    uint w, h;
    g_texture0.GetDimensions(w, h);
    if (w == 0 || h == 0) {
        return float4(center, 1.0);
    }
    float2 texel = 1.0 / float2((float)w, (float)h);

    float intensity = p.y;
    float threshold = p.z;
    float radius = p.w;

    float3 glow = brightPass(center, threshold);
    float wsum = 1.0;
    [unroll]
    for (int i = 0; i < kTaps; i++) {
        float fi = (float)i + 0.5;
        float r = sqrt(fi / (float)kTaps);
        float theta = fi * kGoldenAngle;
        float2 off = float2(cos(theta), sin(theta)) * r * radius * texel;
        float3 c = g_texture0.Sample(g_sampler0, input.uv0 + off).rgb;
        float wt = 1.0 - 0.6 * r; // fade toward the ring edge for a softer falloff
        glow += brightPass(c, threshold) * wt;
        wsum += wt;
    }
    glow /= wsum;

    if (uCustom[5].x > 0.5) {
        return float4(glow * intensity, 1.0);
    }

    // Screen-additive so bright areas bloom without darkening the rest.
    float3 result = center + glow * intensity;
    return float4(result, 1.0);
}
