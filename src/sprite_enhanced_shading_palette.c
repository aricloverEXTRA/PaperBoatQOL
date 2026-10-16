#include "common.h"
#include "sprite.h"
#include "nu/nusys.h"
#include "port/patches/Patches.h"
#include "port/ui/cvar_prefixes.h"
#include "sprite/sprite_enhanced_shading.h"

extern GameStatus* gGameStatusPtr;

#define ENHANCED_SHADING_PALETTE_COUNT 512
static PAL_BIN sEnhancedShadingPalettes[ENHANCED_SHADING_PALETTE_COUNT][16];
static s32 sEnhancedShadingPaletteIdx = 0;

static void compute_hemisphere_lighting(const EnhancedShadingProfile* profile, Vec3f normal, Color_RGB8* outColor) {
    f32 ndot = normal.y * 0.5f + 0.5f;
    f32 invNdot = 1.0f - ndot;
    
    outColor->r = (u8)((profile->hemisphereSkyColor.r * ndot + profile->hemisphereGroundColor.r * invNdot) * profile->hemisphereIntensity);
    outColor->g = (u8)((profile->hemisphereSkyColor.g * ndot + profile->hemisphereGroundColor.g * invNdot) * profile->hemisphereIntensity);
    outColor->b = (u8)((profile->hemisphereSkyColor.b * ndot + profile->hemisphereGroundColor.b * invNdot) * profile->hemisphereIntensity);
}

static void compute_rim_lighting(const EnhancedShadingProfile* profile, Vec3f normal, Vec3f viewDir, Color_RGB8* outColor, f32* outIntensity) {
    f32 ndotv = normal.x * viewDir.x + normal.y * viewDir.y + normal.z * viewDir.z;
    f32 rim = 1.0f - ndotv;
    rim = rim < 0.0f ? 0.0f : (rim > 1.0f ? 1.0f : rim);
    rim = rim * rim;
    
    *outIntensity = rim * profile->rimLightIntensity;
    outColor->r = profile->rimLightColor.r;
    outColor->g = profile->rimLightColor.g;
    outColor->b = profile->rimLightColor.b;
}

static void compute_subsurface_scattering(const EnhancedShadingProfile* profile, Vec3f normal, Vec3f lightDir, Color_RGB8* outColor, f32* outIntensity) {
    f32 ndotl = normal.x * lightDir.x + normal.y * lightDir.y + normal.z * lightDir.z;
    f32 backScatter = 1.0f + ndotl;
    backScatter = backScatter < 0.0f ? 0.0f : (backScatter > 1.0f ? 1.0f : backScatter);
    
    *outIntensity = backScatter * profile->subsurfaceStrength;
    outColor->r = profile->subsurfaceColor.r;
    outColor->g = profile->subsurfaceColor.g;
    outColor->b = profile->subsurfaceColor.b;
}

static f32 compute_ambient_occlusion(const EnhancedShadingProfile* profile, f32 curvature) {
    f32 ao = 1.0f - (curvature * profile->aoIntensity);
    return ao < 0.0f ? 0.0f : (ao > 1.0f ? 1.0f : ao);
}

static f32 compute_dof_blur(const EnhancedShadingProfile* profile, f32 distance) {
    if (!(profile->flags & ENHANCED_SHADING_FLAG_DEPTH_OF_FIELD)) return 0.0f;
    
    f32 diff = distance - profile->dofFocalDistance;
    f32 blur = 0.0f;
    
    if (diff > profile->dofFocalRange) {
        blur = (diff - profile->dofFocalRange) / profile->dofFocalRange;
    } else if (diff < -profile->dofFocalRange) {
        blur = (-diff - profile->dofFocalRange) / profile->dofFocalRange;
    }
    
    blur = blur * profile->dofMaxBlur;
    if (blur > profile->dofMaxBlur) blur = profile->dofMaxBlur;
    
    // Near blur
    if (diff < -profile->dofFocalRange) {
        f32 nearBlur = (-diff - profile->dofFocalRange) / profile->dofFocalRange * profile->dofNearBlurMax;
        if (nearBlur > profile->dofNearBlurMax) nearBlur = profile->dofNearBlurMax;
        if (nearBlur > blur) blur = nearBlur;
    }
    
    return blur;
}

