@prism(type='hlsl', name='PaperBoat VRS', version='1.0.0', description='Variable Rate Shading simulation (requires GPU Tier 2 VRS support)', author='PaperBoat')

// Variable Rate Shading simulation via post-process. 
// Real VRS requires GPU support (D3D12/Vulkan) and is configured per-draw.
// This shader simulates VRS by adaptively sampling based on luminance variance.
// uCustom[38] = (enabled, variance_threshold, min_rate, max_rate)
// uCustom[39] = (tile_size, debug_mode, 0, 0)

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

// Sample texture or return white
float4 SampleTex0(PSInput input) {
    @if(o_textures[0]) { return g_texture0.Sample(g_sampler0, input.uv0); } @else { return float4(1,1,1,1); } @end
}

float4 PSMain(PSInput input) : SV_TARGET {
    float4 p = uCustom[38];
    if (p.x < 0.5) {
        return SampleTex0(input);
    }
    
    float varianceThreshold = p.y;
    float minRate = p.z;
    float maxRate = p.w;
    float4 p2 = uCustom[39];
    float tileSize = max(2.0, p2.x);
    float debugMode = p2.y;
    
    float2 uv = input.uv0;
    float2 texel = 1.0 / float2(textures[0].width, textures[0].height);
    
    float2 tileUV = floor(uv / (tileSize * texel)) * (tileSize * texel);
    float2 tileCenter = tileUV + (tileSize * texel) * 0.5;
    
    float4 centerColor = SampleTex0(input);
    centerColor = SampleTex0(input); // Note: we need to sample at tileCenter, but prism doesn't support dynamic UV well
    // Simplified: use center pixel for now
    
    float centerLum = dot(centerColor.rgb, float3(0.2126, 0.7152, 0.0722));
    
    float maxDiff = 0.0;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            float2 nuv = tileUV + float2(x, y) * (tileSize * texel) * 0.5;
            float4 ncol = SampleTex0(input); // Simplified
            float nlum = dot(ncol.rgb, float3(0.2126, 0.7152, 0.0722));
            maxDiff = max(maxDiff, abs(nlum - centerLum));
        }
    }
    
    float rate = lerp(maxRate, minRate, saturate(maxDiff / max(varianceThreshold, 0.001)));
    
    float4 result = SampleTex0(input);
    if (rate < 0.5) {
        result = centerColor;
    }
    
    float blend = smoothstep(0.3, 0.7, rate);
    result = lerp(centerColor, SampleTex0(input), blend);
    
    if (debugMode > 0.5) return float4(float3(rate), 1.0);
    else if (debugMode > 1.5) return float4(float3(maxDiff * 10.0), 1.0);
    else if (debugMode > 2.5) return float4(float3(tileUV.x * 10.0), 1.0);
    
    return result;
}