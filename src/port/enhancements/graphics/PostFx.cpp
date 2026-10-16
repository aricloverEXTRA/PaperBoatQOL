/**
 * PaperBoat post-processing effects.
 *
 * Drives the libultraship post-pass chain from PaperBoat CVars:
 *   - a colour-adjust pass (exposure / contrast / saturation / tint / tone mapping)
 *   - a single-pass glow/bloom pass for emissive surfaces such as lava and fire
 *
 * The shipped shaders have OpenGL (GLSL) and DirectX 11 (HLSL) variants plus a
 * Vulkan variant. Registering a post pass whose shader file is missing for the
 * active backend aborts the engine, so every pass is hard-gated to the backends
 * that ship a variant (OpenGL/Vulkan/DirectX) and to the opt-in
 * "PostProcessing" CVar (default off). Metal has no variant and stays disabled.
 */

// Fast3dWindow.h pulls in <fast/interpreter.h> -> prism, which must be parsed
// before anything that defines the script_api "Else" macro (pulled in
// transitively by port/hooks/Events.h via common_structs.h).
#include <fast/Fast3dWindow.h>

#include "port/hooks/Events.h"
#include "port/ui/cvar_prefixes.h"

#include <libultraship/libultraship.h>
#include <libultraship/bridge/consolevariablebridge.h>

#include <memory>

#include "common.h"

// Post-pass API from the fast/prism renderer. Declared by hand (like
// LightingManager.cpp) to avoid pulling in the heavy interpreter header.
extern "C" {
int gfx_register_post_pass(const char* o2rShaderPath);
void gfx_unregister_post_pass(int id);
void gfx_set_custom_uniform(uint8_t idx, const float values[4]);
}

#define POSTFX_MASTER_CVAR       CVAR_ENHANCED_SHADING("PostProcessing")
#define POSTFX_GLOW_CVAR         CVAR_ENHANCED_SHADING("Bloom")
#define POSTFX_GLOW_THRESH_CVAR  CVAR_ENHANCED_SHADING("BloomThreshold")
#define POSTFX_GLOW_INT_CVAR     CVAR_ENHANCED_SHADING("BloomIntensity")
#define POSTFX_GLOW_RADIUS_CVAR  CVAR_ENHANCED_SHADING("BloomRadius")
#define POSTFX_OUTLINE_CVAR        CVAR_ENHANCED_SHADING("Outlines")
#define POSTFX_OUTLINE_THRESH_CVAR CVAR_ENHANCED_SHADING("OutlineThreshold")
#define POSTFX_OUTLINE_STRENGTH_CVAR CVAR_ENHANCED_SHADING("OutlineStrength")
#define POSTFX_TONEMAP_CVAR      CVAR_ENHANCED_SHADING("ToneMapping")
#define POSTFX_EXPOSURE_CVAR     CVAR_ENHANCED_SHADING("Exposure")
#define POSTFX_CONTRAST_CVAR     CVAR_ENHANCED_SHADING("PostContrast")
#define POSTFX_SATURATION_CVAR   CVAR_ENHANCED_SHADING("PostSaturation")
#define POSTFX_TINTMIX_CVAR      CVAR_ENHANCED_SHADING("PostTintMix")
#define POSTFX_TINT_COLOR_CVAR   CVAR_ENHANCED_SHADING("PostTintColor")

#define POSTFX_COLOR_ADJUST_SHADER "shaders/post/cs_color_adjust.shader"
#define POSTFX_GLOW_SHADER         "shaders/post/cs_glow.shader"
#define POSTFX_OUTLINE_SHADER      "shaders/post/cs_outline.shader"

static int gColorAdjustHandle = -1;
static int gGlowHandle = -1;
static int gOutlineHandle = -1;
// Bits: 0 = colour adjust, 1 = glow, 2 = outline.
static int gRegisteredSignature = -1;

static bool PostFx_BackendSupported() {
    Ship::Context* ctx = Ship::Context::GetRawInstance();
    if (ctx == nullptr) {
        return false;
    }

    std::shared_ptr<Ship::Window> window = ctx->GetWindow();
    if (window == nullptr) {
        return false;
    }

    Fast::WindowBackend backend = static_cast<Fast::WindowBackend>(window->GetWindowBackend());
    return backend == Fast::FAST3D_SDL_OPENGL || backend == Fast::FAST3D_SDL_VULKAN ||
           backend == Fast::FAST3D_DXGI_DX11;
}

