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
#include "port/hooks/impl/EventSystem.h"
#include "port/ui/cvar_prefixes.h"

#include <string.h>

#include <spdlog/spdlog.h>

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
#define POSTFX_SUNRAYS_CVAR          CVAR_ENHANCED_SHADING("Sunrays")
#define POSTFX_SUNRAYS_X_CVAR        CVAR_ENHANCED_SHADING("SunraysX")
#define POSTFX_SUNRAYS_Y_CVAR        CVAR_ENHANCED_SHADING("SunraysY")
#define POSTFX_SUNRAYS_INTENSITY_CVAR CVAR_ENHANCED_SHADING("SunraysIntensity")
#define POSTFX_SUNRAYS_DECAY_CVAR    CVAR_ENHANCED_SHADING("SunraysDecay")
#define POSTFX_TONEMAP_CVAR      CVAR_ENHANCED_SHADING("ToneMapping")
#define POSTFX_EXPOSURE_CVAR     CVAR_ENHANCED_SHADING("Exposure")
#define POSTFX_CONTRAST_CVAR     CVAR_ENHANCED_SHADING("PostContrast")
#define POSTFX_SATURATION_CVAR   CVAR_ENHANCED_SHADING("PostSaturation")
#define POSTFX_TINTMIX_CVAR      CVAR_ENHANCED_SHADING("PostTintMix")
#define POSTFX_TINT_COLOR_CVAR   CVAR_ENHANCED_SHADING("PostTintColor")
// Diagnostic switch: 0 = off, else a cs_debug.shader mode (see the shader).
#define POSTFX_DEBUG_CVAR        CVAR_ENHANCED_SHADING("DebugPassMode")

#define POSTFX_COLOR_ADJUST_SHADER "shaders/post/cs_color_adjust.shader"
#define POSTFX_GLOW_SHADER         "shaders/post/cs_glow.shader"
#define POSTFX_OUTLINE_SHADER      "shaders/post/cs_outline.shader"
#define POSTFX_SUNRAYS_SHADER      "shaders/post/cs_sunrays.shader"
#define POSTFX_DEBUG_SHADER        "shaders/post/cs_debug.shader"

static int gColorAdjustHandle = -1;
static int gGlowHandle = -1;
static int gOutlineHandle = -1;
static int gSunraysHandle = -1;
static int gDebugHandle = -1;
static int gDebugModeValue = 0;
// Bits: 0 = colour adjust, 1 = sunrays, 2 = glow, 3 = outline; bits 4+ = debug mode.
static int gRegisteredSignature = -1;

// Per-area sun placement for the god-ray pass. Screen-space (0..1) UVs; the
// Y axis is configurable because the post-pass texture orientation can differ
// between backends. Add entries here to give a location its own shafts.
typedef struct {
    const char* mapName;
    float sunX;
    float sunY;
    float intensity;
} SunrayArea;

static const SunrayArea kSunrayAreas[] = {
    { "dro_01", 0.55f, 0.20f, 1.00f }, // Dry Dry Desert
    { "dro_02", 0.58f, 0.16f, 1.00f }, // Dry Dry Ruins (desert temple)
    { "flo_00", 0.50f, 0.14f, 0.70f }, // Flower Fields (open sky)
    { "flo_19", 0.50f, 0.10f, 0.90f }, // Cloudy Climb (beanstalk ascent)
};

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

