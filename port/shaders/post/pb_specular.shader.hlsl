@prism(type='hlsl', name='PaperBoat Specular', version='1.0.0', description='Depth-derived specular highlights from the screen-space sun', author='PaperBoat')

// Fullscreen post pass that adds a specular highlight to lit geometry. Like the
// sun-shading pass it reconstructs world-space position from the scene depth
// buffer (tile 1), derives a per-pixel normal, then evaluates a Blinn-Phong
// half-vector between the sun direction and the view direction and adds the
// reflection to the image. It needs no material IDs, so it works on every
// surface and every backend; the shininess control stands in for roughness.
//
// Parameters:
//   uCustom[23..26] = inverse(view-projection) rows (row-major, row-vector)
//   uCustom[27]     = (cameraWorldX, cameraWorldY, cameraWorldZ, unused)
//   uCustom[12] = (sunDirX, sunDirY, sunDirZ, sunIntensity)
//   uCustom[21] = (sunR, sunG, sunB, ambientLevel)
//   uCustom[38] = (enabled, strength, shininess, unused)
//   uCustom[39] = (debugView, 0, 0, 0)
// debugView: 0 = off, 1 = raw specular term.

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

float4x4 invViewProj() {
    return float4x4(uCustom[23], uCustom[24], uCustom[25], uCustom[26]);
}

float3 worldFromDepth(float2 uv, float d) {
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    float4 clip = float4(ndc, 2.0 * d - 1.0, 1.0);
    float4 wp = mul(clip, invViewProj());
    return wp.xyz / wp.w;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float3 color = g_texture0.Sample(g_sampler0, input.uv0).rgb;

    float4 p = uCustom[38];
    if (p.x < 0.5) {
        return float4(color, 1.0);
    }

    float dc = g_pbDepth.Sample(g_pbDepthSampler, input.uv0).r;
    if (dc >= 0.9999) {
        return float4(color, 1.0);
    }

    uint w, h;
    g_pbDepth.GetDimensions(w, h);
    float2 texel = 1.0 / float2((float)w, (float)h);

    float3 P = worldFromDepth(input.uv0, dc);
    float3 Px = worldFromDepth(input.uv0 + float2(texel.x, 0.0), g_pbDepth.Sample(g_pbDepthSampler, input.uv0 + float2(texel.x, 0.0)).r);
    float3 Py = worldFromDepth(input.uv0 + float2(0.0, texel.y), g_pbDepth.Sample(g_pbDepthSampler, input.uv0 + float2(0.0, texel.y)).r);
    float3 n = normalize(cross(normalize(Px - P), normalize(Py - P)));

    float3 viewDir = normalize(uCustom[27].xyz - P);
    if (dot(n, viewDir) < 0.0) {
        n = -n;
    }

    float3 sunRaw = uCustom[12].xyz;
    float sunLenSq = dot(sunRaw, sunRaw);
    float3 sunDir = sunLenSq > 1e-8 ? sunRaw * rsqrt(sunLenSq) : float3(0.0, 1.0, 0.0);
    float3 sunColor = (uCustom[21].r + uCustom[21].g + uCustom[21].b) > 0.001 ? uCustom[21].rgb : float3(1.0, 1.0, 1.0);

    // Blinn-Phong: the half-vector between the light and the view. Ground-facing
    // fragments contribute nothing, so only surfaces actually lit by the sun shine.
    float3 halfVec = normalize(sunDir + viewDir);
    float ndl = saturate(dot(n, sunDir));
    float ndh = saturate(dot(n, halfVec));
    float shininess = max(p.z, 1.0);
    float spec = pow(ndh, shininess) * ndl;

    if (uCustom[39].x > 0.5) {
        return float4(spec * sunColor, 1.0);
    }

    // Additive so highlights brighten without darkening the rest of the image.
    float3 result = color + spec * sunColor * p.y;
    return float4(result, 1.0);
}