static void PostFx_ReconcilePasses(bool wantColorAdjust, bool wantGlow, bool wantOutline) {
    int signature = (wantColorAdjust ? 1 : 0) | (wantGlow ? 2 : 0) | (wantOutline ? 4 : 0);
    if (signature == gRegisteredSignature) {
        return;
    }

    if (gColorAdjustHandle >= 0) {
        gfx_unregister_post_pass(gColorAdjustHandle);
        gColorAdjustHandle = -1;
    }
    if (gGlowHandle >= 0) {
        gfx_unregister_post_pass(gGlowHandle);
        gGlowHandle = -1;
    }
    if (gOutlineHandle >= 0) {
        gfx_unregister_post_pass(gOutlineHandle);
        gOutlineHandle = -1;
    }

    // Order matters: grade -> glow -> outline, so the ink sits on top.
    if (wantColorAdjust) {
        gColorAdjustHandle = gfx_register_post_pass(POSTFX_COLOR_ADJUST_SHADER);
    }
    if (wantGlow) {
        gGlowHandle = gfx_register_post_pass(POSTFX_GLOW_SHADER);
    }
    if (wantOutline) {
        gOutlineHandle = gfx_register_post_pass(POSTFX_OUTLINE_SHADER);
    }

    gRegisteredSignature = signature;
}

static void PostFx_PushUniforms() {
    if (gColorAdjustHandle >= 0) {
        Color_RGBA8 tint = CVarGetColor(POSTFX_TINT_COLOR_CVAR, { 255, 255, 255, 255 });

        float grade[4] = {
            CVarGetFloat(POSTFX_EXPOSURE_CVAR, 1.05f),
            CVarGetFloat(POSTFX_CONTRAST_CVAR, 1.05f),
            CVarGetFloat(POSTFX_SATURATION_CVAR, 1.1f),
            CVarGetFloat(POSTFX_TINTMIX_CVAR, 0.0f),
        };
        float tintReg[4] = {
            tint.r / 255.0f,
            tint.g / 255.0f,
            tint.b / 255.0f,
            CVarGetInteger(POSTFX_TONEMAP_CVAR, 1) ? 1.0f : 0.0f,
        };

        gfx_set_custom_uniform(2, grade);
        gfx_set_custom_uniform(3, tintReg);
    }

    if (gGlowHandle >= 0) {
        float glow[4] = {
            CVarGetFloat(POSTFX_GLOW_THRESH_CVAR, 0.8f),
            CVarGetFloat(POSTFX_GLOW_INT_CVAR, 0.4f),
            CVarGetFloat(POSTFX_GLOW_RADIUS_CVAR, 2.0f),
            1.0f,
        };

        gfx_set_custom_uniform(4, glow);
    }

    if (gOutlineHandle >= 0) {
        float outline[4] = {
            CVarGetFloat(POSTFX_OUTLINE_THRESH_CVAR, 0.15f),
            CVarGetFloat(POSTFX_OUTLINE_STRENGTH_CVAR, 0.9f),
            0.0f,
            0.0f,
        };

        gfx_set_custom_uniform(5, outline);
    }
}

static void PostFx_FrameUpdate() {
    bool backendOk = PostFx_BackendSupported();
    bool master = backendOk && CVarGetInteger(POSTFX_MASTER_CVAR, 0) != 0;

    bool wantColorAdjust = master;
    bool wantGlow = master && CVarGetInteger(POSTFX_GLOW_CVAR, 1) != 0;
    bool wantOutline = master && CVarGetInteger(POSTFX_OUTLINE_CVAR, 0) != 0;

    PostFx_ReconcilePasses(wantColorAdjust, wantGlow, wantOutline);
    PostFx_PushUniforms();
}

void register_postfx_hooks(void) {
    REGISTER_LISTENER(GameFrameUpdate, EVENT_PRIORITY_NORMAL, [](IEvent*) { PostFx_FrameUpdate(); });
}