static f32 compute_shadow_factor(const EnhancedShadingProfile* profile, f32 distance, f32 ndotl) {
    if (!(profile->flags & ENHANCED_SHADING_FLAG_SHADOWS)) return 1.0f;
    if (distance > profile->shadowDistance) return 1.0f;
    if (ndotl <= 0.0f) return 1.0f;
    
    // Simple shadow attenuation based on distance and angle
    f32 shadowFactor = 1.0f - (ndotl * (1.0f - distance / profile->shadowDistance));
    shadowFactor = shadowFactor * 0.5f + 0.5f; // Blend with 50% shadow intensity
    
    return shadowFactor;
}

static f32 compute_bloom_factor(const EnhancedShadingProfile* profile, f32 luminance) {
    if (!(profile->flags & ENHANCED_SHADING_FLAG_BLOOM)) return 0.0f;
    if (luminance < profile->bloomThreshold) return 0.0f;
    
    f32 bloom = (luminance - profile->bloomThreshold) * profile->bloomIntensity;
    return bloom < 1.0f ? bloom : 1.0f;
}

static f32 apply_tone_mapping(const EnhancedShadingProfile* profile, f32 color) {
    if (!(profile->flags & ENHANCED_SHADING_FLAG_TONE_MAPPING)) return color;
    
    // Reinhard tone mapping
    f32 mapped = (color * profile->exposure) / (color * profile->exposure + profile->whitePoint);
    return mapped;
}

