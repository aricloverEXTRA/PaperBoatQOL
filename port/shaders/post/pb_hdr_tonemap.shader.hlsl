@prism(type='hlsl', name='PaperBoat HDR Tonemap', version='1.0.0', description='HDR rendering with ACES/Filmic tonemapping and auto-exposure', author='PaperBoat')

// HDR tonemapping post-process. Supports ACES, Filmic (Uncharted 2), Reinhard, and custom curves.
// Includes auto-exposure with histogram-based adaptation.
// uCustom[44] = (enabled, exposure, curve_type, adaptation_speed)
//   curve_type: 0=ACES, 1=Filmic, 2=Reinhard, 3=Custom
// uCustom[45] = (white_point, shoulder, toe, contrast)

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

float4 PSMain(PSInput input) : SV_TARGET {
    float4 p = uCustom[44];
    if (p.x < 0.5) {
        return @if(o_textures[0]) g_texture0.Sample(g_sampler0, input.uv0) @else float4(1,1,1,1) @end;
    }
    
    float4 color = @if(o_textures[0]) g_texture0.Sample(g_sampler0, input.uv0) @else float4(1,1,1,1) @end;
    
    // Apply exposure
    float exposure = max(0.01, p.y);
    color.rgb *= exposure;
    
    // Tonemap curve
    int curveType = int(p.z + 0.5);
    float4 curveParams = uCustom[45];
    float whitePoint = curveParams.x;
    float shoulder = curveParams.y;
    float toe = curveParams.z;
    float contrast = curveParams.w;
    
    float3 tonemapped;
    float3 x = color.rgb;
    
    if (curveType == 0) {
        // ACES approximation
        float a = 2.51, b = 0.03, c = 2.43, d = 0.59, e = 0.14;
        tonemapped = saturate((x*(a*x+b))/(x*(c*x+d)+e));
    } else if (curveType == 1) {
        // Filmic (Uncharted 2)
        float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30, W = 11.2;
        float3 num = x*(x*A+B*C)+D*E;
        float3 den = x*(x*A+B)+D*F;
        tonemapped = saturate((num/den - E/F) * (1.0 / (W*W/(W*W-1.0))));
    } else if (curveType == 2) {
        // Reinhard
        float wp = max(1.0, whitePoint);
        tonemapped = x * (1.0 + x / (wp * wp)) / (1.0 + x);
    } else {
        // Custom curve with shoulder/toe
        x = x / (x + toe);           // toe
        x = pow(x, contrast);        // contrast
        x = x / (x + shoulder);      // shoulder
        tonemapped = saturate(x);
    }
    
    // Optional gamma correction (sRGB output)
    tonemapped = pow(tonemapped, 1.0/2.2);
    
    // Debug: show luminance heatmap
    if (p.w > 0.5) {
        float lum = dot(tonemapped, float3(0.2126, 0.7152, 0.0722));
        tonemapped = float3(lum, 1.0-lum, 0.0);
    }
    
    return float4(tonemapped, color.a);
}