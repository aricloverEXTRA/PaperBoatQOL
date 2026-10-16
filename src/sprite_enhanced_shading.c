#include "common.h"
#include "sprite.h"
#include "string.h"
#include "port/patches/Patches.h"
#include "port/hooks/Events.h"
#include "port/ui/cvar_prefixes.h"
#include "sprite/sprite_enhanced_shading.h"

extern void register_all_area_presets(void);

static DynamicLight sDynamicLights[ENHANCED_SHADING_MAX_DYNAMIC_LIGHTS];
static u8 sDynamicLightCount = 0;

EnhancedShadingProfile* gEnhancedShadingProfile;
EnhancedShadingProfile* gAuxEnhancedShadingProfile;

BSS EnhancedShadingProfile gWorldEnhancedShadingProfile;
BSS EnhancedShadingProfile gBattleEnhancedShadingProfile;
BSS EnhancedShadingProfile gWorldAuxEnhancedShadingProfile;
BSS EnhancedShadingProfile gBattleAuxEnhancedShadingProfile;

BSS EnhancedShadingPreset gEnhancedShadingPresets[ENHANCED_SHADING_MAX_PRESETS];
BSS s32 gEnhancedShadingPresetCount = 0;

static b32 sFallbackToVanilla = true;
static b32 sEnhancedShadingInitialized = false;

void init_enhanced_shading_data(void) {
    if (gGameStatusPtr->context == CONTEXT_WORLD) {
        gEnhancedShadingProfile = &gWorldEnhancedShadingProfile;
        gAuxEnhancedShadingProfile = &gWorldAuxEnhancedShadingProfile;
    } else {
        gEnhancedShadingProfile = &gBattleEnhancedShadingProfile;
        gAuxEnhancedShadingProfile = &gBattleAuxEnhancedShadingProfile;
    }
    
    if (!sEnhancedShadingInitialized) {
        clear_enhanced_shading_data();
        register_all_area_presets();
        sEnhancedShadingInitialized = true;
    }
}

