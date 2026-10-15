#include "sprite/sprite_enhanced_shading.h"

static EnhancedShadingPreset gAreaPresets[] = {
    {
        .id = 0x00000000,
        .name = "KMR_00_Goomba_Village",
        .type = ENHANCED_SHADING_PRESET_SUNNY_OUTDOOR,
        .profile = {
            .flags = ENHANCED_SHADING_FLAG_ENABLED | ENHANCED_SHADING_FLAG_USE_PRESET | 
                     ENHANCED_SHADING_FLAG_HEMISPHERE | ENHANCED_SHADING_FLAG_RIM_LIGHTING | ENHANCED_SHADING_FLAG_TEMPORAL_STABILITY,
            .presetType = ENHANCED_SHADING_PRESET_SUNNY_OUTDOOR,
            .lightCount = 2,
            .ambientColor = { 80, 85, 90 },
            .ambientPower = 12,
            .hemisphereSkyColor = { 200, 220, 255 },
            .hemisphereGroundColor = { 100, 80, 60 },
            .hemisphereIntensity = 0.6f,
            .rimLightIntensity = 0.3f,
            .rimLightColor = { 255, 240, 200 },
            .subsurfaceStrength = 0.0f,
            .subsurfaceColor = { 255, 200, 150 },
            .aoIntensity = 0.15f,
            .aoRadius = 1.0f,
            .temporalBlendFactor = 0.85f,
            .lights = {
                [0] = {
                    .type = ENHANCED_LIGHT_DIRECTIONAL,
                    .flags = 1,
                    .pos = { 0, 0, 0 },
                    .dir = { -0.3f, -0.8f, -0.5f },
                    .color = { 255, 245, 220 },
                    .intensity = 1.2f,
                    .falloff = 0.0f,
                    .innerAngle = 0.0f,
                    .outerAngle = 0.0f,
                    .radius = 0.0f,
                },
                [1] = {
                    .type = ENHANCED_LIGHT_POINT,
                    .flags = 1,
                    .pos = { 0, 500, 0 },
                    .dir = { 0, 0, 0 },
                    .color = { 255, 200, 150 },
                    .intensity = 0.3f,
                    .falloff = 0.001f,
                    .innerAngle = 0.0f,
                    .outerAngle = 0.0f,
                    .radius = 800.0f,
                },
            },
        },
    },
    {
        .id = 0x00010000,
        .name = "KMR_30_Goomba_Road",
        .type = ENHANCED_SHADING_PRESET_SUNNY_OUTDOOR,
        .profile = {
            .flags = ENHANCED_SHADING_FLAG_ENABLED | ENHANCED_SHADING_FLAG_USE_PRESET | 
                     ENHANCED_SHADING_FLAG_HEMISPHERE | ENHANCED_SHADING_FLAG_RIM_LIGHTING | ENHANCED_SHADING_FLAG_TEMPORAL_STABILITY,
            .presetType = ENHANCED_SHADING_PRESET_SUNNY_OUTDOOR,
            .lightCount = 2,
            .ambientColor = { 75, 80, 85 },
            .ambientPower = 11,
            .hemisphereSkyColor = { 190, 215, 250 },
            .hemisphereGroundColor = { 95, 75, 55 },
            .hemisphereIntensity = 0.55f,
            .rimLightIntensity = 0.35f,
            .rimLightColor = { 255, 235, 195 },
            .subsurfaceStrength = 0.0f,
            .subsurfaceColor = { 255, 195, 145 },
            .aoIntensity = 0.15f,
            .aoRadius = 1.0f,
            .temporalBlendFactor = 0.85f,
            .lights = {
                [0] = {
                    .type = ENHANCED_LIGHT_DIRECTIONAL,
                    .flags = 1,
                    .pos = { 0, 0, 0 },
                    .dir = { -0.4f, -0.75f, -0.5f },
                    .color = { 255, 240, 215 },
                    .intensity = 1.15f,
                    .falloff = 0.0f,
                    .innerAngle = 0.0f,
                    .outerAngle = 0.0f,
                    .radius = 0.0f,
                },
                [1] = {
                    .type = ENHANCED_LIGHT_POINT,
                    .flags = 1,
                    .pos = { 0, 500, 0 },
                    .dir = { 0, 0, 0 },
                    .color = { 255, 195, 145 },
                    .intensity = 0.25f,
                    .falloff = 0.001f,
                    .innerAngle = 0.0f,
                    .outerAngle = 0.0f,
                    .radius = 800.0f,
                },
            },
        },
    },
};

static const s32 gAreaPresetCount = sizeof(gAreaPresets) / sizeof(gAreaPresets[0]);

void register_all_area_presets(void) {
    for (s32 i = 0; i < gAreaPresetCount; i++) {
        register_enhanced_shading_preset(&gAreaPresets[i]);
    }
}