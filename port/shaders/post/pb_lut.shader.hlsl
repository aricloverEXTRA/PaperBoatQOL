@prism(type='hlsl', name='PaperBoat LUT', version='1.0.0', description='3D LUT color grading for per-area mood', author='PaperBoat')

// 3D LUT color grading using 2D texture with LUT slices laid out in a grid.
// uCustom[42] = (enabled, intensity, lut_index, debug_view)
// uCustom[43] = (lut_size, grid_width, grid_height, 0)

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

// Sample texture 0 or return white
float4 SampleTex0(PSInput input) {
    @if(o_textures[0]) { return g_texture0.Sample(g_sampler0, input.uv0); } @else { return float4(1,1,1,1); } @end
}

// Sample 2D LUT texture (grid of 3D LUT slices)
float3 SampleLUT(float3 color, float lutSize, float gridW, float gridH) {
    @if(o_textures[1]) {
        if (lutSize > 1.0) {
            float3 lutCoord = color.rgb;
            float slice = lutCoord.b * (gridH * gridW - 1);
            float sliceIdx = floor(slice);
            float sliceFrac = frac(slice);
            
            float2 sliceUV = float2(
                fmod(sliceIdx, gridW) / gridW + lutCoord.r / (lutSize * gridW),
                floor(sliceIdx / gridW) / gridH + lutCoord.g / (lutSize * gridH)
            );
            
            return g_texture1.Sample(g_sampler1, sliceUV).rgb;
        }
    } @end
    return color.rgb;
}

// Debug LUT slice view
float3 DebugLUT(float3 color, float debug) {
    if (debug > 0.5) {
        float slice = frac(debug - 0.5) * 6.0;
        float z = floor(slice) / 6.0;
        float2 uv = float2(frac(slice) * 0.166 + color.r * 0.166, color.g);
        @if(o_textures[1]) { return g_texture1.Sample(g_sampler1, float3(uv, z)).rgb; } @else { return color; } @end
    }
    return color;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float4 p = uCustom[42];
    if (p.x < 0.5) {
        return SampleTex0(input);
    }
    
    float4 color = SampleTex0(input);
    float intensity = saturate(p.y);
    
    // Apply 2D LUT (slices in grid) if available
    float lutSize = uCustom[43].x;
    float gridW = uCustom[43].y > 0 ? uCustom[43].y : 16.0;
    float gridH = uCustom[43].z > 0 ? uCustom[43].z : 16.0;
    color.rgb = SampleLUT(color.rgb, lutSize, gridW, gridH);
    
    // Blend with original based on intensity
    float4 original = SampleTex0(input);
    color.rgb = lerp(original.rgb, color.rgb, intensity);
    
    // Debug: show LUT slice
    color.rgb = DebugLUT(color.rgb, p.w);
    
    return color;
}