void clear_enhanced_shading_data(void) {
    s32 i, j;
    
    for (j = 0; j < 2; j++) {
        EnhancedShadingProfile* profile = (j == 0) ? &gWorldEnhancedShadingProfile : &gBattleEnhancedShadingProfile;
        EnhancedShadingProfile* auxProfile = (j == 0) ? &gWorldAuxEnhancedShadingProfile : &gBattleAuxEnhancedShadingProfile;
        
        profile->flags = 0;
        profile->presetType = ENHANCED_SHADING_PRESET_NONE;
        profile->lightCount = 0;
        profile->ambientColor.r = profile->ambientColor.g = profile->ambientColor.b = 50;
        profile->ambientPower = 10;
        profile->hemisphereSkyColor.r = profile->hemisphereSkyColor.g = profile->hemisphereSkyColor.b = 180;
        profile->hemisphereGroundColor.r = profile->hemisphereGroundColor.g = profile->hemisphereGroundColor.b = 60;
        profile->hemisphereIntensity = 0.5f;
        profile->rimLightIntensity = 0.0f;
        profile->rimLightColor.r = profile->rimLightColor.g = profile->rimLightColor.b = 255;
        profile->subsurfaceStrength = 0.0f;
        profile->subsurfaceColor.r = profile->subsurfaceColor.g = profile->subsurfaceColor.b = 255;
        profile->aoIntensity = 0.0f;
        profile->aoRadius = 1.0f;
        profile->temporalBlendFactor = 0.9f;
        
        // Depth of Field
        profile->dofFocalDistance = 500.0f;
        profile->dofFocalRange = 100.0f;
        profile->dofMaxBlur = 0.5f;
        profile->dofNearBlurMax = 0.3f;
        profile->dofSkyboxExempt = 1;
        
        // Shadows
        profile->shadowDistance = 1000.0f;
        profile->shadowBias = 0.005f;
        profile->shadowNormalBias = 0.02f;
        profile->shadowResolution = 2;
        profile->shadowPCF = 1;
        
        // Bloom
        profile->bloomThreshold = 1.0f;
        profile->bloomIntensity = 0.3f;
        profile->bloomRadius = 1.0f;
        
        // Tone Mapping
        profile->exposure = 1.0f;
        profile->whitePoint = 1.0f;
        profile->toneMappingEnabled = 0;
        
        auxProfile->flags = 0;
        auxProfile->presetType = ENHANCED_SHADING_PRESET_NONE;
        auxProfile->lightCount = 0;
        auxProfile->ambientColor.r = auxProfile->ambientColor.g = auxProfile->ambientColor.b = 50;
        auxProfile->ambientPower = 20;
        auxProfile->hemisphereSkyColor.r = auxProfile->hemisphereSkyColor.g = auxProfile->hemisphereSkyColor.b = 180;
        auxProfile->hemisphereGroundColor.r = auxProfile->hemisphereGroundColor.g = auxProfile->hemisphereGroundColor.b = 60;
        auxProfile->hemisphereIntensity = 0.5f;
        auxProfile->rimLightIntensity = 0.0f;
        auxProfile->rimLightColor.r = auxProfile->rimLightColor.g = auxProfile->rimLightColor.b = 255;
        auxProfile->subsurfaceStrength = 0.0f;
        auxProfile->subsurfaceColor.r = auxProfile->subsurfaceColor.g = auxProfile->subsurfaceColor.b = 255;
        auxProfile->aoIntensity = 0.0f;
        auxProfile->aoRadius = 1.0f;
        auxProfile->temporalBlendFactor = 0.9f;
        
        for (i = 0; i < ENHANCED_SHADING_MAX_LIGHTS; i++) {
            profile->lights[i].type = ENHANCED_LIGHT_DIRECTIONAL;
            profile->lights[i].flags = 0;
            profile->lights[i].pos.x = profile->lights[i].pos.y = profile->lights[i].pos.z = 0.0f;
            profile->lights[i].dir.x = profile->lights[i].dir.y = profile->lights[i].dir.z = 0.0f;
            profile->lights[i].color.r = profile->lights[i].color.g = profile->lights[i].color.b = 255;
            profile->lights[i].intensity = 1.0f;
            profile->lights[i].falloff = 1.0f;
            profile->lights[i].innerAngle = 30.0f;
            profile->lights[i].outerAngle = 45.0f;
            profile->lights[i].radius = 100.0f;
            
            auxProfile->lights[i].type = ENHANCED_LIGHT_DIRECTIONAL;
            auxProfile->lights[i].flags = 0;
            auxProfile->lights[i].pos.x = auxProfile->lights[i].pos.y = auxProfile->lights[i].pos.z = 0.0f;
            auxProfile->lights[i].dir.x = auxProfile->lights[i].dir.y = auxProfile->lights[i].dir.z = 0.0f;
            auxProfile->lights[i].color.r = auxProfile->lights[i].color.g = auxProfile->lights[i].color.b = 255;
            auxProfile->lights[i].intensity = 1.0f;
            auxProfile->lights[i].falloff = 1.0f;
            auxProfile->lights[i].innerAngle = 30.0f;
            auxProfile->lights[i].outerAngle = 45.0f;
            auxProfile->lights[i].radius = 100.0f;
        }
    }
}

void apply_enhanced_shading_preset(EnhancedShadingPresetType presetType) {
    init_enhanced_shading_data();
    
    for (s32 i = 0; i < gEnhancedShadingPresetCount; i++) {
        if (gEnhancedShadingPresets[i].type == presetType) {
            apply_enhanced_shading_profile(&gEnhancedShadingPresets[i].profile);
            return;
        }
    }
    
    clear_enhanced_shading_data();
}

