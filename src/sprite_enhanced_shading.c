#include "common.h"
#include "sprite.h"
#include "port/patches/Patches.h"
#include "port/save/SaveManager.h"
#include "port/hooks/Events.h"
#include "port/ui/cvar_prefixes.h"
#include "sprite/sprite_enhanced_shading.h"

extern void register_all_area_presets(void);

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
    
    int mode = CVarGetInteger(CVAR_ENHANCED_SHADING("Mode"), 0);
    if (mode == 0) {
        clear_enhanced_shading_data();
        return;
    }
    
    b32 fallback = CVarGetInteger(CVAR_ENHANCED_SHADING("FallbackToVanilla"), 1);
    enhanced_shading_set_fallback_to_vanilla(fallback);
    
    EnhancedShadingPreset* preset = find_enhanced_shading_preset_by_area(areaName, mapID);
    if (preset != NULL) {
        apply_enhanced_shading_profile(&preset->profile);
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