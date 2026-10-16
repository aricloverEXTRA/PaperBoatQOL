@prism(type='fragment', name='PaperBoat Color Adjust Post', version='1.0.0', description='Exposure, contrast, saturation, tint and tone mapping post-processing pass', author='PaperBoat Shader Mod')

@if(BACKEND_VULKAN)
#version 450
@include("shaders/vulkan/include/common.glsli")
@else
@{GLSL_VERSION}
@end

@if(VERTEX_SHADER)
    @if(BACKEND_VULKAN)
        @include("shaders/vulkan/include/fast3d_vs.glsli")
    @else
        @include("shaders/opengl/include/fast3d_vs.glsli")
    @end
@else
    @if(BACKEND_VULKAN)
    layout(location = 0) out vec4 vOutColor;
    layout(location = 0) in vec2 vTexCoord0;
    layout(set = 1, binding = 0) uniform sampler2D uTex0;
    #define PB_SAMPLE(uv) texture(uTex0, uv)
    #define PB_OUT vOutColor
    @else
    @if(core_opengl || opengles)
    out vec4 vOutColor;
    @end
    @{attr} vec2 vTexCoord0;
    uniform sampler2D uTex0;
    uniform vec4 uCustom[32];
    #define PB_SAMPLE(uv) @{texture}(uTex0, uv)
    #define PB_OUT @{vOutColor}
    @end

    // ACES filmic tone mapping (Narkowicz approximation).
    vec3 pb_aces(vec3 x) {
        const float a = 2.51;
        const float b = 0.03;
        const float c = 2.43;
        const float d = 0.59;
        const float e = 0.14;
        return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
    }

    void main() {
        vec4 texel = PB_SAMPLE(vTexCoord0);
        vec3 color = texel.rgb;

        // uCustom[2] = (exposure, contrast, saturation, tintMix)
        float exposure   = uCustom[2].x;
        float contrast   = uCustom[2].y;
        float saturation = uCustom[2].z;
        float tintMix    = uCustom[2].w;
        // uCustom[3] = (tintR, tintG, tintB, toneMapEnabled)
        vec3  tint       = uCustom[3].rgb;
        float toneMap    = uCustom[3].a;

        color *= exposure;

        if (toneMap > 0.5) {
            color = pb_aces(color);
        }

        color = (color - 0.5) * contrast + 0.5;

        float luma = dot(color, vec3(0.2126, 0.7152, 0.0722));
        color = mix(vec3(luma), color, saturation);

        color = mix(color, color * tint, clamp(tintMix, 0.0, 1.0));

        PB_OUT = vec4(clamp(color, 0.0, 1.0), texel.a);
    }
@end
