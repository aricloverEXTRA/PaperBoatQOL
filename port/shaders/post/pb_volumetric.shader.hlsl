@prism(type='hlsl', name='PaperBoat Volumetric', version='1.0.0', description='Volumetric light shafts (god rays) with depth-based ray marching', author='PaperBoat')

// Volumetric light shafts using ray marching from screen-space sun position.
// Sun screen UV comes from CameraPatches via port_get_sun_screen_uv (register 30.xy).
// Requires depth buffer (engine exposes on tile 1 during post-processing).

struct PSInput {
    float4 position : SV_POSITION;
    @{update_floats(1)}
    @for(i in 0..1)
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

@if(o_textures[0]) Texture2D g_texture0 : register(t0); SamplerState g_sampler0 : register(s0); @end
@if(o_textures[1]) Texture2D g_texture1 : register(t1); SamplerState g_sampler1 : register(s1); @end

// uCustom[30] = (sun_uv_x, sun_uv_y, sun_visible, intensity) -- from CameraPatches
// uCustom[31] = (density, decay, weight, threshold)
// uCustom[32] = (num_samples, max_length, jitter, debug_mode)

float4 PSMain(PSInput input) : SV_TARGET {
    // Sample base color
    float4 base = @if(o_textures[0]) g_texture0.Sample(g_sampler0, input.uv0) @else float4(1,1,1,1) @end;
    float4 p30 = uCustom[30];
    float4 p31 = uCustom[31];
    float4 p32 = uCustom[32];
    
    if (p30.z < 0.5) return base; // sun not visible
    
    float2 sunUV = p30.xy;
    float intensity = p30.w;
    float density = p31.x;
    float decay = p31.y;
    float weight = p31.z;
    float threshold = p31.w;
    int numSamples = int(p32.x + 0.5);
    float maxLength = p32.y;
    float jitter = p32.z;
    float debugMode = p32.w;
    
    if (numSamples <= 0) numSamples = 32;
    if (maxLength <= 0) maxLength = 1.0;
    
    float2 delta = (sunUV - input.uv0) * (maxLength / float(numSamples));
    delta *= 1.0 + jitter * (frac(sin(dot(input.uv0 * 100.0, float2(12.9898, 78.233))) * 43758.5453) - 0.5) * 0.01;
    
    float2 coord = input.uv0;
    float3 shafts = 0;
    float t = 1.0;
    
    for (int i = 0; i < 64; ++i) {
        if (i >= numSamples) break;
        
        // Sample depth for occlusion
        float depth = @if(o_textures[1]) g_texture1.Sample(g_sampler1, coord).r @else 1.0 @end;
        
        // Occlusion: stop if we hit geometry
        if (depth < 0.01) break;
        
        // Sample color at coordinate
        float3 sample = @if(o_textures[0]) g_texture0.Sample(g_sampler0, coord).rgb @else float3(1,1,1) @end;
        float lum = dot(sample, float3(0.2126, 0.7152, 0.0722));
        
        if (lum > threshold) {
            shafts += sample * weight * t * density;
        }
        
        t *= decay;
        coord += delta;
        
        // Early exit if off-screen
        if (coord.x < 0 || coord.x > 1 || coord.y < 0 || coord.y > 1) break;
    }
    
    float3 result = base.rgb + shafts * intensity;
    
    // Debug modes
    if (debugMode > 0.5) result = float3(t);                    // 1 = transmittance
    else if (debugMode > 1.5) result = float3(coord.x);        // 2 = final coord x
    else if (debugMode > 2.5) result = shafts;                 // 3 = shafts only
    
    return float4(saturate(result), base.a);
}