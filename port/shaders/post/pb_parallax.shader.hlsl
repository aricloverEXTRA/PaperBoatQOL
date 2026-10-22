@prism(type='hlsl', name='PaperBoat Parallax', version='1.0.0', description='Parallax Occlusion Mapping for depth illusion on flat surfaces', author='PaperBoat')

// Parallax Occlusion Mapping (POM). Uses height map in alpha channel of normal map
// or separate height texture. Ray-marches in tangent space for self-occlusion.
// uCustom[36] = (enabled, height_scale, min_layers, max_layers)
// uCustom[37] = (fudge_factor, debug_mode, 0, 0)

struct PSInput {
    float4 position : SV_POSITION;
    @{update_floats(1)}
    @for(i in 0..3)
        @if(o_textures[i])
            float2 uv@{i} : TEXCOORD@{i};
            @{update_floats(2)}
        @end
    @end
};

cbuffer TransformCB : register(b4) {
    float4 mtx_palette[32];
    float4 y_scale;
}

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

@if(o_textures[0]) Texture2D g_texture0 : register(t0); SamplerState g_sampler0 : register(s0); @end  // albedo
@if(o_textures[1]) Texture2D g_texture1 : register(t1); SamplerState g_sampler1 : register(s1); @end  // normal (height in alpha)
@if(o_textures[2]) Texture2D g_texture2 : register(t2); SamplerState g_sampler2 : register(s2); @end  // height (optional)

// Sample height from texture 2 (dedicated) or texture 1 alpha (normal map alpha)
float GetHeight(float2 u) {
    @if(o_textures[2]) { return g_texture2.Sample(g_sampler2, u).r; }
    @else @if(o_textures[1]) { return g_texture1.Sample(g_sampler1, u).a; }
    @else { return 0.0; }
}

// Sample albedo
float4 SampleAlbedo(float2 uv) {
    @if(o_textures[0]) { return g_texture0.Sample(g_sampler0, uv); } @else { return float4(1,1,1,1); } @end
}

// Sample normal
float3 SampleNormal(float2 uv) {
    @if(o_textures[1]) { return g_texture1.Sample(g_sampler1, uv).rgb * 2.0 - 1.0; } @else { return float3(0,0,1); } @end
}

float4 PSMain(PSInput input) : SV_TARGET {
    float4 p = uCustom[36];
    if (p.x < 0.5) {
        @if(o_textures[0]) { return g_texture0.Sample(g_sampler0, input.uv0); } @else { return float4(1,1,1,1); } @end;
    }
    
    float2 uv = input.uv0;
    float heightScale = p.y;
    int minLayers = max(1, int(p.z + 0.5));
    int maxLayers = max(minLayers, int(p.w + 0.5));
    float4 p2 = uCustom[37];
    float fudgeFactor = max(0.01, p2.x);
    float debugMode = p2.y;
    
    // View direction in tangent space (approximate for screen-space)
    float3 viewDir = normalize(float3(0, 0, -1));
    float viewDirZ = max(abs(viewDir.z), 0.001);
    
    // Parallax offset
    float2 parallaxOffset = viewDir.xy * heightScale * (1.0 / viewDirZ);
    
    // Steep parallax / POM
    float2 deltaUV = parallaxOffset / float(maxLayers);
    float2 currUV = uv;
    float currHeight = 1.0;
    float prevHeight = 1.0;
    int layer = 0;
    
    currHeight = GetHeight(currUV);
    
    // Find intersection
    for (layer = 0; layer < maxLayers; ++layer) {
        currUV -= deltaUV;
        prevHeight = currHeight;
        currHeight = GetHeight(currUV);
        
        // Intersection test: height map crosses view ray
        float layerDepth = float(layer + 1) / float(maxLayers);
        if (currHeight < layerDepth) break;
    }
    
    // Binary search for precise intersection
    float2 finalUV = uv;
    if (layer > 0 && layer < maxLayers) {
        // Linear interpolation between layers
        float deltaH = currHeight - prevHeight;
        float t = 0.0;
        if (abs(deltaH) > 0.001) {
            t = (float(layer) / float(maxLayers) - prevHeight) / deltaH;
            t = clamp(t, 0.0, 1.0);
        }
        finalUV = uv - deltaUV * (float(layer) + t);
    } else if (layer > 0) {
        finalUV = currUV;
    }
    
    // Sample final color
    float4 albedo = SampleAlbedo(finalUV);
    float3 normal = SampleNormal(finalUV);
    
    // Simple lighting with perturbed normal
    float3 lightDir = normalize(float3(0.5, 0.5, -1.0));
    float NdotL = max(dot(normal, lightDir), 0.0);
    float3 lighting = albedo.rgb * (0.3 + 0.7 * NdotL);
    
    // Debug
    if (debugMode > 0.5) return float4(float3(float(layer) / float(maxLayers)), 1.0); // layer count
    else if (debugMode > 1.5) return float4(normal * 0.5 + 0.5, 1.0);                   // normal
    else if (debugMode > 2.5) return float4(float3(currHeight), 1.0);                  // height
    
    return float4(lighting, albedo.a);
}