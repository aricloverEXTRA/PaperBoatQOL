@prism(type='hlsl', name='PaperBoat Post Debug (D3D11)', version='1.0.0', description='Diagnostic post-processing pass used to isolate geometry UV sampling and presentation failures', author='PaperBoat Shader Mod')

@if(o_root_signature)
    #define RS "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | DENY_VERTEX_SHADER_ROOT_ACCESS), CBV(b0, visibility = SHADER_VISIBILITY_PIXEL), DescriptorTable(SRV(t0), visibility = SHADER_VISIBILITY_PIXEL), DescriptorTable(Sampler(s0), visibility = SHADER_VISIBILITY_PIXEL)"
@end

struct PSInput {
    float4 position : SV_POSITION;
    float2 uv0 : TEXCOORD0;
};

// Mirrors struct TransformUniforms in gfx_rendering_api.h
cbuffer TransformCB : register(b4) {
    float4 mtx_palette[32];
    float4 y_scale;
}

// Mirrors struct PerDrawCB in gfx_direct3d11.cpp (the field layout must match or
// uCustom reads the wrong register).
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
    float4 uCustom[32];
}

Texture2D g_texture0 : register(t0);
SamplerState g_sampler0 : register(s0);

PSInput VSMain(float4 position : POSITION, float mtxSlot : MTXSLOT, float2 uv0 : TEXCOORD0) {
    PSInput result;

    // RSP position transform: matrix palette entry selected per vertex
    int mtxBase = int(mtxSlot + 0.5) * 4;
    float4 clipPos = position.x * mtx_palette[mtxBase] + position.y * mtx_palette[mtxBase + 1] +
                     position.z * mtx_palette[mtxBase + 2] + position.w * mtx_palette[mtxBase + 3];

    // D3D clip space: z in 0..1; y flip is carried in the uniform
    result.position = float4(clipPos.x, clipPos.y * y_scale.x, (clipPos.z + clipPos.w) / 2.0, clipPos.w);
    result.uv0 = float2(uv0.x * uv_transform[0].x + uv_transform[0].y,
                        uv0.y * uv_transform[0].z + uv_transform[0].w);
    return result;
}

@if(o_root_signature)
    [RootSignature(RS)]
@end

struct PSOutput {
    float4 color : SV_TARGET;
};

// uCustom[7].x selects the diagnostic mode:
//   1 = passthrough (sampling test: image should look normal)
//   2 = solid magenta (geometry test: whole screen should be magenta)
//   3 = UV grid (UV test: 8x8 checker should fill the screen)
//   4 = invert (sampling test: image should be negative)
//   5 = UV as colour (UV test: horizontal red / vertical green gradient)
PSOutput PSMain(PSInput input, float4 screenSpace : SV_Position) {
    int mode = (int)(uCustom[7].x + 0.5);
    float4 texel = g_texture0.Sample(g_sampler0, input.uv0);

    PSOutput output;
    if (mode == 2) {
        output.color = float4(1.0, 0.0, 1.0, 1.0);
    } else if (mode == 3) {
        float2 g = floor(input.uv0 * 8.0);
        float c = fmod(g.x + g.y, 2.0);
        output.color = float4(c, c, c, 1.0);
    } else if (mode == 4) {
        output.color = float4(1.0 - texel.rgb, 1.0);
    } else if (mode == 5) {
        output.color = float4(saturate(input.uv0), 0.0, 1.0);
    } else {
        output.color = float4(texel.rgb, 1.0);
    }
    return output;
}
