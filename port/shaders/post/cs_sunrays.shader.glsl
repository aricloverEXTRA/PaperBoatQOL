@prism(type='fragment', name='PaperBoat Sunrays Post', version='1.0.0', description='Screen-space radial god rays post pass', author='PaperBoat Shader Mod')

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
        vec2 uv = vTexCoord0;
        const vec3 luma = vec3(0.2126, 0.7152, 0.0722);

        vec4 base = PB_SAMPLE(uv);

        vec2 sunPos = vec2(uCustom[6].x, uCustom[6].y);
        float intensity = uCustom[6].z;
        float decay = uCustom[6].w;

        const int SAMPLES = 40;
        const float density = 0.95;

        // March from the pixel toward the sun, accumulating brightness so that
        // bright skies / lit openings form shafts while dark occluders block them.
        vec2 delta = (uv - sunPos) * (density / float(SAMPLES));
        vec2 coord = uv;
        float illumDecay = 1.0;
        vec3 rays = vec3(0.0);

        for (int i = 0; i < SAMPLES; i++) {
            coord -= delta;
            vec2 c = clamp(coord, vec2(0.0), vec2(1.0));
            float lum = dot(PB_SAMPLE(c).rgb, luma);
            rays += lum * illumDecay;
            illumDecay *= decay;
        }
        rays *= (1.0 / float(SAMPLES)) * intensity;

        vec3 color = base.rgb + rays;
        PB_OUT = vec4(clamp(color, 0.0, 1.0), 1.0);
    }
@end
