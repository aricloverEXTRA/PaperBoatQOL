with open('src/port/enhancements/graphics/PostFx.cpp', 'r') as f:
    content = f.read()

# Add DX11 check at the start of each Update function
# Pattern: "Fast::Interpreter* interp = GameEngine_GetInterpreter();\n    if (interp == nullptr) {\n        return;\n    }\n\n    if (on && s..."

old_pattern = '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on &&'''

new_pattern = '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on &&'''

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sSsaoPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sSsaoPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sDepthDebugPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sDepthDebugPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sSunShadePassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sSunShadePassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sGodRaysPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sGodRaysPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sContactShadowPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sContactShadowPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sScreenSpaceShadowPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sScreenSpaceShadowPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sBloomPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sBloomPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sFxaaPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sFxaaPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sSpecularPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sSpecularPassId < 0) {''')

# Modern shader suite
content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sPbrPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sPbrPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sLutPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sLutPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sHdrPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sHdrPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sVolumetricPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sVolumetricPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sTaaPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sTaaPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sMotionBlurPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sMotionBlurPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sParallaxPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sParallaxPassId < 0) {''')

content = content.replace(
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sVrsPassId < 0) {''',
    '''Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    // Only register passes on DirectX 11 (Windows)
    if (!PostFx_IsDirectX11()) {
        return;
    }

    if (on && sVrsPassId < 0) {''')

with open('src/port/enhancements/graphics/PostFx.cpp', 'w') as f:
    f.write(content)

print('Added DX11 checks to all pass registration functions')