void apply_enhanced_shading_profile(const EnhancedShadingProfile* profile) {
    if (profile == NULL || gEnhancedShadingProfile == NULL) {
        return;
    }
    
    gEnhancedShadingProfile->flags = profile->flags | ENHANCED_SHADING_FLAG_ENABLED;
    gEnhancedShadingProfile->presetType = profile->presetType;
    gEnhancedShadingProfile->lightCount = profile->lightCount;
    gEnhancedShadingProfile->ambientColor = profile->ambientColor;
    gEnhancedShadingProfile->ambientPower = profile->ambientPower;
    gEnhancedShadingProfile->hemisphereSkyColor = profile->hemisphereSkyColor;
    gEnhancedShadingProfile->hemisphereGroundColor = profile->hemisphereGroundColor;
    gEnhancedShadingProfile->hemisphereIntensity = profile->hemisphereIntensity;
    gEnhancedShadingProfile->rimLightIntensity = profile->rimLightIntensity;
    gEnhancedShadingProfile->rimLightColor = profile->rimLightColor;
    gEnhancedShadingProfile->subsurfaceStrength = profile->subsurfaceStrength;
    gEnhancedShadingProfile->subsurfaceColor = profile->subsurfaceColor;
    gEnhancedShadingProfile->aoIntensity = profile->aoIntensity;
    gEnhancedShadingProfile->aoRadius = profile->aoRadius;
    gEnhancedShadingProfile->temporalBlendFactor = profile->temporalBlendFactor;
    
    for (s32 i = 0; i < ENHANCED_SHADING_MAX_LIGHTS; i++) {
        gEnhancedShadingProfile->lights[i] = profile->lights[i];
    }
}

void enhanced_shading_set_light(u32 idx, const EnhancedLightSource* light) {
    if (idx < ENHANCED_SHADING_MAX_LIGHTS && gEnhancedShadingProfile != NULL && light != NULL) {
        gEnhancedShadingProfile->lights[idx] = *light;
        if (idx >= gEnhancedShadingProfile->lightCount) {
            gEnhancedShadingProfile->lightCount = idx + 1;
        }
        gEnhancedShadingProfile->flags |= ENHANCED_SHADING_FLAG_ENABLED | ENHANCED_SHADING_FLAG_USE_CUSTOM;
    }
}

EnhancedShadingPreset* find_enhanced_shading_preset_by_area(const char* areaName, s32 mapID) {
    // Try exact name match first (mapConfig->id format like "kmr_00")
    for (s32 i = 0; i < gEnhancedShadingPresetCount; i++) {
        if (strcmp(gEnhancedShadingPresets[i].name, areaName) == 0) {
            return &gEnhancedShadingPresets[i];
        }
    }
    // Fallback to numeric ID match
    for (s32 i = 0; i < gEnhancedShadingPresetCount; i++) {
        if (gEnhancedShadingPresets[i].id == (u32)mapID) {
            return &gEnhancedShadingPresets[i];
        }
    }
    return NULL;
}

void register_enhanced_shading_preset(const EnhancedShadingPreset* preset) {
    if (preset == NULL || gEnhancedShadingPresetCount >= ENHANCED_SHADING_MAX_PRESETS) {
        return;
    }
    
    gEnhancedShadingPresets[gEnhancedShadingPresetCount] = *preset;
    gEnhancedShadingPresetCount++;
}

void enhanced_shading_load_area_defaults(const char* areaName, s32 mapID, s32 entryID) {
    init_enhanced_shading_data();
    
    b32 enhancedEnabled = CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
    if (!enhancedEnabled) {
        clear_enhanced_shading_data();
        return;
    }
    
    int mode = CVarGetInteger(CVAR_ENHANCED_SHADING("Mode"), 1);
    if (mode == 0) {
        clear_enhanced_shading_data();
        return;
    }
    
    b32 fallback = CVarGetInteger(CVAR_ENHANCED_SHADING("FallbackToVanilla"), 1);
    enhanced_shading_set_fallback_to_vanilla(fallback);
    
    enhanced_shading_clear_dynamic_lights();
    
    EnhancedShadingPreset* preset = find_enhanced_shading_preset_by_area(areaName, mapID);
    if (preset != NULL) {
        apply_enhanced_shading_profile(&preset->profile);
        
        // Register default dynamic lights for this area
        if (CVarGetInteger(CVAR_ENHANCED_SHADING("DynamicLights"), 1)) {
            enhanced_shading_register_default_dynamic_lights(areaName, mapID);
        }
    } else if (sFallbackToVanilla) {
        clear_enhanced_shading_data();
    }
}

