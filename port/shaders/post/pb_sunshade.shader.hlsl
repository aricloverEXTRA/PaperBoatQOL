@prism(type='hlsl', name='PaperBoat Sun Shading', version='1.0.0', description='Depth-derived world-space normals relit by a screen-space sun', author='PaperBoat')

// Fullscreen post pass that reconstructs world-space position from the scene
// depth buffer (tile 1, bound by the engine during post-processing) and the
// camera view-projection matrix the port publishes, derives a per-pixel normal
// from the reconstructed geometry, and applies a world-space sun + hemisphere
// light on top of the game image.
// Parameters:
//   uCustom[23..26] = inverse(view-projection) rows (row-major, row-vector)
//   uCustom[27]     = (cameraWorldX, cameraWorldY, cameraWorldZ, unused)
//   uCustom[12] = (sunDirX, sunDirY, sunDirZ, sunIntensity)
//   uCustom[13] = (skyR, skyG, skyB, skyStrength)
//   uCustom[14] = (groundR, groundG, groundB, groundStrength)
//   uCustom[21] = (sunR, sunG, sunB, ambientLevel)
//   uCustom[22] = (sunExposure, unused, debugMode, unused)
//   uCustom[28] = (enabled, strength, wrap, unused)
//   uCustom[29] = (showNormals, 0, 0, 0)
// debugMode: 0 = off, 1 = raw light, 2 = reconstructed normal.

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

// Reconstructs the world-space position for a screen uv / hardware depth pair.
// Hardware depth d is in 0..1 (near..far); clip.z/clip.w = 2d - 1.
float3 worldFromDepth(float2 uv, float d) {
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clip = float4(ndc, 2.0 * d - 1.0, 1.0);
    float4 wp = mul(clip, invViewProj());
    return wp.xyz / max(wp.w, 0.001);
}

float4 PSMain(PSInput input) : SV_TARGET {
    float3 color = g_texture0.Sample(g_sampler0, input.uv0).rgb;

    float4 p = uCustom[28];
    if (p.x < 0.5) {
        return float4(color, 1.0);
    }

    // Check if inverse VP matrix is valid (not all zeros)
    float4x4 invVP = invViewProj();
    bool vpValid = any(abs(invVP[0]) > 0.001) || any(abs(invVP[1]) > 0.001) || 
                   any(abs(invVP[2]) > 0.001) || any(abs(invVP[3]) > 0.001);
    
    if (!vpValid) {
        // VP matrix not set up - return original color with simple lighting
        return float4(color * float3(1.2, 1.2, 1.2), 1.0);
    }

    float dc = g_pbDepth.Sample(g_pbDepthSampler, input.uv0).r;
    // Cleared background / skybox carries no geometry: leave it unlit.
    if (dc >= 0.9999) {
        return float4(color, 1.0);
    }

    uint w, h;
    g_pbDepth.GetDimensions(w, h);
    if (w == 0 || h == 0) {
        return float4(color, 1.0);
    }
    float2 texel = 1.0 / float2((float)w, (float)h);

    float3 P = worldFromDepth(input.uv0, dc);
    // Check if world position is valid
    if (any(isnan(P)) || any(isinf(P))) {
        return float4(color, 1.0);
    }
    
    // Neighbouring world positions; their cross product is the surface normal.
    float3 Px = worldFromDepth(input.uv0 + float2(texel.x, 0.0), g_pbDepth.Sample(g_pbDepthSampler, input.uv0 + float2(texel.x, 0.0)).r);
    float3 Py = worldFromDepth(input.uv0 + float2(0.0, texel.y), g_pbDepth.Sample(g_pbDepthSampler, input.uv0 + float2(0.0, texel.y)).r);
    
    if (any(isnan(Px)) || any(isinf(Px)) || any(isnan(Py)) || any(isinf(Py))) {
        return float4(color, 1.0);
    }
    
    float3 n = normalize(cross(normalize(Px - P), normalize(Py - P)));
    if (any(isnan(n)) || any(isinf(n))) {
        return float4(color, 1.0);
    }

    // Keep the normal facing the camera so a depth-derived sign flip never makes
    // a surface read as lit from behind.
    float3 viewDir = normalize(uCustom[27].xyz - P);
    if (any(isnan(viewDir)) || any(isinf(viewDir))) {
        return float4(color, 1.0);
    }
    if (dot(n, viewDir) < 0.0) {
        n = -n;
    }

    if (uCustom[29].x > 0.5) {
        return float4(n * 0.5 + 0.5, 1.0);
    }

    float wrap = p.z;
    float ambientLevel = uCustom[21].w > 0.001 ? uCustom[21].w : 1.0;

    float hemi = n.y * 0.5 + 0.5;
    float3 sky = uCustom[13].rgb * uCustom[13].a;
    float3 ground = uCustom[14].rgb * uCustom[14].a;
    float3 light = lerp(ground, sky, hemi) * ambientLevel;

    float3 sunRaw = uCustom[12].xyz;
    float sunLenSq = dot(sunRaw, sunRaw);
    float3 sunDir = sunLenSq > 1e-8 ? sunRaw * rsqrt(sunLenSq) : float3(0.0, 1.0, 0.0);
    float3 sunColor = (uCustom[21].r + uCustom[21].g + uCustom[21].b) > 0.001 ? uCustom[21].rgb : float3(1.0, 1.0, 1.0);
    float ndl = saturate((dot(n, sunDir) + wrap) / (1.0 + wrap));
    light += ndl * sunColor * uCustom[12].w;

    float exposure = uCustom[22].x > 0.001 ? uCustom[22].x : 1.0;
    light *= exposure;

    if (uCustom[22].z > 1.5) {
        return float4(light, 1.0);
    }

    return float4(color * lerp(float3(1.0, 1.0, 1.0), light, p.y), 1.0);
}