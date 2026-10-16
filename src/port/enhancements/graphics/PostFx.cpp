/**
 * PaperBoat post-processing effects.
 *
 * Drives the libultraship post-pass chain from PaperBoat CVars:
 *   - a colour-adjust pass (exposure / contrast / saturation / tint / tone mapping)
 *   - a single-pass glow/bloom pass for emissive surfaces such as lava and fire
 *
 * The shipped shaders only have OpenGL and Vulkan variants. Registering a post
 * pass whose shader file is missing for the active backend aborts the engine, so
 * every pass is hard-gated to the OpenGL/Vulkan backends and to the opt-in
 * "PostProcessing" CVar (default off).
 */

#include "port/hooks/Events.h"
#include "port/ui/cvar_prefixes.h"

#include <libultraship/libultraship.h>
#include <libultraship/bridge/consolevariablebridge.h>
#include <fast/Fast3dWindow.h>

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
#define POSTFX_TONEMAP_CVAR      CVAR_ENHANCED_SHADING("ToneMapping")
#define POSTFX_EXPOSURE_CVAR     CVAR_ENHANCED_SHADING("Exposure")
#define POSTFX_CONTRAST_CVAR     CVAR_ENHANCED_SHADING("PostContrast")
#define POSTFX_SATURATION_CVAR   CVAR_ENHANCED_SHADING("PostSaturation")
#define POSTFX_TINTMIX_CVAR      CVAR_ENHANCED_SHADING("PostTintMix")
#define POSTFX_TINT_COLOR_CVAR   CVAR_ENHANCED_SHADING("PostTintColor")

#define POSTFX_COLOR_ADJUST_SHADER "shaders/post/cs_color_adjust.shader"
#define POSTFX_GLOW_SHADER         "shaders/post/cs_glow.shader"

static int gColorAdjustHandle = -1;
static int gGlowHandle = -1;
// Bits: 0 = colour adjust registered, 1 = glow registered.
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
    return backend == Fast::FAST3D_SDL_OPENGL || backend == Fast::FAST3D_SDL_VULKAN;
}

static void PostFx_ReconcilePasses(bool wantColorAdjust, bool wantGlow) {
    int signature = (wantColorAdjust ? 1 : 0) | (wantGlow ? 2 : 0);
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

    if (wantColorAdjust) {
        gColorAdjustHandle = gfx_register_post_pass(POSTFX_COLOR_ADJUST_SHADER);
    }
    if (wantGlow) {
        gGlowHandle = gfx_register_post_pass(POSTFX_GLOW_SHADER);
    }

    gRegisteredSignature = signature;
}

static void PostFx_PushUniforms() {
    if (gColorAdjustHandle >= 0) {
        Color_RGBA8 tint = CVarGetColor(POSTFX_TINT_COLOR_CVAR, { 255, 255, 255, 255 });

        float grade[4] = {
            CVarGetFloat(POSTFX_EXPOSURE_CVAR, 1.0f),
            CVarGetFloat(POSTFX_CONTRAST_CVAR, 1.0f),
            CVarGetFloat(POSTFX_SATURATION_CVAR, 1.0f),
            CVarGetFloat(POSTFX_TINTMIX_CVAR, 0.0f),
        };
        float tintReg[4] = {
            tint.r / 255.0f,
            tint.g / 255.0f,
            tint.b / 255.0f,
            CVarGetInteger(POSTFX_TONEMAP_CVAR, 0) ? 1.0f : 0.0f,
        };

        gfx_set_custom_uniform(2, grade);
        gfx_set_custom_uniform(3, tintReg);
    }

    if (gGlowHandle >= 0) {
        float glow[4] = {
            CVarGetFloat(POSTFX_GLOW_THRESH_CVAR, 1.0f),
            CVarGetFloat(POSTFX_GLOW_INT_CVAR, 0.3f),
            CVarGetFloat(POSTFX_GLOW_RADIUS_CVAR, 2.0f),
            1.0f,
        };

        gfx_set_custom_uniform(4, glow);
    }
}

static void PostFx_FrameUpdate() {
    bool backendOk = PostFx_BackendSupported();
    bool master = backendOk && CVarGetInteger(POSTFX_MASTER_CVAR, 0) != 0;

    bool wantColorAdjust = master;
    bool wantGlow = master && CVarGetInteger(POSTFX_GLOW_CVAR, 0) != 0;

    PostFx_ReconcilePasses(wantColorAdjust, wantGlow);
    PostFx_PushUniforms();
}

void register_postfx_hooks(void) {
    REGISTER_LISTENER(GameFrameUpdate, EVENT_PRIORITY_NORMAL, [](IEvent*) { PostFx_FrameUpdate(); });
}