b32 enhanced_shading_is_area_supported(const char* areaName, s32 mapID) {
    return find_enhanced_shading_preset_by_area(areaName, mapID) != NULL;
}

void enhanced_shading_set_fallback_to_vanilla(b32 enabled) {
    sFallbackToVanilla = enabled;
}

b32 enhanced_shading_get_fallback_to_vanilla(void) {
    return sFallbackToVanilla;
}

s32 enhanced_shading_add_dynamic_light(const DynamicLight* light) {
    if (light == NULL || sDynamicLightCount >= ENHANCED_SHADING_MAX_DYNAMIC_LIGHTS) {
        return -1;
    }
    
    s32 index = sDynamicLightCount;
    sDynamicLights[index] = *light;
    sDynamicLightCount++;
    
    if (gEnhancedShadingProfile != NULL) {
        gEnhancedShadingProfile->dynamicLights[index] = *light;
        gEnhancedShadingProfile->dynamicLightCount = sDynamicLightCount;
        gEnhancedShadingProfile->flags |= ENHANCED_SHADING_FLAG_DYNAMIC_LIGHTS;
    }
    
    return index;
}

void enhanced_shading_remove_dynamic_light(s32 lightIndex) {
    if (lightIndex < 0 || lightIndex >= sDynamicLightCount) {
        return;
    }
    
    for (s32 i = lightIndex; i < sDynamicLightCount - 1; i++) {
        sDynamicLights[i] = sDynamicLights[i + 1];
    }
    sDynamicLightCount--;
    
    if (gEnhancedShadingProfile != NULL) {
        for (s32 i = lightIndex; i < sDynamicLightCount; i++) {
            gEnhancedShadingProfile->dynamicLights[i] = sDynamicLights[i];
        }
        gEnhancedShadingProfile->dynamicLightCount = sDynamicLightCount;
        if (sDynamicLightCount == 0) {
            gEnhancedShadingProfile->flags &= ~ENHANCED_SHADING_FLAG_DYNAMIC_LIGHTS;
        }
    }
}

void enhanced_shading_update_dynamic_light(s32 lightIndex, const DynamicLight* light) {
    if (lightIndex < 0 || lightIndex >= sDynamicLightCount || light == NULL) {
        return;
    }
    
    sDynamicLights[lightIndex] = *light;
    
    if (gEnhancedShadingProfile != NULL) {
        gEnhancedShadingProfile->dynamicLights[lightIndex] = *light;
    }
}

DynamicLight* enhanced_shading_get_dynamic_light(s32 lightIndex) {
    if (lightIndex < 0 || lightIndex >= sDynamicLightCount) {
        return NULL;
    }
    return &sDynamicLights[lightIndex];
}

void enhanced_shading_clear_dynamic_lights(void) {
    sDynamicLightCount = 0;
    if (gEnhancedShadingProfile != NULL) {
        gEnhancedShadingProfile->dynamicLightCount = 0;
        gEnhancedShadingProfile->flags &= ~ENHANCED_SHADING_FLAG_DYNAMIC_LIGHTS;
    }
}

