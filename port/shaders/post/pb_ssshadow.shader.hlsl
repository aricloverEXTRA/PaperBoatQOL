@prism(type='hlsl', name='PaperBoat Screen-Space Shadows', version='1.0.0', description='Long-range screen-space cast shadows from the sun', author='PaperBoat')

// Fullscreen post pass that casts real, longer-range shadows from the sun using
// the scene depth buffer the engine exposes on tile 1. For each surface pixel it
// reconstructs the world position and normal, then marches a ray toward the sun in
// screen space (aimed at the sun's projected position) and treats any geometry the
// ray passes close behind as an occluder. This catches Mario and props casting
// shadows onto the ground while both are on screen; unlike a shadow map it cannot
// cast shadows from off-screen sources, but it needs no second world render pass
// and works on every backend that can sample depth. Shares the sun direction with
// the fragment lighting (Sun Azimuth/Elevation) and the camera view-projection the
// port publishes each frame.
//
// Parameters:
//   uCustom[36] = (enabled, strength, lengthPixels, thickness)
//   uCustom[37] = (debugView, sunUvX, sunUvY, bias)

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

// Scene depth, bound by the engine on tile 1 during post passes.
Texture2D<float> g_pbDepth : register(t1);
SamplerState g_pbDepthSampler : register(s1);

static const int kSteps = 24;

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

// Inverse view-projection, assembled from the four rows the port publishes.
float4x4 invViewProj() {
    return float4x4(uCustom[23], uCustom[24], uCustom[25], uCustom[26]);
}

// Reconstructs the world-space position for a screen uv / hardware depth pair
// (same convention as the depth sun pass: row-vector clip, y flipped).
float3 worldFromDepth(float2 uv, float d) {
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clip = float4(ndc, 2.0 * d - 1.0, 1.0);
    float4 wp = mul(clip, invViewProj());
    return wp.xyz / wp.w;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float3 color = g_texture0.Sample(g_sampler0, input.uv0).rgb;

    float4 p = uCustom[36];
    if (p.x < 0.5) {
        return float4(color, 1.0);
    }

    uint w, h;
    g_pbDepth.GetDimensions(w, h);
    if (w == 0 || h == 0) {
        return float4(color, 1.0);
    }

    float dc = g_pbDepth.Sample(g_pbDepthSampler, input.uv0).r;
    if (dc >= 0.9999 || dc < 0.0 || dc > 1.0) {
        return float4(color, 1.0);
    }

    float2 texel = 1.0 / float2((float)w, (float)h);

    float3 P = worldFromDepth(input.uv0, dc);
    float3 Px = worldFromDepth(input.uv0 + float2(texel.x, 0.0),
                               g_pbDepth.Sample(g_pbDepthSampler, input.uv0 + float2(texel.x, 0.0)).r);
    float3 Py = worldFromDepth(input.uv0 + float2(0.0, texel.y),
                               g_pbDepth.Sample(g_pbDepthSampler, input.uv0 + float2(0.0, texel.y)).r);
    float3 n = normalize(cross(normalize(Px - P), normalize(Py - P)));
    float3 viewDir = normalize(uCustom[27].xyz - P);
    if (dot(n, viewDir) < 0.0) {
        n = -n;
    }

    float3 sunRaw = uCustom[12].xyz;
    float sunLenSq = dot(sunRaw, sunRaw);
    float3 sunDir = sunLenSq > 1e-8 ? sunRaw * rsqrt(sunLenSq) : float3(0.0, 1.0, 0.0);

    float strength = p.y;
    float lengthPixels = p.z;
    float thickness = p.w;
    float bias = uCustom[37].w;

    // Only surfaces facing the light cast a shadow; back-facing surfaces are
    // already unlit.
    if (dot(n, sunDir) <= 0.02) {
        return float4(color, 1.0);
    }

    // World length of the shadow ray at this depth, so a pixel-space length maps
    // to a screen-size-consistent world distance.
    float rayWorldLen = lengthPixels * max(length(P - uCustom[27].xyz), 1.0) / (float)h;

    // Aim the screen-space ray at the sun's projected position when the port
    // supplied one; otherwise fall back to the sun direction's X/Z projection.
    float2 sunUv = uCustom[37].yz;
    float2 dir;
    if (sunUv.x > -9000.0) {
        dir = input.uv0 - sunUv; // march from the pixel toward the sun
    } else {
        dir = float2(sunDir.x, -sunDir.z);
    }
    float dirLen = length(dir);
    dir = dirLen > 1e-5 ? dir / dirLen : float2(0.0, -1.0);

    float shadow = 0.0;
    [unroll]
    for (int i = 1; i <= kSteps; i++) {
        float t = (float)i / (float)kSteps;
        float2 suv = input.uv0 + dir * lengthPixels * t * texel;
        float sd = g_pbDepth.Sample(g_pbDepthSampler, suv).r;
        if (sd >= 0.9999) {
            continue; // marched into the background
        }
        float3 S = worldFromDepth(suv, sd);
        float along = dot(S - P, sunDir);
        if (along <= bias) {
            continue; // not in front of the surface along the light
        }
        if (along > rayWorldLen) {
            continue; // beyond the shadow range at this depth
        }
        // Perpendicular distance from the light ray to the sampled point; within
        // the assumed thickness it occludes the surface at P.
        float3 perp = (S - P) - sunDir * along;
        float dist = length(perp);
        if (dist < thickness) {
            // Nearer occluders (small t) darken more, and gently fade the far end
            // of the ray so the shadow edge is soft.
            float edge = 1.0 - t;
            shadow = max(shadow, edge * edge);
        }
    }

    float amount = saturate(shadow * strength);

    if (uCustom[37].x > 0.5) {
        return float4(amount, amount, amount, 1.0);
    }

    return float4(color * (1.0 - amount), 1.0);
}
