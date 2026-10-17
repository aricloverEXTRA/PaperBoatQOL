@prism(type='fragment', name='PaperBoat Post Debug', version='1.0.0', description='Diagnostic post-processing pass used to isolate geometry UV sampling and presentation failures', author='PaperBoat Shader Mod')

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

    // uCustom[7].x selects the diagnostic mode:
    //   1 = passthrough (sampling test: image should look normal)
    //   2 = solid magenta (geometry test: whole screen should be magenta)
    //   3 = UV grid (UV test: 8x8 checker should fill the screen)
    //   4 = invert (sampling test: image should be negative)
    //   5 = UV as colour (UV test: horizontal red / vertical green gradient)
    void main() {
        int mode = int(uCustom[7].x + 0.5);
        vec4 texel = PB_SAMPLE(vTexCoord0);

        if (mode == 2) {
            PB_OUT = vec4(1.0, 0.0, 1.0, 1.0);
        } else if (mode == 3) {
            vec2 g = floor(vTexCoord0 * 8.0);
            float c = mod(g.x + g.y, 2.0);
            PB_OUT = vec4(c, c, c, 1.0);
        } else if (mode == 4) {
            PB_OUT = vec4(1.0 - texel.rgb, 1.0);
        } else if (mode == 5) {
            PB_OUT = vec4(clamp(vTexCoord0, 0.0, 1.0), 0.0, 1.0);
        } else {
            PB_OUT = vec4(texel.rgb, 1.0);
        }
    }
@end
