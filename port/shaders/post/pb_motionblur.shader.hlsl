@prism(type='hlsl', name='PaperBoat Motion Blur', version='1.0.0', description='Camera and object motion blur using velocity buffer', author='PaperBoat')

// Motion blur post-process. Samples along velocity vectors for camera/object blur.
// uCustom[34] = (enabled, strength, num_samples, camera_blur)
// uCustom[35] = (velocity_scale, max_blur, min_velocity, debug_mode)

struct PSInput {
    float4 position : SV_POSITION;
    @{update_floats(1)}
    @for(i in 0..2)
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

@if(o_textures[0]) Texture2D g_texture0 : register(t0); SamplerState g_sampler0 : register(s0); @end  // color
@if(o_textures[1]) Texture2D g_texture1 : register(t1); SamplerState g_sampler1 : register(s1); @end  // velocity

float4 PSMain(PSInput input) : SV_TARGET {
    float4 p = uCustom[34];
    if (p.x < 0.5) {
        return @if(o_textures[0]) g_texture0.Sample(g_sampler0, input.uv0) @else float4(1,1,1,1) @end;
    }
    
    float4 color = @if(o_textures[0]) g_texture0.Sample(g_sampler0, input.uv0) @else float4(1,1,1,1) @end;
    float2 velocity = @if(o_textures[1]) g_texture1.Sample(g_sampler1, input.uv1).xy @else float2(0,0) @end;
    
    float strength = p.y;
    int numSamples = max(1, int(p.z + 0.5));
    float cameraBlur = p.w;
    float4 p2 = uCustom[35];
    float velScale = p2.x;
    float maxBlur = p2.y;
    float minVel = p2.z;
    float debugMode = p2.w;
    
    if (numSamples > 16) numSamples = 16;
    
    velocity *= velScale;
    float velLen = length(velocity);
    
    if (velLen < minVel) {
        return color;
    }
    
    // Limit blur radius
    float blurRadius = min(velLen * strength, maxBlur);
    float2 dir = velocity / max(velLen, 0.001);
    
    // Camera motion blur uses screen-center radial blur
    float2 camCenter = float2(0.5, 0.5);
    float2 camDir = input.uv0 - camCenter;
    float camDist = length(camDir);
    if (camDist > 0.001) {
        camDir /= camDist;
    }
    
    float3 accum = color.rgb;
    float weightSum = 1.0;
    
    // Sample along velocity (object motion)
    for (int i = 1; i <= numSamples; ++i) {
        float t = float(i) / float(numSamples);
        float2 offset = dir * blurRadius * t;
        
        float4 sample = @if(o_textures[0]) g_texture0.Sample(g_sampler0, input.uv0 - offset) @else color @end;
        float w = 1.0 - t * 0.5; // trapezoidal weighting
        accum += sample.rgb * w;
        weightSum += w;
        
        // Camera blur component
        if (cameraBlur > 0.5) {
            float2 camOffset = camDir * blurRadius * cameraBlur * t;
            float4 camSample = @if(o_textures[0]) g_texture0.Sample(g_sampler0, input.uv0 - camOffset) @else color @end;
            accum += camSample.rgb * w * cameraBlur * 0.5;
            weightSum += w * cameraBlur * 0.5;
        }
    }
    
    float3 result = accum / weightSum;
    
    // Debug
    if (debugMode > 0.5) result = float3(velLen * 10.0);        // 1 = velocity magnitude
    else if (debugMode > 1.5) result = dir * 0.5 + 0.5;         // 2 = velocity direction
    else if (debugMode > 2.5) result = color.rgb;               // 3 = original
    
    return float4(saturate(result), color.a);
}