/**
 * PaperBoat area -> shader lighting translation layer.
 *
 * The generated per-area preset table (src/area_lighting_presets.c) describes
 * every map's mood with the game's RDP-style shading numbers: light colours are
 * 0..255, ambientPower 0..10 and hemisphere intensity 0..1, all meant to be added
 * to a base colour. The per-fragment lighting in the engine shader works in a
 * completely different space where 1.0 is neutral (see paperboatFragmentLight in
 * fast/shaders/directx/default.shader.hlsl). This file bridges the two: once per
 * frame it resolves the current area's authored preset and writes the fragment
 * lighting uniforms, so each map gets lighting tuned for its own art.
 *
 * Areas that have no authored preset fall back to a palette-derived ambient
 * level, so the "every area is covered" guarantee still holds.
 *
 * Registers written (see PostFx.cpp for the full layout):
 *   12 = (sunDir.xyz, sunIntensity)
 *   13 = (skyR, skyG, skyB, skyLevel)
 *   14 = (groundR, groundG, groundB, groundLevel)
 *   16 = (rimIntensity, rimPower, rimBias, unused)
 *   21 = (sunR, sunG, sunB, ambientLevel)
 *   22 = (exposure, unused, debugView, unused)
 * Enable/strength/wrap/normal-sign (reg 15) and relight/tonemap (17/18) stay under
 * the user's existing CVars.
 */

#include "common.h"
#include "map.h"
#include "sprite/sprite_enhanced_shading.h"
#include "port/ui/cvar_prefixes.h"
#include "port/enhancements/graphics/ShadingPresets.h"

#include <libultraship/bridge/consolevariablebridge.h>

#include <cmath>

extern "C" {
void gfx_set_custom_uniform(uint8_t idx, const float values[4]);

// Derived in BackgroundPatches.cpp from the current map's background palette.
void port_get_area_ambient(float* outR, float* outG, float* outB);

// World map configuration (world.c); id is the map name like "kmr_00".
extern MapConfig* gMapConfig;
}

static constexpr const char* kAreaLightingCvar = CVAR_ENHANCED_SHADING("AreaLighting");
static constexpr const char* kSunWarmthCvar = CVAR_ENHANCED_SHADING("SunWarmth");
static constexpr const char* kSunIntensityCvar = CVAR_ENHANCED_SHADING("SunIntensity");
static constexpr const char* kSunDebugCvar = CVAR_ENHANCED_SHADING("SunDebug");

static float pbaClampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Turns a shading colour (0..255) into a near-white hue tint (0.6..1.4) by
// dividing out its own luminance, so a dark authored colour still reads as a
// direction (warm/cool) rather than a black wash in the fragment-light space.
static void pbaTint(const Color_RGB8* c, float out[3]) {
    const float r = c->r / 255.0f;
    const float g = c->g / 255.0f;
    const float b = c->b / 255.0f;
    const float luma = 0.299f * r + 0.587f * g + 0.114f * b;

    if (luma < 0.02f) {
        out[0] = out[1] = out[2] = 1.0f;
        return;
    }

    const float inv = 1.0f / luma;
    out[0] = pbaClampf(r * inv, 0.6f, 1.4f);
    out[1] = pbaClampf(g * inv, 0.6f, 1.4f);
    out[2] = pbaClampf(b * inv, 0.6f, 1.4f);
}

static const EnhancedLightSource* pbaPickSun(const EnhancedShadingProfile* profile) {
    if (profile == NULL) {
        return NULL;
    }

    u8 count = profile->lightCount;
    if (count > ENHANCED_SHADING_MAX_LIGHTS) {
        count = ENHANCED_SHADING_MAX_LIGHTS;
    }

    for (u8 i = 0; i < count; i++) {
        const EnhancedLightSource* light = &profile->lights[i];
        if (light->flags != 0 && light->type == ENHANCED_LIGHT_DIRECTIONAL) {
            return light;
        }
    }

    return NULL;
}

