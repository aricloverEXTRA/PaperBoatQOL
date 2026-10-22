@prism(type='hlsl', name='PaperBoat PBR', version='1.0.0', description='Physically Based Rendering for materials (metallic/roughness)', author='PaperBoat')

// PBR post-process applying GGX BRDF. Requires g_texture0 (albedo), g_texture1 (normal), g_texture2 (ORM: Occlusion=R, Roughness=G, Metallic=B).

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

@if(o_textures[0]) Texture2D g_texture0 : register(t0); SamplerState g_sampler0 : register(s0); @end
@if(o_textures[1]) Texture2D g_texture1 : register(t1); SamplerState g_sampler1 : register(s1); @end
@if(o_textures[2]) Texture2D g_texture2 : register(t2); SamplerState g_sampler2 : register(s2); @end

// uCustom[40] = (enabled, use_orm, env_intensity, debug_mode)
// uCustom[41] = (roughness_override, metallic_override, ao_override, 0)

float3 EnvBRDF(float3 V, float3 N, float3 F0, float roughness, float NdotV) {
    float3 F = F0 + (1.0 - F0) * pow(1.0 - NdotV, 5.0);
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, normalize(V)), 0.001);
    float denom = NdotH * NdotH * (a2 - 1.0) + 1.0;
    float D = a2 / (3.14159 * denom * denom);
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    float GGX_V = NdotV / (NdotV * (1.0 - k) + k);
    float G = GGX_V;
    return D * G * F;
}

float4 PSMain(PSInput input) : SV_TARGET {
    float4 p = uCustom[40];
    if (p.x < 0.5) return float4(1,1,1,1);
    
    float3 albedo = @if(o_textures[0]) g_texture0.Sample(g_sampler0, input.uv0).rgb @else float3(1,1,1) @end;
    
    float3 normal = float3(0,0,1);
    @if(o_textures[1]) {
        float3 nm = g_texture1.Sample(g_sampler1, input.uv1).rgb * 2.0 - 1.0;
        normal = normalize(nm);
    } @end
    
    float ao = 1.0, roughness = 0.5, metallic = 0.0;
    @if(o_textures[2]) {
        float3 orm = g_texture2.Sample(g_sampler2, input.uv2).rgb;
        ao = orm.r; roughness = orm.g; metallic = orm.b;
    } @end
    
    float4 ov = uCustom[41];
    roughness = max(0.01, p.y > 0.5 ? ov.x : roughness);
    metallic = saturate(p.y > 0.5 ? ov.y : metallic);
    ao = saturate(p.y > 0.5 ? ov.z : ao);
    
    float3 F0 = lerp(float3(0.04), albedo, metallic);
    float3 V = normalize(float3(0,0,1));
    float NdotV = max(dot(normal, V), 0.001);
    
    float3 brdf = EnvBRDF(V, normal, F0, roughness, NdotV);
    float3 ambient = uCustom[21].rgb * uCustom[21].a;
    float3 color = albedo * ambient * ao + brdf * p.z;
    
    if (p.w > 0.5) color = normal * 0.5 + 0.5;
    else if (p.w > 1.5) color = float3(roughness);
    else if (p.w > 2.5) color = float3(metallic);
    else if (p.w > 3.5) color = float3(ao);
    
    return float4(color, 1.0);
}