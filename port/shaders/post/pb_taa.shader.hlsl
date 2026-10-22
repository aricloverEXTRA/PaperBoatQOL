@prism(type='hlsl', name='PaperBoat TAA', version='1.0.0', description='Temporal Anti-Aliasing with jitter and history blending', author='PaperBoat')

// Temporal Anti-Aliasing. Uses jittered projection + history buffer blending.
// Requires velocity buffer (tile 2) and previous frame color (tile 3).
// uCustom[33] = (enabled, jitter_scale, blend_factor, clamp_factor)

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

@if(o_textures[0]) Texture2D g_texture0 : register(t0); SamplerState g_sampler0 : register(s0); @end
@if(o_textures[1]) Texture2D g_texture1 : register(t1); SamplerState g_sampler1 : register(s1); @end
@if(o_textures[2]) Texture2D g_texture2 : register(t2); SamplerState g_sampler2 : register(s2); @end
@if(o_textures[3]) Texture2D g_texture3 : register(t3); SamplerState g_sampler3 : register(s3); @end

// Sample color texture
float4 SampleColor0(PSInput input, float2 uv) {
    @if(o_textures[0]) { return g_texture0.Sample(g_sampler0, uv); } @else { return float4(1,1,1,1); } @end
}

// Sample depth texture
float SampleDepth(PSInput input, float2 uv) {
    @if(o_textures[1]) { return g_texture1.Sample(g_sampler1, uv).r; } @else { return 1.0; } @end
}

// Sample velocity texture
float2 SampleVelocity(PSInput input) {
    @if(o_textures[2]) { return g_texture2.Sample(g_sampler2, input.uv2).xy; } @else { return float2(0,0); } @end
}

// Sample history texture
float4 SampleHistory(PSInput input, float2 uv) {
    @if(o_textures[3]) { return g_texture3.Sample(g_sampler3, uv); } @else { return SampleColor0(input, uv); } @end
}

// Sample current color
float4 SampleCurrent(PSInput input) {
    @if(o_textures[0]) { return g_texture0.Sample(g_sampler0, input.uv0); } @else { return float4(1,1,1,1); } @end
}

// Sample neighbor color
float4 SampleNeighbor(PSInput input, float2 uv) {
    @if(o_textures[0]) { return g_texture0.Sample(g_sampler0, uv); } @else { return SampleCurrent(input); } @end
}

float4 PSMain(PSInput input) : SV_TARGET {
    float4 p = uCustom[33];
    if (p.x < 0.5) {
        @if(o_textures[0]) { return g_texture0.Sample(g_sampler0, input.uv0); } @else { return float4(1,1,1,1); } @end;
    }
    
    float4 current = SampleCurrent(input);
    float4 history = SampleHistory(input, input.uv3);
    float2 velocity = SampleVelocity(input);
    float depth = SampleDepth(input, input.uv1);
    
    // Reproject history using velocity
    float2 historyUV = input.uv0 - velocity * 0.5;
    history = SampleHistory(input, historyUV);
    
    // Neighborhood clamping (YCoCg color space for better clamping)
    float3 YCoCg = float3(
        dot(current.rgb, float3(0.25, 0.5, 0.25)),
        dot(current.rgb, float3(-0.25, 0.5, -0.25)),
        dot(current.rgb, float3(0.25, 0.0, -0.25))
    );
    
    // Sample neighborhood
    float2 texel = 1.0 / float2(textures[0].width, textures[0].height);
    float3 minC = YCoCg, maxC = YCoCg;
    for (int y = -1; y <= 1; y++) {
        for (int x = -1; x <= 1; x++) {
            if (x == 0 && y == 0) continue;
            float2 nuv = input.uv0 + float2(x, y) * texel;
            float4 ncol = SampleNeighbor(input, nuv);
            float3 nYCoCg = float3(
                dot(ncol.rgb, float3(0.25, 0.5, 0.25)),
                dot(ncol.rgb, float3(-0.25, 0.5, -0.25)),
                dot(ncol.rgb, float3(0.25, 0.0, -0.25))
            );
            minC = min(minC, nYCoCg);
            maxC = max(maxC, nYCoCg);
        }
    }
    
    float3 historyYCoCg = float3(
        dot(history.rgb, float3(0.25, 0.5, 0.25)),
        dot(history.rgb, float3(-0.25, 0.5, -0.25)),
        dot(history.rgb, float3(0.25, 0.0, -0.25))
    );
    
    // Clamp history to neighborhood
    float clampFactor = p.w;
    historyYCoCg = clamp(historyYCoCg, lerp(YCoCg, minC, clampFactor), lerp(YCoCg, maxC, clampFactor));
    
    // Convert back to RGB
    float3 historyClamped = float3(
        historyYCoCg.x + historyYCoCg.y + historyYCoCg.z,
        historyYCoCg.x - historyYCoCg.z,
        historyYCoCg.x - historyYCoCg.y
    );
    
    // Blend
    float blend = saturate(p.z);
    float3 result = lerp(current.rgb, historyClamped, blend);
    
    // Debug
    if (p.w > 1.5) result = velocity.xy * 100.0;
    else if (p.w > 2.5) result = history.rgb;
    else if (p.w > 3.5) result = current.rgb;
    
    return float4(result, current.a);
}