static void pbaApplyAuthored(const EnhancedShadingProfile* profile) {
    const EnhancedLightSource* sun = pbaPickSun(profile);

    float sunDirX = 0.0f;
    float sunDirY = 1.0f;
    float sunDirZ = 0.0f;
    float sunIntensity = CVarGetFloat(kSunIntensityCvar, 0.45f);
    float sunColorR = 1.0f;
    float sunColorG = 1.0f;
    float sunColorB = 1.0f;

    if (sun != NULL) {
        // EnhancedLightSource.dir is the direction the light travels; the shader
        // wants the direction toward the light, so negate and normalise.
        const float dx = -sun->dir.x;
        const float dy = -sun->dir.y;
        const float dz = -sun->dir.z;
        const float len = sqrtf(dx * dx + dy * dy + dz * dz);

        if (len > 1e-5f) {
            sunDirX = dx / len;
            sunDirY = dy / len;
            sunDirZ = dz / len;
        }
        sunIntensity = pbaClampf(sun->intensity, 0.0f, 2.0f);
        if (sun->color.r != 0 || sun->color.g != 0 || sun->color.b != 0) {
            sunColorB = pbaClampf(sun->color.b / 255.0f, 0.1f, 1.5f);
            sunColorG = pbaClampf(sun->color.g / 255.0f, 0.1f, 1.5f);
            sunColorR = pbaClampf(sun->color.r / 255.0f, 0.1f, 1.5f);
        }
    }

    float skyTint[3];
    float groundTint[3];
    pbaTint(&profile->hemisphereSkyColor, skyTint);
    pbaTint(&profile->hemisphereGroundColor, groundTint);

    // Level curves chosen to land in the shader's known-good range (sky ~0.75,
    // ground ~0.4, ambient ~0.6) for the values the generator emits (hemisphere
    // intensity 0.2, ambientPower 5).
    const float skyLevel = pbaClampf(0.55f + profile->hemisphereIntensity, 0.45f, 1.10f);
    const float groundLevel = pbaClampf(skyLevel * 0.5f, 0.20f, 0.70f);
    const float ambientLevel = pbaClampf(0.40f + (float) profile->ambientPower * 0.04f, 0.30f, 1.15f);

    float reg12[4] = { sunDirX, sunDirY, sunDirZ, sunIntensity };
    float reg13[4] = { skyTint[0], skyTint[1], skyTint[2], skyLevel };
    float reg14[4] = { groundTint[0], groundTint[1], groundTint[2], groundLevel };
    float reg16[4] = { 0.0f, 3.0f, 0.6f, 0.0f };
    float reg21[4] = { sunColorR, sunColorG, sunColorB, ambientLevel };
    float reg22[4] = { profile->exposure > 0.001f ? pbaClampf(profile->exposure, 0.1f, 2.0f) : 1.0f, 0.0f,
                       (float) CVarGetInteger(kSunDebugCvar, 0), 0.0f };

    if ((profile->flags & ENHANCED_SHADING_FLAG_RIM_LIGHTING) && profile->rimLightIntensity > 0.0f) {
        reg16[0] = pbaClampf(profile->rimLightIntensity, 0.0f, 4.0f);
    }

    gfx_set_custom_uniform(12, reg12);
    gfx_set_custom_uniform(13, reg13);
    gfx_set_custom_uniform(14, reg14);
    gfx_set_custom_uniform(16, reg16);
    gfx_set_custom_uniform(21, reg21);
    gfx_set_custom_uniform(22, reg22);
}

// Areas without an authored preset: keep the palette-derived hemisphere that
// PostFx already pushes, but set the ambient level from the palette brightness so
// dark maps (caves, night) get a deeper shadow while bright ones lift. The sun
// colour mirrors the warm-sun formula so the register stays self-consistent.
static void pbaApplyAuto(void) {
    float r = 0.5f;
    float g = 0.5f;
    float b = 0.5f;
    port_get_area_ambient(&r, &g, &b);

    const float luma = pbaClampf(0.299f * r + 0.587f * g + 0.114f * b, 0.0f, 1.0f);
    const float ambientLevel = pbaClampf(0.40f + luma * 0.70f, 0.25f, 1.15f);
    const float warmth = pbaClampf(CVarGetFloat(kSunWarmthCvar, 0.45f), 0.0f, 1.0f);

    float reg21[4] = { 1.0f, 1.0f - 0.16f * warmth, 1.0f - 0.40f * warmth, ambientLevel };
    gfx_set_custom_uniform(21, reg21);
}

extern "C" void EnhancedShadingBridge_PushLightingUniforms(void) {
    if (gGameStatusPtr == NULL) {
        return;
    }
    if (CVarGetInteger(kAreaLightingCvar, 1) == 0) {
        return;
    }

    // An explicitly selected custom preset takes priority over the per-area table,
    // so a shared "look" can be applied across every map.
    EnhancedShadingProfile customProfile;
    if (ShadingPresets::GetActiveProfile(&customProfile)) {
        pbaApplyAuthored(&customProfile);
        return;
    }

    EnhancedShadingPreset* preset = NULL;
    if (gMapConfig != NULL && gMapConfig->id != NULL) {
        preset = find_enhanced_shading_preset_by_area(gMapConfig->id, gGameStatusPtr->mapID);
    }

    if (preset != NULL) {
        pbaApplyAuthored(&preset->profile);
    } else {
        pbaApplyAuto();
    }
}

extern "C" const char* EnhancedShadingBridge_CurrentAreaName(void) {
    if (gMapConfig == NULL || gMapConfig->id == NULL) {
        return "";
    }
    return gMapConfig->id;
}

extern "C" b32 EnhancedShadingBridge_CurrentAreaHasPreset(void) {
    if (gGameStatusPtr == NULL || gMapConfig == NULL || gMapConfig->id == NULL) {
        return false;
    }
    return find_enhanced_shading_preset_by_area(gMapConfig->id, gGameStatusPtr->mapID) != NULL;
}
