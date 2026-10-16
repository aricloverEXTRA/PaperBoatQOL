#ifndef SPRITE_ENHANCED_SHADING_H
#define SPRITE_ENHANCED_SHADING_H

#include "common.h"
#include "common_structs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ENHANCED_SHADING_MAX_LIGHTS 16
#define ENHANCED_SHADING_MAX_PRESETS 512
#define ENHANCED_SHADING_MAX_DYNAMIC_LIGHTS 64

typedef enum EnhancedShadingPresetType {
    ENHANCED_SHADING_PRESET_NONE = 0,
    ENHANCED_SHADING_PRESET_SUNNY_OUTDOOR,
    ENHANCED_SHADING_PRESET_CLOUDY_OUTDOOR,
    ENHANCED_SHADING_PRESET_UNDERGROUND,
    ENHANCED_SHADING_PRESET_CAVE,
    ENHANCED_SHADING_PRESET_FOREST,
    ENHANCED_SHADING_PRESET_DESERT,
    ENHANCED_SHADING_PRESET_SNOW,
    ENHANCED_SHADING_PRESET_VOLCANO,
    ENHANCED_SHADING_PRESET_INDOOR_WARM,
    ENHANCED_SHADING_PRESET_INDOOR_COOL,
    ENHANCED_SHADING_PRESET_HAUNTED,
    ENHANCED_SHADING_PRESET_UNDERWATER,
    ENHANCED_SHADING_PRESET_SUNSET,
    ENHANCED_SHADING_PRESET_NIGHT,
    ENHANCED_SHADING_PRESET_CUSTOM,
} EnhancedShadingPresetType;

typedef enum EnhancedGraphicsPreset {
    ENHANCED_GRAPHICS_PRESET_LOW = 0,
    ENHANCED_GRAPHICS_PRESET_MEDIUM,
    ENHANCED_GRAPHICS_PRESET_HIGH,
    ENHANCED_GRAPHICS_PRESET_ULTRA,
    ENHANCED_GRAPHICS_PRESET_CUSTOM,
} EnhancedGraphicsPreset;

typedef enum EnhancedShadingFlags {
    ENHANCED_SHADING_FLAG_ENABLED         = (1 << 0),
    ENHANCED_SHADING_FLAG_USE_PRESET      = (1 << 1),
    ENHANCED_SHADING_FLAG_USE_CUSTOM      = (1 << 2),
    ENHANCED_SHADING_FLAG_RIM_LIGHTING    = (1 << 3),
    ENHANCED_SHADING_FLAG_SUBSURFACE      = (1 << 4),
    ENHANCED_SHADING_FLAG_AMBIENT_OCCLUSION = (1 << 5),
    ENHANCED_SHADING_FLAG_FALLOFF_SMOOTH  = (1 << 6),
    ENHANCED_SHADING_FLAG_TEMPORAL_STABILITY = (1 << 7),
    ENHANCED_SHADING_FLAG_DYNAMIC_LIGHTS  = (1 << 8),
    ENHANCED_SHADING_FLAG_HEMISPHERE      = (1 << 9),
    ENHANCED_SHADING_FLAG_DEPTH_OF_FIELD  = (1 << 10),
    ENHANCED_SHADING_FLAG_SHADOWS         = (1 << 11),
    ENHANCED_SHADING_FLAG_BLOOM           = (1 << 12),
    ENHANCED_SHADING_FLAG_TONE_MAPPING    = (1 << 13),
    ENHANCED_SHADING_FLAG_SKYBOX_DOF_EXEMPT = (1 << 14),
} EnhancedShadingFlags;

typedef enum EnhancedLightType {
    ENHANCED_LIGHT_DIRECTIONAL = 0,
    ENHANCED_LIGHT_POINT,
    ENHANCED_LIGHT_SPOT,
    ENHANCED_LIGHT_HEMISPHERE,
} EnhancedLightType;

typedef enum DynamicLightType {
    DYNAMIC_LIGHT_TORCH = 0,
    DYNAMIC_LIGHT_LANTERN,
    DYNAMIC_LIGHT_CANDLE,
    DYNAMIC_LIGHT_MAGIC,
    DYNAMIC_LIGHT_FIRE,
    DYNAMIC_LIGHT_CUSTOM,
} DynamicLightType;

typedef struct DynamicLight {
    DynamicLightType type;
    u8 flags;
    Vec3f pos;
    Color_RGB8 color;
    f32 intensity;
    f32 radius;
    f32 flickerSpeed;
    f32 flickerAmount;
    s32 ownerEntityID;
    u8 unk_padding[4];
} DynamicLight;