void enhanced_shading_register_default_dynamic_lights(const char* areaName, s32 mapID) {
    // Register default torches/lanterns for known indoor areas
    // This is a simple implementation - in practice, these would be loaded from area data
    
    if (strstr(areaName, "hos_") != NULL || strstr(areaName, "jan_") != NULL || 
        strstr(areaName, "tik_") != NULL || strstr(areaName, "sam_") != NULL) {
        // Toad Town houses - add a few lanterns
        DynamicLight lantern1 = {
            .type = DYNAMIC_LIGHT_LANTERN,
            .flags = 1,
            .pos = { -200.0f, 50.0f, -100.0f },
            .color = { 255, 180, 100 },
            .intensity = 0.8f,
            .radius = 300.0f,
            .flickerSpeed = 0.0f,
            .flickerAmount = 0.0f,
            .ownerEntityID = -1,
        };
        DynamicLight lantern2 = {
            .type = DYNAMIC_LIGHT_LANTERN,
            .flags = 1,
            .pos = { 200.0f, 50.0f, -100.0f },
            .color = { 255, 180, 100 },
            .intensity = 0.8f,
            .radius = 300.0f,
            .flickerSpeed = 0.0f,
            .flickerAmount = 0.0f,
            .ownerEntityID = -1,
        };
        enhanced_shading_add_dynamic_light(&lantern1);
        enhanced_shading_add_dynamic_light(&lantern2);
    } else if (strstr(areaName, "dgb_") != NULL) {
        // Tubba Blubba's Manor - add torches
        DynamicLight torch1 = {
            .type = DYNAMIC_LIGHT_TORCH,
            .flags = 1,
            .pos = { -150.0f, 80.0f, -50.0f },
            .color = { 255, 120, 40 },
            .intensity = 1.0f,
            .radius = 400.0f,
            .flickerSpeed = 15.0f,
            .flickerAmount = 0.3f,
            .ownerEntityID = -1,
        };
        DynamicLight torch2 = {
            .type = DYNAMIC_LIGHT_TORCH,
            .flags = 1,
            .pos = { 150.0f, 80.0f, -50.0f },
            .color = { 255, 120, 40 },
            .intensity = 1.0f,
            .radius = 400.0f,
            .flickerSpeed = 13.0f,
            .flickerAmount = 0.3f,
            .ownerEntityID = -1,
        };
        enhanced_shading_add_dynamic_light(&torch1);
        enhanced_shading_add_dynamic_light(&torch2);
    } else if (strstr(areaName, "osr_") != NULL) {
        // Boo's Mansion - add candles
        DynamicLight candle1 = {
            .type = DYNAMIC_LIGHT_CANDLE,
            .flags = 1,
            .pos = { -100.0f, 40.0f, -80.0f },
            .color = { 255, 200, 150 },
            .intensity = 0.6f,
            .radius = 200.0f,
            .flickerSpeed = 20.0f,
            .flickerAmount = 0.4f,
            .ownerEntityID = -1,
        };
        DynamicLight candle2 = {
            .type = DYNAMIC_LIGHT_CANDLE,
            .flags = 1,
            .pos = { 100.0f, 40.0f, -80.0f },
            .color = { 255, 200, 150 },
            .intensity = 0.6f,
            .radius = 200.0f,
            .flickerSpeed = 18.0f,
            .flickerAmount = 0.4f,
            .ownerEntityID = -1,
        };
        enhanced_shading_add_dynamic_light(&candle1);
        enhanced_shading_add_dynamic_light(&candle2);
    } else if (strstr(areaName, "kkj_") != NULL || strstr(areaName, "kpa_") != NULL) {
        // Koopa Bros Fortress - add torches
        DynamicLight torch = {
            .type = DYNAMIC_LIGHT_TORCH,
            .flags = 1,
            .pos = { 0.0f, 100.0f, 0.0f },
            .color = { 255, 100, 30 },
            .intensity = 1.2f,
            .radius = 500.0f,
            .flickerSpeed = 12.0f,
            .flickerAmount = 0.35f,
            .ownerEntityID = -1,
        };
        enhanced_shading_add_dynamic_light(&torch);
    } else if (strstr(areaName, "kzn_") != NULL || strstr(areaName, "iwa_") != NULL) {
        // Mt. Lavalava - add lava glow lights
        DynamicLight lavaGlow = {
            .type = DYNAMIC_LIGHT_FIRE,
            .flags = 1,
            .pos = { 0.0f, 50.0f, 0.0f },
            .color = { 255, 80, 20 },
            .intensity = 1.5f,
            .radius = 600.0f,
            .flickerSpeed = 10.0f,
            .flickerAmount = 0.2f,
            .ownerEntityID = -1,
        };
        enhanced_shading_add_dynamic_light(&lavaGlow);
    }
}

