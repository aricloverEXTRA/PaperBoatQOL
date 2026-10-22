@prism(type='fragment', name='PaperBoat Glow Post', version='1.0.0', description='Single-pass bright glow/bloom for emissive surfaces such as lava fire and magic', author='PaperBoat Shader Mod')

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

    // Keeps only the portion of the colour brighter than the threshold, so dim
    // scenery stays untouched while bright lava/fire/magic bleeds into a halo.
    vec3 pb_bright(vec3 c, float threshold) {
        float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
        float k = max(luma - threshold, 0.0);
        return c * k;
    }

    void main() {
        // uCustom[4] = (threshold, intensity, radiusPixels, enabled)
        float threshold = uCustom[4].x;
        float intensity = uCustom[4].y;
        float radius    = uCustom[4].z;
        float enabled   = uCustom[4].w;

        vec4 base = PB_SAMPLE(vTexCoord0);

        if (enabled < 0.5 || intensity <= 0.0) {
            PB_OUT = vec4(base.rgb, 1.0);
            return;
        }

        // uCustom[1] = (width, height, 1/width, 1/height) - engine built-in.
        vec2 texel = uCustom[1].zw * max(radius, 0.0);

        vec3 glow = vec3(0.0);
        glow += pb_bright(PB_SAMPLE(vTexCoord0 + vec2( texel.x,  0.0)).rgb, threshold);
        glow += pb_bright(PB_SAMPLE(vTexCoord0 + vec2(-texel.x,  0.0)).rgb, threshold);
        glow += pb_bright(PB_SAMPLE(vTexCoord0 + vec2( 0.0,  texel.y)).rgb, threshold);
        glow += pb_bright(PB_SAMPLE(vTexCoord0 + vec2( 0.0, -texel.y)).rgb, threshold);
        glow += 0.7071 * pb_bright(PB_SAMPLE(vTexCoord0 + vec2( texel.x,  texel.y)).rgb, threshold);
        glow += 0.7071 * pb_bright(PB_SAMPLE(vTexCoord0 + vec2(-texel.x,  texel.y)).rgb, threshold);
        glow += 0.7071 * pb_bright(PB_SAMPLE(vTexCoord0 + vec2( texel.x, -texel.y)).rgb, threshold);
        glow += 0.7071 * pb_bright(PB_SAMPLE(vTexCoord0 + vec2(-texel.x, -texel.y)).rgb, threshold);
        glow *= (1.0 / 6.8284);

        vec3 color = base.rgb + glow * intensity;
        PB_OUT = vec4(clamp(color, 0.0, 1.0), 1.0);
    }
@end