typedef struct EnhancedLightSource {
    EnhancedLightType type;
    u8 flags;
    Vec3f pos;
    Vec3f dir;
    Color_RGB8 color;
    f32 intensity;
    f32 falloff;
    f32 innerAngle;
    f32 outerAngle;
    f32 radius;
    u8 unk_2C[4];
} EnhancedLightSource;

typedef struct EnhancedShadingProfile {
    u16 flags;
    u16 presetType;
    EnhancedLightSource lights[ENHANCED_SHADING_MAX_LIGHTS];
    u8 lightCount;
    Color_RGB8 ambientColor;
    u8 ambientPower;
    Color_RGB8 hemisphereSkyColor;
    Color_RGB8 hemisphereGroundColor;
    f32 hemisphereIntensity;
    f32 rimLightIntensity;
    Color_RGB8 rimLightColor;
    f32 subsurfaceStrength;
    Color_RGB8 subsurfaceColor;
    f32 aoIntensity;
    f32 aoRadius;
    f32 temporalBlendFactor;
    
    // Depth of Field
    f32 dofFocalDistance;
    f32 dofFocalRange;
    f32 dofMaxBlur;
    f32 dofNearBlurMax;
    b32 dofSkyboxExempt;
    
    // Shadows
    f32 shadowDistance;
    f32 shadowBias;
    f32 shadowNormalBias;
    u8 shadowResolution;
    b32 shadowPCF;
    
    // Bloom
    f32 bloomThreshold;
    f32 bloomIntensity;
    f32 bloomRadius;
    
    // Tone Mapping
    f32 exposure;
    f32 whitePoint;
    b32 toneMappingEnabled;
    
    DynamicLight dynamicLights[ENHANCED_SHADING_MAX_DYNAMIC_LIGHTS];
    u8 dynamicLightCount;
    u8 unk_padding[6];
} EnhancedShadingProfile;

typedef struct EnhancedShadingPreset {
    u32 id;
    char name[32];
    EnhancedShadingPresetType type;
    EnhancedShadingProfile profile;
} EnhancedShadingPreset;

extern EnhancedShadingProfile* gEnhancedShadingProfile;
extern EnhancedShadingProfile* gAuxEnhancedShadingProfile;
extern EnhancedShadingProfile gWorldEnhancedShadingProfile;
extern EnhancedShadingProfile gBattleEnhancedShadingProfile;
extern EnhancedShadingProfile gWorldAuxEnhancedShadingProfile;
extern EnhancedShadingProfile gBattleAuxEnhancedShadingProfile;

extern EnhancedShadingPreset gEnhancedShadingPresets[ENHANCED_SHADING_MAX_PRESETS];
extern s32 gEnhancedShadingPresetCount;

void init_enhanced_shading_data(void);
void clear_enhanced_shading_data(void);
void apply_enhanced_shading_preset(EnhancedShadingPresetType presetType);
void apply_enhanced_shading_profile(const EnhancedShadingProfile* profile);
void enhanced_shading_set_light(u32 idx, const EnhancedLightSource* light);
void create_enhanced_shading_palette(Matrix4f mtx, s32 uls, s32 ult, s32 lrs, s32 lrt, s32 alpha, s32 otherModeLBits);
EnhancedShadingPreset* find_enhanced_shading_preset_by_area(const char* areaName, s32 mapID);
void register_enhanced_shading_preset(const EnhancedShadingPreset* preset);
void enhanced_shading_load_area_defaults(const char* areaName, s32 mapID, s32 entryID);
b32 enhanced_shading_is_area_supported(const char* areaName, s32 mapID);
void enhanced_shading_set_fallback_to_vanilla(b32 enabled);
b32 enhanced_shading_get_fallback_to_vanilla(void);

// Dynamic light functions
s32 enhanced_shading_add_dynamic_light(const DynamicLight* light);
void enhanced_shading_remove_dynamic_light(s32 lightIndex);
void enhanced_shading_update_dynamic_light(s32 lightIndex, const DynamicLight* light);
DynamicLight* enhanced_shading_get_dynamic_light(s32 lightIndex);
void enhanced_shading_clear_dynamic_lights(void);
void enhanced_shading_register_default_dynamic_lights(const char* areaName, s32 mapID);

// Platform-aware graphics preset limits
s32 enhanced_shading_get_platform_max_preset(void);
s32 enhanced_shading_get_effective_preset(void);
b32 enhanced_shading_platform_allows_feature(s32 featureFlag);

#ifdef __cplusplus
}
#endif

#endif