// ===== Platform-aware graphics preset limits =====
//
// Mobile/console/web targets get capped so devices don't overheat or
// drop frames. Desktop (Windows/Linux/macOS) is unrestricted.

s32 enhanced_shading_get_platform_max_preset(void) {
#if defined(__EMSCRIPTEN__)
    // Web builds use WASM + WebGL2 and lack WebGPU fallbacks, so cap at Low.
    return ENHANCED_GRAPHICS_PRESET_LOW;
#elif defined(__ANDROID__) || defined(__IOS__) || defined(__SWITCH__) || defined(__WIIU__)
    // Battery/thermals: cap at Medium (Switch-like optimization for mobile).
    return ENHANCED_GRAPHICS_PRESET_MEDIUM;
#else
    // Windows / Linux / macOS: no cap (Custom allowed).
    return ENHANCED_GRAPHICS_PRESET_CUSTOM;
#endif
}

s32 enhanced_shading_get_effective_preset(void) {
    s32 preset = CVarGetInteger(CVAR_ENHANCED_SHADING("GraphicsPreset"), ENHANCED_GRAPHICS_PRESET_HIGH);
    s32 maxPreset = enhanced_shading_get_platform_max_preset();

    if (preset < ENHANCED_GRAPHICS_PRESET_LOW) {
        preset = ENHANCED_GRAPHICS_PRESET_LOW;
    }
    if (preset > maxPreset) {
        preset = maxPreset;
    }
    return preset;
}

// Returns TRUE if the given feature flag is enabled by the effective preset.
// Custom preset falls back to the individual CVars handled by the caller.
b32 enhanced_shading_platform_allows_feature(s32 featureFlag) {
    s32 preset = enhanced_shading_get_effective_preset();

    switch (preset) {
        case ENHANCED_GRAPHICS_PRESET_LOW:
            // Baseline: hemisphere shading only.
            return featureFlag == ENHANCED_SHADING_FLAG_HEMISPHERE
                || featureFlag == ENHANCED_SHADING_FLAG_DYNAMIC_LIGHTS
                || featureFlag == ENHANCED_SHADING_FLAG_FALLOFF_SMOOTH;
        case ENHANCED_GRAPHICS_PRESET_MEDIUM:
            // Balanced: adds AO + rim + temporal stability.
            return featureFlag == ENHANCED_SHADING_FLAG_HEMISPHERE
                || featureFlag == ENHANCED_SHADING_FLAG_DYNAMIC_LIGHTS
                || featureFlag == ENHANCED_SHADING_FLAG_FALLOFF_SMOOTH
                || featureFlag == ENHANCED_SHADING_FLAG_AMBIENT_OCCLUSION
                || featureFlag == ENHANCED_SHADING_FLAG_RIM_LIGHTING
                || featureFlag == ENHANCED_SHADING_FLAG_TEMPORAL_STABILITY;
        case ENHANCED_GRAPHICS_PRESET_HIGH:
            // Quality: adds subsurface + DOF.
            return featureFlag == ENHANCED_SHADING_FLAG_HEMISPHERE
                || featureFlag == ENHANCED_SHADING_FLAG_DYNAMIC_LIGHTS
                || featureFlag == ENHANCED_SHADING_FLAG_FALLOFF_SMOOTH
                || featureFlag == ENHANCED_SHADING_FLAG_AMBIENT_OCCLUSION
                || featureFlag == ENHANCED_SHADING_FLAG_RIM_LIGHTING
                || featureFlag == ENHANCED_SHADING_FLAG_TEMPORAL_STABILITY
                || featureFlag == ENHANCED_SHADING_FLAG_SUBSURFACE
                || featureFlag == ENHANCED_SHADING_FLAG_DEPTH_OF_FIELD;
        case ENHANCED_GRAPHICS_PRESET_ULTRA:
            // Everything on.
            return true;
        default:
            return false;
    }
}