void create_enhanced_shading_palette(Matrix4f mtx, s32 uls, s32 ult, s32 lrs, s32 lrt, s32 alpha, s32 otherModeLBits) {
    Camera* camera = &gCameras[gCurrentCameraID];
    EnhancedShadingProfile* profile = gEnhancedShadingProfile;
    
    b32 enhancedEnabled = CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
    if (profile == NULL || !(profile->flags & ENHANCED_SHADING_FLAG_ENABLED) || !enhancedEnabled) {
        create_shading_palette(mtx, uls, ult, lrs, lrt, alpha, otherModeLBits);
        return;
    }
    
    int mode = CVarGetInteger(CVAR_ENHANCED_SHADING("Mode"), 1);
    if (mode == 0) {
        create_shading_palette(mtx, uls, ult, lrs, lrt, alpha, otherModeLBits);
        return;
    }
    
    // Determine effective feature enables. On restricted platforms (mobile/
    // console/web) the graphics preset is clamped and overrides individual
    // toggles; on desktop "Custom" uses the per-feature CVars.
    s32 graphicsPreset = enhanced_shading_get_effective_preset();
    b32 usePreset = (graphicsPreset != ENHANCED_GRAPHICS_PRESET_CUSTOM);

    b32 presetHemisphere = enhanced_shading_platform_allows_feature(ENHANCED_SHADING_FLAG_HEMISPHERE);
    b32 presetRim = enhanced_shading_platform_allows_feature(ENHANCED_SHADING_FLAG_RIM_LIGHTING);
    b32 presetSubsurface = enhanced_shading_platform_allows_feature(ENHANCED_SHADING_FLAG_SUBSURFACE);
    b32 presetAO = enhanced_shading_platform_allows_feature(ENHANCED_SHADING_FLAG_AMBIENT_OCCLUSION);
    b32 presetSmoothFalloff = enhanced_shading_platform_allows_feature(ENHANCED_SHADING_FLAG_FALLOFF_SMOOTH);
    b32 presetTemporal = enhanced_shading_platform_allows_feature(ENHANCED_SHADING_FLAG_TEMPORAL_STABILITY);
    b32 presetDynamicLights = enhanced_shading_platform_allows_feature(ENHANCED_SHADING_FLAG_DYNAMIC_LIGHTS);
    b32 presetDOF = enhanced_shading_platform_allows_feature(ENHANCED_SHADING_FLAG_DEPTH_OF_FIELD);
    b32 presetShadows = enhanced_shading_platform_allows_feature(ENHANCED_SHADING_FLAG_SHADOWS);
    b32 presetBloom = enhanced_shading_platform_allows_feature(ENHANCED_SHADING_FLAG_BLOOM);
    b32 presetToneMapping = enhanced_shading_platform_allows_feature(ENHANCED_SHADING_FLAG_TONE_MAPPING);

    b32 hemisphereEnabled = usePreset ? presetHemisphere : CVarGetInteger(CVAR_ENHANCED_SHADING("HemisphereLighting"), 1);
    b32 rimEnabled = usePreset ? presetRim : CVarGetInteger(CVAR_ENHANCED_SHADING("RimLighting"), 0);
    b32 subsurfaceEnabled = usePreset ? presetSubsurface : CVarGetInteger(CVAR_ENHANCED_SHADING("SubsurfaceScattering"), 0);
    b32 aoEnabled = usePreset ? presetAO : CVarGetInteger(CVAR_ENHANCED_SHADING("AmbientOcclusion"), 0);
    b32 smoothFalloffEnabled = usePreset ? presetSmoothFalloff : CVarGetInteger(CVAR_ENHANCED_SHADING("SmoothFalloff"), 1);
    b32 temporalEnabled = usePreset ? presetTemporal : CVarGetInteger(CVAR_ENHANCED_SHADING("TemporalStability"), 1);
    b32 dynamicLightsEnabled = usePreset ? presetDynamicLights : CVarGetInteger(CVAR_ENHANCED_SHADING("DynamicLights"), 1);
    b32 dynamicLightFlickerEnabled = CVarGetInteger(CVAR_ENHANCED_SHADING("DynamicLightFlicker"), 1);
    
    if (hemisphereEnabled) {
        profile->flags |= ENHANCED_SHADING_FLAG_HEMISPHERE;
    } else {
        profile->flags &= ~ENHANCED_SHADING_FLAG_HEMISPHERE;
    }
    
    if (rimEnabled) {
        profile->flags |= ENHANCED_SHADING_FLAG_RIM_LIGHTING;
        profile->rimLightIntensity = CVarGetFloat(CVAR_ENHANCED_SHADING("RimIntensity"), 0.5f);
    } else {
        profile->flags &= ~ENHANCED_SHADING_FLAG_RIM_LIGHTING;
    }
    
    if (subsurfaceEnabled) {
        profile->flags |= ENHANCED_SHADING_FLAG_SUBSURFACE;
        profile->subsurfaceStrength = CVarGetFloat(CVAR_ENHANCED_SHADING("SubsurfaceStrength"), 0.3f);
    } else {
        profile->flags &= ~ENHANCED_SHADING_FLAG_SUBSURFACE;
    }
    
    if (aoEnabled) {
        profile->flags |= ENHANCED_SHADING_FLAG_AMBIENT_OCCLUSION;
        profile->aoIntensity = CVarGetFloat(CVAR_ENHANCED_SHADING("AOIntensity"), 0.5f);
    } else {
        profile->flags &= ~ENHANCED_SHADING_FLAG_AMBIENT_OCCLUSION;
    }
    
    if (smoothFalloffEnabled) {
        profile->flags |= ENHANCED_SHADING_FLAG_FALLOFF_SMOOTH;
    } else {
        profile->flags &= ~ENHANCED_SHADING_FLAG_FALLOFF_SMOOTH;
    }
    
    if (temporalEnabled) {
        profile->flags |= ENHANCED_SHADING_FLAG_TEMPORAL_STABILITY;
    } else {
        profile->flags &= ~ENHANCED_SHADING_FLAG_TEMPORAL_STABILITY;
    }
    
    if (dynamicLightsEnabled) {
        profile->flags |= ENHANCED_SHADING_FLAG_DYNAMIC_LIGHTS;
    } else {
        profile->flags &= ~ENHANCED_SHADING_FLAG_DYNAMIC_LIGHTS;
    }
    
    b32 dofEnabled = usePreset ? presetDOF : CVarGetInteger(CVAR_ENHANCED_SHADING("DepthOfField"), 0);
    b32 shadowsEnabled = usePreset ? presetShadows : CVarGetInteger(CVAR_ENHANCED_SHADING("Shadows"), 0);
    b32 bloomEnabled = usePreset ? presetBloom : CVarGetInteger(CVAR_ENHANCED_SHADING("Bloom"), 0);
    b32 toneMappingEnabled = usePreset ? presetToneMapping : CVarGetInteger(CVAR_ENHANCED_SHADING("ToneMapping"), 0);
    b32 skyboxDofExempt = CVarGetInteger(CVAR_ENHANCED_SHADING("SkyboxDOFExempt"), 1);
    
    if (dofEnabled) {
        profile->flags |= ENHANCED_SHADING_FLAG_DEPTH_OF_FIELD;
        profile->dofFocalDistance = CVarGetFloat(CVAR_ENHANCED_SHADING("DOFFocalDistance"), 500.0f);
        profile->dofFocalRange = CVarGetFloat(CVAR_ENHANCED_SHADING("DOFFocalRange"), 100.0f);
        profile->dofMaxBlur = CVarGetFloat(CVAR_ENHANCED_SHADING("DOFMaxBlur"), 0.5f);
        profile->dofNearBlurMax = CVarGetFloat(CVAR_ENHANCED_SHADING("DOFNearBlurMax"), 0.3f);
        profile->dofSkyboxExempt = skyboxDofExempt;
    } else {
        profile->flags &= ~ENHANCED_SHADING_FLAG_DEPTH_OF_FIELD;
    }
    
    if (shadowsEnabled) {
        profile->flags |= ENHANCED_SHADING_FLAG_SHADOWS;
        profile->shadowDistance = CVarGetFloat(CVAR_ENHANCED_SHADING("ShadowDistance"), 1000.0f);
        profile->shadowBias = CVarGetFloat(CVAR_ENHANCED_SHADING("ShadowBias"), 0.005f);
        profile->shadowNormalBias = CVarGetFloat(CVAR_ENHANCED_SHADING("ShadowNormalBias"), 0.02f);
        profile->shadowResolution = CVarGetInteger(CVAR_ENHANCED_SHADING("ShadowResolution"), 2);
        profile->shadowPCF = CVarGetInteger(CVAR_ENHANCED_SHADING("ShadowPCF"), 1);
    } else {
        profile->flags &= ~ENHANCED_SHADING_FLAG_SHADOWS;
    }
    
    if (bloomEnabled) {
        profile->flags |= ENHANCED_SHADING_FLAG_BLOOM;
        profile->bloomThreshold = CVarGetFloat(CVAR_ENHANCED_SHADING("BloomThreshold"), 1.0f);
        profile->bloomIntensity = CVarGetFloat(CVAR_ENHANCED_SHADING("BloomIntensity"), 0.3f);
        profile->bloomRadius = CVarGetFloat(CVAR_ENHANCED_SHADING("BloomRadius"), 1.0f);
    } else {
        profile->flags &= ~ENHANCED_SHADING_FLAG_BLOOM;
    }
    
    if (toneMappingEnabled) {
        profile->flags |= ENHANCED_SHADING_FLAG_TONE_MAPPING;
        profile->exposure = CVarGetFloat(CVAR_ENHANCED_SHADING("Exposure"), 1.0f);
        profile->whitePoint = CVarGetFloat(CVAR_ENHANCED_SHADING("WhitePoint"), 1.0f);
    } else {
        profile->flags &= ~ENHANCED_SHADING_FLAG_TONE_MAPPING;
    }
    
    f32 shadowDirX = 0.0f;
    f32 shadowDirY = 0.0f;
    f32 shadowDirZ = 0.0f;
    
    f32 posX = mtx[3][0];
    f32 posY = mtx[3][1];
    f32 posZ = mtx[3][2];
    
    f32 Mxz = mtx[0][2];
    f32 Myz = mtx[1][2];
    f32 Mzz = mtx[2][2];
    
    f32 Pxz = camera->mtxPerspective[0][2];
    f32 Pzz = camera->mtxPerspective[2][2];
    
    f32 facingDir = ((-Mxz * Pxz + Mzz * Pzz) < 0.0f) ? 1.0f : -1.0f;
    
    Vec3f worldNormal;
    if (facingDir < 0.0f) {
        worldNormal.x = Mzz;
        worldNormal.y = Myz;
        worldNormal.z = Mxz;
    } else {
        worldNormal.x = -Mzz;
        worldNormal.y = Myz;
        worldNormal.z = -Mxz;
    }
    
    f32 normalLen = sqrtf(worldNormal.x * worldNormal.x + worldNormal.y * worldNormal.y + worldNormal.z * worldNormal.z);
    if (normalLen > 0.0f) {
        worldNormal.x /= normalLen;
        worldNormal.y /= normalLen;
        worldNormal.z /= normalLen;
    }
    
    Vec3f viewDir = { -camera->mtxViewPlayer[2][0], -camera->mtxViewPlayer[2][1], -camera->mtxViewPlayer[2][2] };
    f32 viewLen = sqrtf(viewDir.x * viewDir.x + viewDir.y * viewDir.y + viewDir.z * viewDir.z);
    if (viewLen > 0.0f) {
        viewDir.x /= viewLen;
        viewDir.y /= viewLen;
        viewDir.z /= viewLen;
    }
    
    Color_RGB8 finalShadowColor = profile->ambientColor;
    Color_RGB8 finalHighlightColor = { 255, 255, 255 };
    f32 totalShadowIntensity = 1.0f;
    f32 totalLightIntensity = 1.0f;
    
    if (profile->flags & ENHANCED_SHADING_FLAG_USE_PRESET || profile->flags & ENHANCED_SHADING_FLAG_USE_CUSTOM) {
        Color_RGB8 hemisphereColor;
        compute_hemisphere_lighting(profile, worldNormal, &hemisphereColor);
        
        finalShadowColor.r = (u8)MIN(255, (s32)finalShadowColor.r + hemisphereColor.r);
        finalShadowColor.g = (u8)MIN(255, (s32)finalShadowColor.g + hemisphereColor.g);
        finalShadowColor.b = (u8)MIN(255, (s32)finalShadowColor.b + hemisphereColor.b);
    }
    
    for (s32 i = 0; i < profile->lightCount; i++) {
        EnhancedLightSource* light = &profile->lights[i];
        if (!(light->flags & 1)) continue;
        
        Vec3f lightDir = { 0.0f, 0.0f, 0.0f };
        f32 dist = 0.0f;
        f32 intensityScale = 1.0f;
        
        switch (light->type) {
            case ENHANCED_LIGHT_DIRECTIONAL:
                lightDir.x = -light->dir.x;
                lightDir.y = -light->dir.y;
                lightDir.z = -light->dir.z;
                break;
                
            case ENHANCED_LIGHT_POINT: {
                f32 dx = posX - light->pos.x;
                f32 dy = posY - light->pos.y;
                f32 dz = posZ - light->pos.z;
                dist = sqrtf(dx*dx + dy*dy + dz*dz);
                if (dist > 0.0f) {
                    lightDir.x = dx / dist;
                    lightDir.y = dy / dist;
                    lightDir.z = dz / dist;
                    if (profile->flags & ENHANCED_SHADING_FLAG_FALLOFF_SMOOTH) {
                        intensityScale = 1.0f / (1.0f + dist * light->falloff + dist * dist * light->falloff * 0.1f);
                    } else {
                        intensityScale = 1.0f / (1.0f + dist * light->falloff);
                    }
                } else {
                    lightDir.x = lightDir.y = lightDir.z = 0.0f;
                    intensityScale = 1.0f;
                }
                break;
            }
                
            case ENHANCED_LIGHT_SPOT: {
                f32 dx = posX - light->pos.x;
                f32 dy = posY - light->pos.y;
                f32 dz = posZ - light->pos.z;
                dist = sqrtf(dx*dx + dy*dy + dz*dz);
                if (dist > 0.0f) {
                    lightDir.x = dx / dist;
                    lightDir.y = dy / dist;
                    lightDir.z = dz / dist;
                    
                    f32 spotDot = lightDir.x * (-light->dir.x) + lightDir.y * (-light->dir.y) + lightDir.z * (-light->dir.z);
                    f32 spotCosInner = cosf(light->innerAngle * 3.14159f / 180.0f);
                    f32 spotCosOuter = cosf(light->outerAngle * 3.14159f / 180.0f);
                    
                    if (spotDot > spotCosInner) {
                        intensityScale = 1.0f;
                    } else if (spotDot > spotCosOuter) {
                        intensityScale = (spotDot - spotCosOuter) / (spotCosInner - spotCosOuter);
                    } else {
                        intensityScale = 0.0f;
                    }
                    
                    if (profile->flags & ENHANCED_SHADING_FLAG_FALLOFF_SMOOTH) {
                        intensityScale /= (1.0f + dist * light->falloff + dist * dist * light->falloff * 0.1f);
                    } else {
                        intensityScale /= (1.0f + dist * light->falloff);
                    }
                } else {
                    intensityScale = 0.0f;
                }
                break;
            }
                
            case ENHANCED_LIGHT_HEMISPHERE:
                lightDir.x = light->dir.x;
                lightDir.y = light->dir.y;
                lightDir.z = light->dir.z;
                break;
        }
        
        if (intensityScale <= 0.0f) continue;
        if (intensityScale > 1.0f) intensityScale = 1.0f;
        
        f32 ndotl = worldNormal.x * lightDir.x + worldNormal.y * lightDir.y + worldNormal.z * lightDir.z;
        f32 ndotv = worldNormal.x * viewDir.x + worldNormal.y * viewDir.y + worldNormal.z * viewDir.z;
        
        f32 shadowIntensity = intensityScale * (ndotl > 0.0f ? ndotl : -ndotl * 0.3f);
        f32 lightIntensity = intensityScale * (ndotl > 0.0f ? ndotl : 0.0f);
        
        if (profile->flags & ENHANCED_SHADING_FLAG_RIM_LIGHTING) {
            Color_RGB8 rimColor;
            f32 rimIntensity;
            compute_rim_lighting(profile, worldNormal, viewDir, &rimColor, &rimIntensity);
            finalHighlightColor.r = (u8)MIN(255, (s32)finalHighlightColor.r + rimColor.r * rimIntensity);
            finalHighlightColor.g = (u8)MIN(255, (s32)finalHighlightColor.g + rimColor.g * rimIntensity);
            finalHighlightColor.b = (u8)MIN(255, (s32)finalHighlightColor.b + rimColor.b * rimIntensity);
            totalLightIntensity += rimIntensity;
        }
        
        if (profile->flags & ENHANCED_SHADING_FLAG_SUBSURFACE && ndotl < 0.0f) {
            Color_RGB8 sssColor;
            f32 sssIntensity;
            compute_subsurface_scattering(profile, worldNormal, lightDir, &sssColor, &sssIntensity);
            finalShadowColor.r = (u8)MIN(255, (s32)finalShadowColor.r + sssColor.r * sssIntensity);
            finalShadowColor.g = (u8)MIN(255, (s32)finalShadowColor.g + sssColor.g * sssIntensity);
            finalShadowColor.b = (u8)MIN(255, (s32)finalShadowColor.b + sssColor.b * sssIntensity);
        }
        
        shadowDirX += lightDir.x * shadowIntensity;
        shadowDirY += lightDir.y * shadowIntensity;
        shadowDirZ += lightDir.z * shadowIntensity;
        
        finalShadowColor.r = (u8)MIN(255, (s32)finalShadowColor.r + light->color.r * shadowIntensity * 0.5f);
        finalShadowColor.g = (u8)MIN(255, (s32)finalShadowColor.g + light->color.g * shadowIntensity * 0.5f);
        finalShadowColor.b = (u8)MIN(255, (s32)finalShadowColor.b + light->color.b * shadowIntensity * 0.5f);
        
        finalHighlightColor.r = (u8)MIN(255, (s32)finalHighlightColor.r + light->color.r * lightIntensity);
        finalHighlightColor.g = (u8)MIN(255, (s32)finalHighlightColor.g + light->color.g * lightIntensity);
        finalHighlightColor.b = (u8)MIN(255, (s32)finalHighlightColor.b + light->color.b * lightIntensity);
        
        totalShadowIntensity += shadowIntensity;
        totalLightIntensity += lightIntensity;
    }
    
    // Process dynamic lights (torches, lanterns, candles, etc.)
    if (profile->flags & ENHANCED_SHADING_FLAG_DYNAMIC_LIGHTS) {
        for (s32 i = 0; i < profile->dynamicLightCount; i++) {
            DynamicLight* dynLight = &profile->dynamicLights[i];
            if (!(dynLight->flags & 1)) continue;
            
            f32 dx = posX - dynLight->pos.x;
            f32 dy = posY - dynLight->pos.y;
            f32 dz = posZ - dynLight->pos.z;
            f32 dist = sqrtf(dx*dx + dy*dy + dz*dz);
            
            if (dist > dynLight->radius) continue;
            
            Vec3f lightDir;
            if (dist > 0.0f) {
                lightDir.x = dx / dist;
                lightDir.y = dy / dist;
                lightDir.z = dz / dist;
            } else {
                lightDir.x = lightDir.y = lightDir.z = 0.0f;
            }
            
            f32 intensityScale = 1.0f;
            if (profile->flags & ENHANCED_SHADING_FLAG_FALLOFF_SMOOTH) {
                intensityScale = 1.0f / (1.0f + dist * 0.01f + dist * dist * 0.0001f);
            } else {
                intensityScale = 1.0f / (1.0f + dist * 0.01f);
            }
            
            // Apply flicker for torches/candles
            if (dynamicLightFlickerEnabled && (dynLight->type == DYNAMIC_LIGHT_TORCH || dynLight->type == DYNAMIC_LIGHT_CANDLE)) {
                f32 flicker = sinf(gGameStatusPtr->frameCounter * dynLight->flickerSpeed * 0.1f) * dynLight->flickerAmount;
                intensityScale *= (1.0f + flicker);
            }
            
            if (intensityScale <= 0.0f) continue;
            if (intensityScale > 1.0f) intensityScale = 1.0f;
            
            f32 ndotl = worldNormal.x * lightDir.x + worldNormal.y * lightDir.y + worldNormal.z * lightDir.z;
            
            f32 shadowIntensity = intensityScale * (ndotl > 0.0f ? ndotl : -ndotl * 0.3f) * dynLight->intensity;
            f32 lightIntensity = intensityScale * (ndotl > 0.0f ? ndotl : 0.0f) * dynLight->intensity;
            
            shadowDirX += lightDir.x * shadowIntensity;
            shadowDirY += lightDir.y * shadowIntensity;
            shadowDirZ += lightDir.z * shadowIntensity;
            
            finalShadowColor.r = (u8)MIN(255, (s32)finalShadowColor.r + dynLight->color.r * shadowIntensity * 0.5f);
            finalShadowColor.g = (u8)MIN(255, (s32)finalShadowColor.g + dynLight->color.g * shadowIntensity * 0.5f);
            finalShadowColor.b = (u8)MIN(255, (s32)finalShadowColor.b + dynLight->color.b * shadowIntensity * 0.5f);
            
            finalHighlightColor.r = (u8)MIN(255, (s32)finalHighlightColor.r + dynLight->color.r * lightIntensity);
            finalHighlightColor.g = (u8)MIN(255, (s32)finalHighlightColor.g + dynLight->color.g * lightIntensity);
            finalHighlightColor.b = (u8)MIN(255, (s32)finalHighlightColor.b + dynLight->color.b * lightIntensity);
            
            totalShadowIntensity += shadowIntensity;
            totalLightIntensity += lightIntensity;
        }
    }
    
    if (profile->flags & ENHANCED_SHADING_FLAG_AMBIENT_OCCLUSION) {
        f32 curvature = 1.0f - fabsf(worldNormal.y);
        f32 ao = compute_ambient_occlusion(profile, curvature);
        finalShadowColor.r = (u8)(finalShadowColor.r * ao);
        finalShadowColor.g = (u8)(finalShadowColor.g * ao);
        finalShadowColor.b = (u8)(finalShadowColor.b * ao);
    }
    
    f32 shadowMag = sqrtf(shadowDirX * shadowDirX + shadowDirY * shadowDirY + shadowDirZ * shadowDirZ);
    if (shadowMag > 0.0f) {
        shadowDirX /= shadowMag;
        shadowDirY /= shadowMag;
        shadowDirZ /= shadowMag;
    }
    
    if (profile->flags & ENHANCED_SHADING_FLAG_TEMPORAL_STABILITY) {
        static f32 prevShadowDirX = 0, prevShadowDirY = 0, prevShadowDirZ = 0;
        static Color_RGB8 prevShadowColor = {50, 50, 50};
        static Color_RGB8 prevHighlightColor = {255, 255, 255};
        
        f32 blend = profile->temporalBlendFactor;
        shadowDirX = shadowDirX * (1.0f - blend) + prevShadowDirX * blend;
        shadowDirY = shadowDirY * (1.0f - blend) + prevShadowDirY * blend;
        shadowDirZ = shadowDirZ * (1.0f - blend) + prevShadowDirZ * blend;
        
        finalShadowColor.r = (u8)(finalShadowColor.r * (1.0f - blend) + prevShadowColor.r * blend);
        finalShadowColor.g = (u8)(finalShadowColor.g * (1.0f - blend) + prevShadowColor.g * blend);
        finalShadowColor.b = (u8)(finalShadowColor.b * (1.0f - blend) + prevShadowColor.b * blend);
        
        finalHighlightColor.r = (u8)(finalHighlightColor.r * (1.0f - blend) + prevHighlightColor.r * blend);
        finalHighlightColor.g = (u8)(finalHighlightColor.g * (1.0f - blend) + prevHighlightColor.g * blend);
        finalHighlightColor.b = (u8)(finalHighlightColor.b * (1.0f - blend) + prevHighlightColor.b * blend);
        
        prevShadowDirX = shadowDirX;
        prevShadowDirY = shadowDirY;
        prevShadowDirZ = shadowDirZ;
        prevShadowColor = finalShadowColor;
        prevHighlightColor = finalHighlightColor;
    }
    
    port_appendGfx_shading_palette(
        mtx,
        uls, ult, lrs, lrt,
        alpha,
        shadowDirX, shadowDirY, shadowDirZ,
        finalShadowColor.r, finalShadowColor.g, finalShadowColor.b,
        finalHighlightColor.r, finalHighlightColor.g, finalHighlightColor.b,
        profile->ambientPower,
        otherModeLBits
    );
}