@prism(type='hlsl', name='PaperBoat SSAO', version='1.0.3', description='Screen-space ambient occlusion from the scene depth buffer', author='PaperBoat')

// Fullscreen post pass that samples the scene depth buffer the engine exposes on
// tile 1 during post-processing (SelectTextureFbDepth) and multiplies an ambient
// occlusion term into the game image (tile 0). Depth-only by design: it needs no
// normals or projection matrices, so it is robust to the N64 pipeline's matrix
// conventions and to widescreen. MSAA must be off (a multisampled depth SRV
// cannot be sampled).
//
// Parameters arrive per frame in uCustom[19] = (enabled, strength, radiusPixels,
// bias) and uCustom[20] = (showAoOnly, 0, 0, 0).

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

// Golden-angle spiral: samples are distributed evenly over the disc and the whole
// spiral is rotated by a per-pixel hash so the low tap count reads as smooth
// occlusion instead of a fixed, visible 8-way grid. The rotation is static (no
// temporal jitter) to avoid shimmering without a temporal filter.
static const float kGoldenAngle = 2.39996323;
static const int kTaps = 12;

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

    float4 p = uCustom[19];
    if (p.x < 0.5) {
        return float4(color, 1.0);
    }

    uint w, h;
    g_pbDepth.GetDimensions(w, h);
    // A depth-consuming pass only has a valid depth SRV when the game framebuffer
    // is single-sampled. If the binding is missing (e.g. MSAA still active, or a
    // device without a depth SRV), GetDimensions returns (0,0); bail out to a
    // plain passthrough instead of dividing by zero and turning the screen black.
    if (w == 0 || h == 0) {
        return float4(color, 1.0);
    }

    float dc = g_pbDepth.Sample(g_pbDepthSampler, input.uv0).r;
    // Cleared background / skybox carries no geometry: leave it unoccluded.
    // Values outside [0,1] indicate a mis-bound or non-depth resource; bail out
    // rather than deriving a bogus occlusion term from them.
    if (dc >= 0.9999 || dc < 0.0 || dc > 1.0) {
        return float4(color, 1.0);
    }

    float2 texel = 1.0 / float2((float)w, (float)h);

    float strength = p.y;
    float radius = p.z;
    float bias = p.w;

    // Depth is non-linear (0 = near, 1 = far), so a fixed world gap maps to a
    // smaller depth delta the further away it is. Grow the occluder search range
    // with the centre depth to keep the response even across the view distance.
    float band = bias;
    float range = 0.02 + 0.25 * dc;

    // Per-pixel rotation of the sample spiral (decorrelates neighbouring pixels).
    float rot = frac(sin(dot(input.uv0, float2(12.9898, 78.233))) * 43758.5453) * 6.28318531;
    float cr = cos(rot);
    float sr = sin(rot);

    float occ = 0.0;
    float wsum = 0.0;
    [unroll]
    for (int i = 0; i < kTaps; i++) {
        float fi = (float)i + 0.5;
        float r = sqrt(fi / (float)kTaps); // even area distribution across the disc
        float theta = fi * kGoldenAngle;
        float2 base = float2(cos(theta), sin(theta)) * r;
        float2 dir = float2(base.x * cr - base.y * sr, base.x * sr + base.y * cr);
        float w = 1.0 - 0.7 * r; // weight the centre more, fade at the rim
        float dn = g_pbDepth.Sample(g_pbDepthSampler, input.uv0 + dir * radius * texel).r;
        float dz = dc - dn; // positive => neighbour is in front of the centre
        // Full weight just past the bias, fading to zero at the range bound so
        // distant surfaces (silhouette background) do not get a halo.
        occ += (dz > band) ? saturate(1.0 - dz / range) * w : 0.0;
        wsum += w;
    }

    float ao = saturate(1.0 - (occ / wsum) * strength);
    // Never let the occlusion term erase the image outright: a mis-bound depth
    // buffer (all taps reading a foreground depth) would otherwise multiply the
    // whole frame to black. Cap the darkest the ambient term can go at half.
    ao = max(ao, 0.5);

    if (uCustom[20].x > 0.5) {
        return float4(ao, ao, ao, 1.0);
    }
    return float4(color * ao, 1.0);
}