static void PostFx_ReconcilePasses(bool wantColorAdjust, bool wantSunrays, bool wantGlow, bool wantOutline,
                                   int debugMode) {
    int signature = (wantColorAdjust ? 1 : 0) | (wantSunrays ? 2 : 0) | (wantGlow ? 4 : 0) |
                    (wantOutline ? 8 : 0) | (debugMode << 4);
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
    if (gSunraysHandle >= 0) {
        gfx_unregister_post_pass(gSunraysHandle);
        gSunraysHandle = -1;
    }
    if (gDebugHandle >= 0) {
        gfx_unregister_post_pass(gDebugHandle);
        gDebugHandle = -1;
    }

    gDebugModeValue = debugMode;

    // The diagnostic pass runs alone so it can attribute any visual result to a
    // single, minimal shader (geometry / UV / sampling / presentation).
    if (debugMode > 0) {
        gDebugHandle = gfx_register_post_pass(POSTFX_DEBUG_SHADER);
        gRegisteredSignature = signature;
        SPDLOG_INFO("[PostFx] DEBUG pass registered: handle={} mode={}", gDebugHandle, debugMode);
        return;
    }

    // Order matters: grade -> sunrays -> glow -> outline, so the ink sits on top.
    if (wantColorAdjust) {
        gColorAdjustHandle = gfx_register_post_pass(POSTFX_COLOR_ADJUST_SHADER);
    }
    if (wantSunrays) {
        gSunraysHandle = gfx_register_post_pass(POSTFX_SUNRAYS_SHADER);
    }
    if (wantGlow) {
        gGlowHandle = gfx_register_post_pass(POSTFX_GLOW_SHADER);
    }
    if (wantOutline) {
        gOutlineHandle = gfx_register_post_pass(POSTFX_OUTLINE_SHADER);
    }

    gRegisteredSignature = signature;

    SPDLOG_INFO("[PostFx] passes reconciled: grade={} sunrays={} glow={} outline={}", gColorAdjustHandle,
                gSunraysHandle, gGlowHandle, gOutlineHandle);
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

    if (gDebugHandle >= 0) {
        float debugReg[4] = { (float)gDebugModeValue, 0.0f, 0.0f, 0.0f };
        gfx_set_custom_uniform(7, debugReg);
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

    if (gSunraysHandle >= 0) {
        float sunrays[4] = {
            CVarGetFloat(POSTFX_SUNRAYS_X_CVAR, 0.5f),
            CVarGetFloat(POSTFX_SUNRAYS_Y_CVAR, 0.2f),
            CVarGetFloat(POSTFX_SUNRAYS_INTENSITY_CVAR, 0.6f),
            CVarGetFloat(POSTFX_SUNRAYS_DECAY_CVAR, 0.95f),
        };

        gfx_set_custom_uniform(6, sunrays);
    }
}

// Applies the per-area sun position on map load so each location gets its own
// shaft direction. Falls back to a generic high-sky sun for unknown areas.
static void PostFx_OnMapLoad(IEvent* event) {
    if (event == nullptr) {
        return;
    }

    const OnMapLoad* ev = (const OnMapLoad*)event;
    if (ev == nullptr) {
        return;
    }

    for (s32 i = 0; i < (s32)(sizeof(kSunrayAreas) / sizeof(kSunrayAreas[0])); i++) {
        if (strcmp(kSunrayAreas[i].mapName, ev->mapName) == 0) {
            CVarSetFloat(POSTFX_SUNRAYS_X_CVAR, kSunrayAreas[i].sunX);
            CVarSetFloat(POSTFX_SUNRAYS_Y_CVAR, kSunrayAreas[i].sunY);
            CVarSetFloat(POSTFX_SUNRAYS_INTENSITY_CVAR, kSunrayAreas[i].intensity);
            return;
        }
    }

    CVarSetFloat(POSTFX_SUNRAYS_X_CVAR, 0.5f);
    CVarSetFloat(POSTFX_SUNRAYS_Y_CVAR, 0.2f);
    CVarSetFloat(POSTFX_SUNRAYS_INTENSITY_CVAR, 0.6f);
}

static void PostFx_FrameUpdate() {
    bool backendOk = PostFx_BackendSupported();
    if (!backendOk) {
        static bool warned = false;
        if (!warned) {
            warned = true;
            SPDLOG_WARN("[PostFx] backend not supported for post passes; effects stay off");
        }
        return;
    }

    bool master = CVarGetInteger(POSTFX_MASTER_CVAR, 0) != 0;

    // One-shot report so a playtest log shows what the chain is doing.
    static int lastLoggedMaster = -1;
    if ((int)master != lastLoggedMaster) {
        lastLoggedMaster = (int)master;
        Ship::Context* ctx = Ship::Context::GetRawInstance();
        int backend = (ctx != nullptr && ctx->GetWindow() != nullptr) ? ctx->GetWindow()->GetWindowBackend() : -1;
        SPDLOG_INFO("[PostFx] master={} backend={}", master, backend);
    }

    // Diagnostic pass is independent of the master toggle so it can be used to
    // verify the pipeline even when nothing else is visibly working.
    int debugMode = CVarGetInteger(POSTFX_DEBUG_CVAR, 0);
    if (debugMode > 0) {
        PostFx_ReconcilePasses(false, false, false, false, debugMode);
        PostFx_PushUniforms();
        return;
    }

    bool wantColorAdjust = master;
    bool wantSunrays = master && CVarGetInteger(POSTFX_SUNRAYS_CVAR, 0) != 0;
    bool wantGlow = master && CVarGetInteger(POSTFX_GLOW_CVAR, 1) != 0;
    bool wantOutline = master && CVarGetInteger(POSTFX_OUTLINE_CVAR, 0) != 0;

    PostFx_ReconcilePasses(wantColorAdjust, wantSunrays, wantGlow, wantOutline, 0);
    PostFx_PushUniforms();
}

void register_postfx_hooks(void) {
    REGISTER_LISTENER(GameFrameUpdate, EVENT_PRIORITY_NORMAL, [](IEvent*) { PostFx_FrameUpdate(); });
    REGISTER_LISTENER(OnMapLoad, EVENT_PRIORITY_NORMAL, [](IEvent* event) { PostFx_OnMapLoad(event); });
}
