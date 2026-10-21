@prism(type='hlsl', name='PaperBoat FXAA', version='1.0.0', description='Fast approximate anti-aliasing for the game image', author='PaperBoat')

// Fullscreen post pass that antialiases the game image (tile 0) with a compact
// FXAA-style edge filter. The N64 pipeline renders without multisampling at
// native resolution, so edges are stair-stepped once the image is upscaled;
// FXAA smooths them cheaply. Colour-only: needs no depth and no state beyond the
// image itself, so it is safe on every backend and with MSAA on or off.
//
// Parameters:
//   uCustom[6] = (enabled, subpixel, edgeThreshold, edgeThresholdMin)

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

static const float kLuma[3] = { 0.299, 0.587, 0.114 };

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

float lumaOf(float3 c) {
    return dot(c, float3(kLuma[0], kLuma[1], kLuma[2]));
}

float4 PSMain(PSInput input) : SV_TARGET {
    float4 p = uCustom[6];
    float3 center = g_texture0.Sample(g_sampler0, input.uv0).rgb;
    if (p.x < 0.5) {
        return float4(center, 1.0);
    }

    uint w, h;
    g_texture0.GetDimensions(w, h);
    if (w == 0 || h == 0) {
        return float4(center, 1.0);
    }
    float2 texel = 1.0 / float2((float)w, (float)h);

    float lNW = lumaOf(g_texture0.Sample(g_sampler0, input.uv0 + float2(-1.0, -1.0) * texel).rgb);
    float lNE = lumaOf(g_texture0.Sample(g_sampler0, input.uv0 + float2(1.0, -1.0) * texel).rgb);
    float lSW = lumaOf(g_texture0.Sample(g_sampler0, input.uv0 + float2(-1.0, 1.0) * texel).rgb);
    float lSE = lumaOf(g_texture0.Sample(g_sampler0, input.uv0 + float2(1.0, 1.0) * texel).rgb);
    float lM = lumaOf(center);

    float lMin = min(lM, min(min(lNW, lNE), min(lSW, lSE)));
    float lMax = max(lM, max(max(lNW, lNE), max(lSW, lSE)));

    // Not enough local contrast to be an edge: leave the pixel alone.
    float range = lMax - lMin;
    if (range < max(p.w, lMax * p.z)) {
        return float4(center, 1.0);
    }

    // Estimate the edge orientation from the luma gradient.
    float2 dir = float2(-((lNW + lNE) - (lSW + lSE)), ((lNW + lSW) - (lNE + lSE)));
    float dirReduce = max((lNW + lNE + lSW + lSE) * 0.25 * 0.1, 1.0 / 128.0);
    float rcpDirMin = 1.0 / (min(abs(dir.x), abs(dir.y)) + dirReduce);
    dir = clamp(dir * rcpDirMin, float2(-8.0, -8.0), float2(8.0, 8.0)) * texel;

    // Sample along the edge; the average of the two nearer samples matches the
    // edge, the average of all four is a softer fallback.
    float3 rgbA = 0.5 * (g_texture0.Sample(g_sampler0, input.uv0 + dir * (1.0 / 3.0 - 0.5)).rgb +
                         g_texture0.Sample(g_sampler0, input.uv0 + dir * (2.0 / 3.0 - 0.5)).rgb);
    float3 rgbB = rgbA * 0.5 + 0.25 * (g_texture0.Sample(g_sampler0, input.uv0 + dir * -0.5).rgb +
                                       g_texture0.Sample(g_sampler0, input.uv0 + dir * 0.5).rgb);

    float lB = lumaOf(rgbB);
    float3 result = (lB < lMin || lB > lMax) ? rgbA : rgbB;

    // Blend a little of the original back in to keep edges crisp (subpixel).
    result = lerp(result, center, saturate(p.y) * 0.25);
    return float4(result, 1.0);
}
