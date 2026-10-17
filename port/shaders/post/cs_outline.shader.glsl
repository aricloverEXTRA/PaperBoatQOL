@prism(type='fragment', name='PaperBoat Outline Post', version='1.0.0', description='Ink outline edge-detect post pass', author='PaperBoat Shader Mod')

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

    void main() {
        // Texel size comes from the engine built-in uCustom[1] = (w, h, 1/w, 1/h).
        vec2 texel = vec2(uCustom[1].z, uCustom[1].w);
        vec2 uv = vTexCoord0;

        const vec3 luma = vec3(0.2126, 0.7152, 0.0722);
        vec4 base = PB_SAMPLE(uv);

        float lumC = dot(base.rgb, luma);
        float lumL = dot(PB_SAMPLE(uv - vec2(texel.x, 0.0)).rgb, luma);
        float lumR = dot(PB_SAMPLE(uv + vec2(texel.x, 0.0)).rgb, luma);
        float lumD = dot(PB_SAMPLE(uv - vec2(0.0, texel.y)).rgb, luma);
        float lumU = dot(PB_SAMPLE(uv + vec2(0.0, texel.y)).rgb, luma);

        float edge = length(vec2(lumR - lumL, lumU - lumD));

        float threshold = uCustom[5].x;
        float strength = uCustom[5].y;
        float ink = smoothstep(threshold, threshold + 0.15, edge) * strength;

        vec3 inkColor = vec3(0.02, 0.02, 0.03);
        vec3 color = mix(base.rgb, inkColor, clamp(ink, 0.0, 1.0));

        PB_OUT = vec4(color, 1.0);
    }
@end
