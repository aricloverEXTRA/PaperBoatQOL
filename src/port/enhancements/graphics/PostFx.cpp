/**
 * PaperBoat enhanced-shading support.
 *
 * The screen-space post-processing chain (colour grade / glow / outline / god
 * rays) was removed: registering those passes crashed both the DirectX 11 and
 * OpenGL backends and never produced a usable image. The two effects that remain
 * run inside the engine's main shader, so they need no extra passes:
 *   - engine lighting   (uCustom[8]) : hemisphere + wrapped diffuse from the
 *                                       area's own light colours
 *   - modern-look grade (uCustom[9]) : saturation / contrast / brightness
 *   - sunlight          (uCustom[12..18,21,22]) : per-fragment sun + hemisphere,
 *                                       albedo-recovery relight, tonemap and the
 *                                       warm-sun / ambient-level / exposure knobs
 */

// Fast3dWindow.h pulls in <fast/interpreter.h> -> prism, which must be parsed
// before anything that defines the script_api "Else" macro (pulled in
// transitively by port/hooks/Events.h via common_structs.h).
#include <fast/Fast3dWindow.h>

#include "port/hooks/Events.h"
#include "port/hooks/impl/EventSystem.h"
#include "port/ui/cvar_prefixes.h"
#include "port/Engine.h"
#include <spdlog/spdlog.h>

#include <cmath>
#include <cstring>

#include <libultraship/libultraship.h>
#include <libultraship/bridge/consolevariablebridge.h>

extern "C" void gfx_set_custom_uniform(uint8_t idx, const float values[4]);

// Derived in BackgroundPatches.cpp from the current map's background palette.
extern "C" void port_get_area_ambient(float* outR, float* outG, float* outB);

// Defined in CameraPatches.cpp; projects the world sun direction to screen space
// (uv in 0..1, visible = 1 when in front of the camera and roughly on screen).
extern "C" void port_get_sun_screen_uv(float* outU, float* outV, int* outVisible);

// Defined in EnhancedShadingBridge.cpp; overrides the fragment-lighting uniforms
// with the current area's authored preset (or a palette-derived fallback) so every
// map gets lighting tuned for its own art.
extern "C" void EnhancedShadingBridge_PushLightingUniforms(void);
extern "C" const char* EnhancedShadingBridge_CurrentAreaName(void);
extern "C" b32 EnhancedShadingBridge_CurrentAreaHasPreset(void);

// Defined in port/Engine.cpp; returns the active Fast3D interpreter (or null).
Fast::Interpreter* GameEngine_GetInterpreter();

static float pbClampf(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// Master gate. The "Enable Enhanced Shading" checkbox in the menu drives this.
// When it is off every enhanced-shading effect is fully disabled: the in-shader
// effects receive neutral uniforms (each effect's enable register is zeroed) and
// the depth-consuming post passes are unregistered. This is the only switch that
// reliably returns the game to vanilla, and the known-good starting point when
// bisecting a crash.
#define POSTFX_MASTER_CVAR CVAR_ENHANCED_SHADING("Enabled")

static bool pbMasterEnabled() {
    return CVarGetInteger(POSTFX_MASTER_CVAR, 1) != 0;
}

// PaperBoat "always-on" switch. When true, the modern DirectX 11 lighting
// pipeline (modern grade, area ambient, fragment light, albedo relight, tonemap)
// is driven regardless of its enable CVars, so the enhanced look is the only look
// the game shows and there is no "vanilla" fallback. This previously trapped every
// in-shader effect permanently on (its menu toggles did nothing) and left no way
// to disable a misbehaving effect, so it is left off: the menu toggles are
// authoritative and the master switch above can still turn everything off.
static constexpr bool kModernLightingForced = false;

static bool pbEffectOn(const char* cvar, int defaultValue) {
    if (!pbMasterEnabled()) {
        return false;
    }
    return kModernLightingForced || CVarGetInteger(cvar, defaultValue) != 0;
}

// Engine-level enhanced lighting (DirectX 11 shader path). When enabled the
// engine's vertex lighting switches from the flat N64 ambient to a hemisphere +
// wrapped-diffuse model derived from the area's own light colours. Off by default
// so the vanilla N64 lighting is untouched.
#define POSTFX_ENGINE_LIGHTING_CVAR        CVAR_ENHANCED_SHADING("EngineLighting")
#define POSTFX_ENGINE_LIGHTING_WRAP_CVAR   CVAR_ENHANCED_SHADING("EngineLightingWrap")
#define POSTFX_ENGINE_LIGHTING_GROUND_CVAR CVAR_ENHANCED_SHADING("EngineLightingGround")
#define POSTFX_ENGINE_LIGHTING_BOOST_CVAR  CVAR_ENHANCED_SHADING("EngineLightingBoost")
// Whole-world "modern look" grade (DirectX 11 shader path). Runs inside the main
// shader so it does not depend on the post-pass chain: saturation / contrast /
// brightness applied to every shaded fragment, driven through uCustom[9].
#define POSTFX_MODERN_CVAR      CVAR_ENHANCED_SHADING("ModernLook")
#define POSTFX_MODERN_SAT_CVAR  CVAR_ENHANCED_SHADING("ModernSaturation")
#define POSTFX_MODERN_CON_CVAR  CVAR_ENHANCED_SHADING("ModernContrast")
#define POSTFX_MODERN_BRI_CVAR  CVAR_ENHANCED_SHADING("ModernBrightness")

// Per-area ambient colour grade (DirectX 11 shader path). Each area is given the
// mood of its own background palette, auto-derived at runtime so every map is
// covered without authored per-area data. Register 10 = (tintR, tintG, tintB,
// strength); strength == 0 leaves the image unchanged.
#define POSTFX_AMBIENT_CVAR          CVAR_ENHANCED_SHADING("AreaAmbient")
#define POSTFX_AMBIENT_STRENGTH_CVAR CVAR_ENHANCED_SHADING("AreaAmbientStrength")

// Per-fragment world lighting (DirectX 11 shader path). The shader reconstructs a
// flat per-triangle normal from the vertex position and applies a per-area
// hemisphere + sun (+ optional rim), giving the world real directional shading on
// top of its baked colours. Registers:
//   [12] = (sunDirX, sunDirY, sunDirZ, sunIntensity)
//   [13] = (skyR, skyG, skyB, skyStrength)
//   [14] = (groundR, groundG, groundB, groundStrength)
//   [15] = (enabled, strength, wrap, normalSign)
//   [16] = (rimIntensity, rimPower, rimSkyBias, 0)
//   [17] = (relightEnabled, relightStrength, relightFloor, 0)
//   [18] = (tonemapEnabled, exposure, curve, blend)
// The Hemisphere/Rim toggles reuse the pre-existing CVars of those names.
#define POSTFX_FRAGLIGHT_CVAR       CVAR_ENHANCED_SHADING("FragmentLighting")
#define POSTFX_FRAGLIGHT_STR_CVAR   CVAR_ENHANCED_SHADING("FragmentLightingStrength")
#define POSTFX_FRAGLIGHT_WRAP_CVAR  CVAR_ENHANCED_SHADING("FragmentLightingWrap")
#define POSTFX_FRAGLIGHT_FLIP_CVAR  CVAR_ENHANCED_SHADING("FlipLightingNormal")
#define POSTFX_SUN_AZIMUTH_CVAR     CVAR_ENHANCED_SHADING("SunAzimuth")
#define POSTFX_SUN_ELEVATION_CVAR   CVAR_ENHANCED_SHADING("SunElevation")
#define POSTFX_SUN_INTENSITY_CVAR   CVAR_ENHANCED_SHADING("SunIntensity")
#define POSTFX_HEMISPHERE_CVAR      CVAR_ENHANCED_SHADING("HemisphereLighting")
#define POSTFX_RIM_CVAR             CVAR_ENHANCED_SHADING("RimLighting")
#define POSTFX_RIM_INTENSITY_CVAR   CVAR_ENHANCED_SHADING("RimIntensity")

// Sunlight art-direction: a warm sun colour, a shadow-deepening ambient level and
// a whole-scene exposure handed to the shader through registers 21 and 22. Only
// affects the per-fragment light, so it is a no-op while that is disabled.
#define POSTFX_SUN_WARMTH_CVAR   CVAR_ENHANCED_SHADING("SunWarmth")
#define POSTFX_SUN_AMBIENT_CVAR  CVAR_ENHANCED_SHADING("SunAmbientLevel")
#define POSTFX_SUN_EXPOSURE_CVAR CVAR_ENHANCED_SHADING("SunExposure")
#define POSTFX_SUN_DEBUG_CVAR    CVAR_ENHANCED_SHADING("SunDebug")

// Albedo relight: recovers the diffuse albedo from lit fragments and re-applies
// it with a new lighting model. Register 17 = (enabled, strength, floor, 0).
#define POSTFX_RELIGHT_CVAR       CVAR_ENHANCED_SHADING("RelightMode")
#define POSTFX_RELIGHT_STR_CVAR   CVAR_ENHANCED_SHADING("RelightStrength")
#define POSTFX_RELIGHT_FLOOR_CVAR CVAR_ENHANCED_SHADING("RelightBakedFloor")

// Tonemap: exposure + optional ACES filmic curve + blend with original.
#define POSTFX_TONEMAP_CVAR          CVAR_ENHANCED_SHADING("ToneMapping")
#define POSTFX_TONEMAP_EXPOSURE_CVAR CVAR_ENHANCED_SHADING("TonemapExposure")
#define POSTFX_TONEMAP_CURVE_CVAR    CVAR_ENHANCED_SHADING("TonemapCurve")
#define POSTFX_TONEMAP_BLEND_CVAR    CVAR_ENHANCED_SHADING("TonemapBlend")

// Depth-debug visualisation post pass. Registers 19/20 not used here; uses
// its own path through the engine's debug channel.
#define POSTFX_DEPTH_DEBUG_CVAR  CVAR_ENHANCED_SHADING("DepthDebug")

// Screen-space ambient occlusion post pass. Registers 19/20 = params/debug.
#define POSTFX_SSAO_CVAR        CVAR_ENHANCED_SHADING("SSAO")
#define POSTFX_SSAO_STR_CVAR    CVAR_ENHANCED_SHADING("SSAOStrength")
#define POSTFX_SSAO_RADIUS_CVAR CVAR_ENHANCED_SHADING("SSAORadius")
#define POSTFX_SSAO_BIAS_CVAR   CVAR_ENHANCED_SHADING("SSAOBias")
#define POSTFX_SSAO_DEBUG_CVAR  CVAR_ENHANCED_SHADING("SSAODebug")

// Depth-derived sun shading post pass. Reuses the sun/hemisphere uniforms
// already written for the in-shader fragment light (registers 12..14, 21, 22)
// and the view-projection matrix the engine publishes (23..27); only its own
// enable/strength registers are written here.
#define POSTFX_SUNSHADE_CVAR        CVAR_ENHANCED_SHADING("DepthSunShading")
#define POSTFX_SUNSHADE_STR_CVAR    CVAR_ENHANCED_SHADING("DepthSunStrength")
#define POSTFX_SUNSHADE_WRAP_CVAR   CVAR_ENHANCED_SHADING("DepthSunWrap")
#define POSTFX_SUNSHADE_DEBUG_CVAR  CVAR_ENHANCED_SHADING("DepthSunDebug")

// Screen-space god rays post pass. Hands the tuning to registers 30/31 (the sun's
// screen position comes from CameraPatches via port_get_sun_screen_uv) and
// registers the pass while the CVar is set. Disabled (register 30.x == 0) when
// the sun is behind the camera or off screen, so the shader passes through.
#define POSTFX_GODRAYS_CVAR          CVAR_ENHANCED_SHADING("GodRays")
#define POSTFX_GODRAYS_INTENSITY_CVAR CVAR_ENHANCED_SHADING("GodRayIntensity")
#define POSTFX_GODRAYS_DENSITY_CVAR  CVAR_ENHANCED_SHADING("GodRayDensity")
#define POSTFX_GODRAYS_DECAY_CVAR    CVAR_ENHANCED_SHADING("GodRayDecay")
#define POSTFX_GODRAYS_WEIGHT_CVAR   CVAR_ENHANCED_SHADING("GodRayWeight")
#define POSTFX_GODRAYS_THRESHOLD_CVAR CVAR_ENHANCED_SHADING("GodRayThreshold")

// Screen-space contact shadows post pass. Short-range, high-frequency shadowing
// from the depth buffer. Registers 2/3 = params/debug.
#define POSTFX_CONTACT_CVAR          CVAR_ENHANCED_SHADING("ContactShadows")
#define POSTFX_CONTACT_STR_CVAR      CVAR_ENHANCED_SHADING("ContactShadowStrength")
#define POSTFX_CONTACT_LEN_CVAR      CVAR_ENHANCED_SHADING("ContactShadowLength")
#define POSTFX_CONTACT_THICK_CVAR    CVAR_ENHANCED_SHADING("ContactShadowThickness")
#define POSTFX_CONTACT_DEBUG_CVAR    CVAR_ENHANCED_SHADING("ContactShadowDebug")

// Screen-space cast shadows post pass. Registers 36/37 = params/debug.
#define POSTFX_SSSHADOW_CVAR         CVAR_ENHANCED_SHADING("ScreenSpaceShadows")
#define POSTFX_SSSHADOW_STR_CVAR     CVAR_ENHANCED_SHADING("ScreenSpaceShadowStrength")
#define POSTFX_SSSHADOW_LEN_CVAR     CVAR_ENHANCED_SHADING("ScreenSpaceShadowLength")
#define POSTFX_SSSHADOW_THICK_CVAR   CVAR_ENHANCED_SHADING("ScreenSpaceShadowThickness")
#define POSTFX_SSSHADOW_BIAS_CVAR    CVAR_ENHANCED_SHADING("ScreenSpaceShadowBias")
#define POSTFX_SSSHADOW_DEBUG_CVAR   CVAR_ENHANCED_SHADING("ScreenSpaceShadowDebug")

// Bloom post pass. Registers 4/5 = params/debug.
#define POSTFX_BLOOM_CVAR            CVAR_ENHANCED_SHADING("Bloom")
#define POSTFX_BLOOM_INT_CVAR        CVAR_ENHANCED_SHADING("BloomIntensity")
#define POSTFX_BLOOM_THRESH_CVAR     CVAR_ENHANCED_SHADING("BloomThreshold")
#define POSTFX_BLOOM_RADIUS_CVAR     CVAR_ENHANCED_SHADING("BloomRadius")
#define POSTFX_BLOOM_DEBUG_CVAR      CVAR_ENHANCED_SHADING("BloomDebug")

// FXAA post pass. Register 6 = params.
#define POSTFX_FXAA_CVAR             CVAR_ENHANCED_SHADING("FXAA")
#define POSTFX_FXAA_SUBPIX_CVAR      CVAR_ENHANCED_SHADING("FXAASubpixel")
#define POSTFX_FXAA_EDGE_CVAR        CVAR_ENHANCED_SHADING("FXAAEdgeThreshold")
#define POSTFX_FXAA_EDGEMIN_CVAR     CVAR_ENHANCED_SHADING("FXAAEdgeThresholdMin")

// Universal specular post pass. Registers 38/39 = params/debug.
#define POSTFX_SPECULAR_CVAR         CVAR_ENHANCED_SHADING("Specular")
#define POSTFX_SPECULAR_STR_CVAR     CVAR_ENHANCED_SHADING("SpecularStrength")
#define POSTFX_SPECULAR_SHINY_CVAR   CVAR_ENHANCED_SHADING("SpecularShininess")
#define POSTFX_SPECULAR_DEBUG_CVAR   CVAR_ENHANCED_SHADING("SpecularDebug")

// Built-in visual presets (applies a curated set of all CVars at once)
#define POSTFX_BUILTIN_PRESET_CVAR      CVAR_ENHANCED_SHADING("BuiltInPreset")
#define POSTFX_AUTOINDOOR_PRESET_CVAR   CVAR_ENHANCED_SHADING("AutoIndoorPreset")

// Dev Tools -> General -> Shader Log Level. Controls the PaperBoat custom-lighting
// and shader diagnostics (this file and the engine's shader-permutation log) only.
// 0 = off, 1 = basic (on change), 2 = verbose (also on a periodic heartbeat).
#define POSTFX_SHADER_LOG_CVAR  CVAR_DEVELOPER_TOOLS("ShaderLogLevel")

// Modern shader suite CVars
#define POSTFX_PBR_CVAR              CVAR_ENHANCED_SHADING("PBR")
#define POSTFX_PBR_USE_ORM_CVAR      CVAR_ENHANCED_SHADING("PBRUseORM")
#define POSTFX_PBR_ENV_INT_CVAR      CVAR_ENHANCED_SHADING("PBREnvIntensity")
#define POSTFX_PBR_DEBUG_CVAR        CVAR_ENHANCED_SHADING("PBRDebug")

#define POSTFX_LUT_CVAR              CVAR_ENHANCED_SHADING("LUT")
#define POSTFX_LUT_INTENSITY_CVAR    CVAR_ENHANCED_SHADING("LUTIntensity")
#define POSTFX_LUT_INDEX_CVAR        CVAR_ENHANCED_SHADING("LUTIndex")
#define POSTFX_LUT_DEBUG_CVAR        CVAR_ENHANCED_SHADING("LUTDebug")

#define POSTFX_HDR_CVAR              CVAR_ENHANCED_SHADING("HDR")
#define POSTFX_HDR_EXPOSURE_CVAR     CVAR_ENHANCED_SHADING("HDRExposure")
#define POSTFX_HDR_CURVE_CVAR        CVAR_ENHANCED_SHADING("HDRCurve")
#define POSTFX_HDR_ADAPT_CVAR        CVAR_ENHANCED_SHADING("HDRAdaptation")
#define POSTFX_HDR_WHITE_CVAR        CVAR_ENHANCED_SHADING("HDRWhitePoint")
#define POSTFX_HDR_SHOULDER_CVAR     CVAR_ENHANCED_SHADING("HDRShoulder")
#define POSTFX_HDR_TOE_CVAR          CVAR_ENHANCED_SHADING("HDRToe")
#define POSTFX_HDR_CONTRAST_CVAR     CVAR_ENHANCED_SHADING("HDRContrast")

#define POSTFX_VOLUMETRIC_CVAR       CVAR_ENHANCED_SHADING("Volumetric")
#define POSTFX_VOL_DENSITY_CVAR      CVAR_ENHANCED_SHADING("VolDensity")
#define POSTFX_VOL_DECAY_CVAR        CVAR_ENHANCED_SHADING("VolDecay")
#define POSTFX_VOL_WEIGHT_CVAR       CVAR_ENHANCED_SHADING("VolWeight")
#define POSTFX_VOL_THRESH_CVAR       CVAR_ENHANCED_SHADING("VolThreshold")
#define POSTFX_VOL_SAMPLES_CVAR      CVAR_ENHANCED_SHADING("VolSamples")
#define POSTFX_VOL_LENGTH_CVAR       CVAR_ENHANCED_SHADING("VolMaxLength")

#define POSTFX_TAA_CVAR              CVAR_ENHANCED_SHADING("TAA")
#define POSTFX_TAA_JITTER_CVAR       CVAR_ENHANCED_SHADING("TAAJitter")
#define POSTFX_TAA_BLEND_CVAR        CVAR_ENHANCED_SHADING("TAABlend")
#define POSTFX_TAA_CLAMP_CVAR        CVAR_ENHANCED_SHADING("TAAClamp")

#define POSTFX_MOTIONBLUR_CVAR       CVAR_ENHANCED_SHADING("MotionBlur")
#define POSTFX_MB_STRENGTH_CVAR      CVAR_ENHANCED_SHADING("MBStrength")
#define POSTFX_MB_SAMPLES_CVAR       CVAR_ENHANCED_SHADING("MBSamples")
#define POSTFX_MB_CAMERA_CVAR        CVAR_ENHANCED_SHADING("MBCamera")
#define POSTFX_MB_VELSCALE_CVAR      CVAR_ENHANCED_SHADING("MBVelScale")
#define POSTFX_MB_MAXBLUR_CVAR       CVAR_ENHANCED_SHADING("MBMaxBlur")

#define POSTFX_PARALLAX_CVAR         CVAR_ENHANCED_SHADING("Parallax")
#define POSTFX_PARALLAX_SCALE_CVAR   CVAR_ENHANCED_SHADING("ParallaxScale")
#define POSTFX_PARALLAX_MINL_CVAR    CVAR_ENHANCED_SHADING("ParallaxMinLayers")
#define POSTFX_PARALLAX_MAXL_CVAR    CVAR_ENHANCED_SHADING("ParallaxMaxLayers")

#define POSTFX_VRS_CVAR              CVAR_ENHANCED_SHADING("VRS")
#define POSTFX_VRS_THRESH_CVAR       CVAR_ENHANCED_SHADING("VRSThreshold")
#define POSTFX_VRS_MINRATE_CVAR      CVAR_ENHANCED_SHADING("VRSMinRate")
#define POSTFX_VRS_MAXRATE_CVAR      CVAR_ENHANCED_SHADING("VRSMaxRate")

// Debug CVars for modern shaders
#define POSTFX_PBR_DEBUG_CVAR        CVAR_ENHANCED_SHADING("PBRDebug")
#define POSTFX_LUT_DEBUG_CVAR        CVAR_ENHANCED_SHADING("LUTDebug")
#define POSTFX_HDR_DEBUG_CVAR        CVAR_ENHANCED_SHADING("HDRDebug")
#define POSTFX_VOL_DEBUG_CVAR        CVAR_ENHANCED_SHADING("VolDebug")
#define POSTFX_TAA_DEBUG_CVAR        CVAR_ENHANCED_SHADING("TAADebug")
#define POSTFX_MB_DEBUG_CVAR         CVAR_ENHANCED_SHADING("MBDebug")
#define POSTFX_PARALLAX_DEBUG_CVAR   CVAR_ENHANCED_SHADING("ParallaxDebug")
#define POSTFX_VRS_DEBUG_CVAR        CVAR_ENHANCED_SHADING("VRSDebug")

static int PostFx_ShaderLogLevel() {
    return CVarGetInteger(POSTFX_SHADER_LOG_CVAR, 1);
}

// Stability / debounce state ---------------------------------------------------
static uint32_t sLastStableFrame = 0;
static bool sStable = true;

// Track desired pass state for debouncing (only register/unregister when it
// actually changes, not every frame).
static bool sDesiredSsao = false;
static bool sDesiredDepthDebug = false;
static bool sDesiredSunShade = false;
static bool sDesiredGodRays = false;
static bool sDesiredContactShadow = false;
static bool sDesiredScreenSpaceShadow = false;
static bool sDesiredBloom = false;
static bool sDesiredFxaa = false;
static bool sDesiredSpecular = false;
static bool sFirstFrame = true;

// Area type auto-detection -----------------------------------------------------
enum AreaType {
    AREA_OUTDOOR = 0,
    AREA_INDOOR  = 1,
    AREA_UNDERGROUND = 2,
    AREA_UNKNOWN = 3
};
static AreaType sCurrentAreaType = AREA_UNKNOWN;
static bool sIsIndoor = false;  // Track indoor state internally (no CVar changes)
static const char* sPrevAreaName = nullptr;

// Preset handling --------------------------------------------------------------
struct ShadingPreset {
    const char* name;
    // In-shader
    bool engineLighting;
    float engineLightingWrap;
    float engineLightingGround;
    float engineLightingBoost;
    bool modernLook;
    float modernSat;
    float modernCon;
    float modernBri;
    bool areaAmbient;
    float areaAmbientStrength;
    bool fragmentLighting;
    float fragmentLightingStrength;
    float fragmentLightingWrap;
    float sunAzimuth;
    float sunElevation;
    float sunIntensity;
    float sunWarmth;
    float sunAmbient;
    float sunExposure;
    bool hemisphere;
    bool rim;
    float rimIntensity;
    float relightStrength;
    float relightFloor;
    bool tonemap;
    float tonemapExposure;
    int tonemapCurve;
    float tonemapBlend;
    // Post passes (original)
    bool ssao;
    float ssaoStrength;
    float ssaoRadius;
    float ssaoBias;
    bool sunshade;
    float sunshadeStrength;
    bool godrays;
    float godraysIntensity;
    float godraysDensity;
    float godraysDecay;
    float godraysWeight;
    float godraysThreshold;
    bool contactShadow;
    float contactShadowStrength;
    float contactShadowLength;
    float contactShadowThickness;
    bool screenSpaceShadow;
    float screenSpaceShadowStrength;
    float screenSpaceShadowLength;
    float screenSpaceShadowThickness;
    float screenSpaceShadowBias;
    bool bloom;
    float bloomIntensity;
    float bloomThreshold;
    float bloomRadius;
    bool fxaa;
    float fxaaSubpixel;
    float fxaaEdgeThreshold;
    float fxaaEdgeThresholdMin;
    bool specular;
    float specularStrength;
    float specularShininess;
    // Modern shader suite
    bool pbr;
    float pbrEnvIntensity;
    bool lut;
    float lutIntensity;
    int lutIndex;
    bool hdr;
    float hdrExposure;
    int hdrCurve;
    float hdrAdaptation;
    bool volumetric;
    float volDensity;
    float volDecay;
    float volWeight;
    float volThreshold;
    int volSamples;
    float volMaxLength;
    bool taa;
    float taaJitter;
    float taaBlend;
    float taaClamp;
    bool motionBlur;
    float mbStrength;
    int mbSamples;
    bool mbCamera;
    bool parallax;
    float parallaxScale;
    int parallaxMinLayers;
    int parallaxMaxLayers;
    bool vrs;
    float vrsThreshold;
    float vrsMinRate;
    float vrsMaxRate;
};

static void ApplyPreset(const ShadingPreset& p) {
    // In-shader
    CVarSetInteger(POSTFX_ENGINE_LIGHTING_CVAR, p.engineLighting ? 1 : 0);
    CVarSetFloat(POSTFX_ENGINE_LIGHTING_WRAP_CVAR, p.engineLightingWrap);
    CVarSetFloat(POSTFX_ENGINE_LIGHTING_GROUND_CVAR, p.engineLightingGround);
    CVarSetFloat(POSTFX_ENGINE_LIGHTING_BOOST_CVAR, p.engineLightingBoost);
    CVarSetInteger(POSTFX_MODERN_CVAR, p.modernLook ? 1 : 0);
    CVarSetFloat(POSTFX_MODERN_SAT_CVAR, p.modernSat);
    CVarSetFloat(POSTFX_MODERN_CON_CVAR, p.modernCon);
    CVarSetFloat(POSTFX_MODERN_BRI_CVAR, p.modernBri);
    CVarSetInteger(POSTFX_AMBIENT_CVAR, p.areaAmbient ? 1 : 0);
    CVarSetFloat(POSTFX_AMBIENT_STRENGTH_CVAR, p.areaAmbientStrength);
    CVarSetInteger(POSTFX_FRAGLIGHT_CVAR, p.fragmentLighting ? 1 : 0);
    CVarSetFloat(POSTFX_FRAGLIGHT_STR_CVAR, p.fragmentLightingStrength);
    CVarSetFloat(POSTFX_FRAGLIGHT_WRAP_CVAR, p.fragmentLightingWrap);
    CVarSetFloat(POSTFX_SUN_AZIMUTH_CVAR, p.sunAzimuth);
    CVarSetFloat(POSTFX_SUN_ELEVATION_CVAR, p.sunElevation);
    CVarSetFloat(POSTFX_SUN_INTENSITY_CVAR, p.sunIntensity);
    CVarSetFloat(POSTFX_SUN_WARMTH_CVAR, p.sunWarmth);
    CVarSetFloat(POSTFX_SUN_AMBIENT_CVAR, p.sunAmbient);
    CVarSetFloat(POSTFX_SUN_EXPOSURE_CVAR, p.sunExposure);
    CVarSetInteger(POSTFX_HEMISPHERE_CVAR, p.hemisphere ? 1 : 0);
    CVarSetInteger(POSTFX_RIM_CVAR, p.rim ? 1 : 0);
    CVarSetFloat(POSTFX_RIM_INTENSITY_CVAR, p.rimIntensity);
    CVarSetFloat(POSTFX_RELIGHT_STR_CVAR, p.relightStrength);
    CVarSetFloat(POSTFX_RELIGHT_FLOOR_CVAR, p.relightFloor);
    CVarSetInteger(POSTFX_TONEMAP_CVAR, p.tonemap ? 1 : 0);
    CVarSetFloat(POSTFX_TONEMAP_EXPOSURE_CVAR, p.tonemapExposure);
    CVarSetInteger(POSTFX_TONEMAP_CURVE_CVAR, p.tonemapCurve);
    CVarSetFloat(POSTFX_TONEMAP_BLEND_CVAR, p.tonemapBlend);
    // Post passes
    CVarSetInteger(POSTFX_SSAO_CVAR, p.ssao ? 1 : 0);
    CVarSetFloat(POSTFX_SSAO_STR_CVAR, p.ssaoStrength);
    CVarSetFloat(POSTFX_SSAO_RADIUS_CVAR, p.ssaoRadius);
    CVarSetFloat(POSTFX_SSAO_BIAS_CVAR, p.ssaoBias);
    CVarSetInteger(POSTFX_SUNSHADE_CVAR, p.sunshade ? 1 : 0);
    CVarSetFloat(POSTFX_SUNSHADE_STR_CVAR, p.sunshadeStrength);
    CVarSetInteger(POSTFX_GODRAYS_CVAR, p.godrays ? 1 : 0);
    CVarSetFloat(POSTFX_GODRAYS_INTENSITY_CVAR, p.godraysIntensity);
    CVarSetFloat(POSTFX_GODRAYS_DENSITY_CVAR, p.godraysDensity);
    CVarSetFloat(POSTFX_GODRAYS_DECAY_CVAR, p.godraysDecay);
    CVarSetFloat(POSTFX_GODRAYS_WEIGHT_CVAR, p.godraysWeight);
    CVarSetFloat(POSTFX_GODRAYS_THRESHOLD_CVAR, p.godraysThreshold);
    CVarSetInteger(POSTFX_CONTACT_CVAR, p.contactShadow ? 1 : 0);
    CVarSetFloat(POSTFX_CONTACT_STR_CVAR, p.contactShadowStrength);
    CVarSetFloat(POSTFX_CONTACT_LEN_CVAR, p.contactShadowLength);
    CVarSetFloat(POSTFX_CONTACT_THICK_CVAR, p.contactShadowThickness);
    CVarSetInteger(POSTFX_SSSHADOW_CVAR, p.screenSpaceShadow ? 1 : 0);
    CVarSetFloat(POSTFX_SSSHADOW_STR_CVAR, p.screenSpaceShadowStrength);
    CVarSetFloat(POSTFX_SSSHADOW_LEN_CVAR, p.screenSpaceShadowLength);
    CVarSetFloat(POSTFX_SSSHADOW_THICK_CVAR, p.screenSpaceShadowThickness);
    CVarSetFloat(POSTFX_SSSHADOW_BIAS_CVAR, p.screenSpaceShadowBias);
    CVarSetInteger(POSTFX_BLOOM_CVAR, p.bloom ? 1 : 0);
    CVarSetFloat(POSTFX_BLOOM_INT_CVAR, p.bloomIntensity);
    CVarSetFloat(POSTFX_BLOOM_THRESH_CVAR, p.bloomThreshold);
    CVarSetFloat(POSTFX_BLOOM_RADIUS_CVAR, p.bloomRadius);
    CVarSetInteger(POSTFX_FXAA_CVAR, p.fxaa ? 1 : 0);
    CVarSetFloat(POSTFX_FXAA_SUBPIX_CVAR, p.fxaaSubpixel);
    CVarSetFloat(POSTFX_FXAA_EDGE_CVAR, p.fxaaEdgeThreshold);
    CVarSetFloat(POSTFX_FXAA_EDGEMIN_CVAR, p.fxaaEdgeThresholdMin);
    CVarSetInteger(POSTFX_SPECULAR_CVAR, p.specular ? 1 : 0);
    CVarSetFloat(POSTFX_SPECULAR_STR_CVAR, p.specularStrength);
    CVarSetFloat(POSTFX_SPECULAR_SHINY_CVAR, p.specularShininess);
    // Modern shader suite
    CVarSetInteger(POSTFX_PBR_CVAR, p.pbr ? 1 : 0);
    CVarSetFloat(POSTFX_PBR_ENV_INT_CVAR, p.pbrEnvIntensity);
    CVarSetInteger(POSTFX_LUT_CVAR, p.lut ? 1 : 0);
    CVarSetFloat(POSTFX_LUT_INTENSITY_CVAR, p.lutIntensity);
    CVarSetInteger(POSTFX_LUT_INDEX_CVAR, p.lutIndex);
    CVarSetInteger(POSTFX_HDR_CVAR, p.hdr ? 1 : 0);
    CVarSetFloat(POSTFX_HDR_EXPOSURE_CVAR, p.hdrExposure);
    CVarSetInteger(POSTFX_HDR_CURVE_CVAR, p.hdrCurve);
    CVarSetFloat(POSTFX_HDR_ADAPT_CVAR, p.hdrAdaptation);
    CVarSetInteger(POSTFX_VOLUMETRIC_CVAR, p.volumetric ? 1 : 0);
    CVarSetFloat(POSTFX_VOL_DENSITY_CVAR, p.volDensity);
    CVarSetFloat(POSTFX_VOL_DECAY_CVAR, p.volDecay);
    CVarSetFloat(POSTFX_VOL_WEIGHT_CVAR, p.volWeight);
    CVarSetFloat(POSTFX_VOL_THRESH_CVAR, p.volThreshold);
    CVarSetInteger(POSTFX_VOL_SAMPLES_CVAR, p.volSamples);
    CVarSetFloat(POSTFX_VOL_LENGTH_CVAR, p.volMaxLength);
    CVarSetInteger(POSTFX_TAA_CVAR, p.taa ? 1 : 0);
    CVarSetFloat(POSTFX_TAA_JITTER_CVAR, p.taaJitter);
    CVarSetFloat(POSTFX_TAA_BLEND_CVAR, p.taaBlend);
    CVarSetFloat(POSTFX_TAA_CLAMP_CVAR, p.taaClamp);
    CVarSetInteger(POSTFX_MOTIONBLUR_CVAR, p.motionBlur ? 1 : 0);
    CVarSetFloat(POSTFX_MB_STRENGTH_CVAR, p.mbStrength);
    CVarSetInteger(POSTFX_MB_SAMPLES_CVAR, p.mbSamples);
    CVarSetInteger(POSTFX_MB_CAMERA_CVAR, p.mbCamera ? 1 : 0);
    CVarSetInteger(POSTFX_PARALLAX_CVAR, p.parallax ? 1 : 0);
    CVarSetFloat(POSTFX_PARALLAX_SCALE_CVAR, p.parallaxScale);
    CVarSetInteger(POSTFX_PARALLAX_MINL_CVAR, p.parallaxMinLayers);
    CVarSetInteger(POSTFX_PARALLAX_MAXL_CVAR, p.parallaxMaxLayers);
    CVarSetInteger(POSTFX_VRS_CVAR, p.vrs ? 1 : 0);
    CVarSetFloat(POSTFX_VRS_THRESH_CVAR, p.vrsThreshold);
    CVarSetFloat(POSTFX_VRS_MINRATE_CVAR, p.vrsMinRate);
    CVarSetFloat(POSTFX_VRS_MAXRATE_CVAR, p.vrsMaxRate);
}

// Built-in presets -------------------------------------------------------------
static const ShadingPreset sPresets[] = {
    // "Balanced" - the recommended default: good visuals, stable, works everywhere
    {
        "Balanced",
        // In-shader
        true, 0.5f, 0.45f, 1.1f,   // engine lighting
        true, 1.1f, 1.05f, 1.0f,   // modern look
        true, 0.6f,                // area ambient
        true, 0.6f, 0.35f,         // fragment lighting
        45.0f, 55.0f, 0.45f,       // sun azimuth, elevation, intensity
        0.45f, 0.80f, 1.0f,        // sun warmth, ambient, exposure
        true, true, 1.0f,          // hemisphere, rim, rim intensity
        0.85f, 0.15f,              // relight
        true, 0.9f, 0, 1.0f,       // tonemap
        // Post passes
        true, 1.0f, 10.0f, 0.0004f, // SSAO
        true, 0.7f,                // sunshade
        true, 0.6f, 0.8f, 0.96f, 0.5f, 0.65f, // god rays
        true, 0.6f, 48.0f, 4.0f,   // contact shadow
        true, 0.7f, 96.0f, 8.0f, 0.05f, // screen-space shadow
        true, 0.6f, 0.75f, 12.0f,  // bloom
        true, 0.75f, 0.125f, 0.0312f, // FXAA
        true, 0.5f, 32.0f,         // specular
        // Modern shader suite defaults (off)
        false, 0.0f,          // pbr
        false, 0.0f, 0,       // lut
        false, 0.0f, 0, 0.0f, // hdr
        false, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, // volumetric
        false, 0.0f, 0.0f, 0.0f, // taa
        false, 0.0f, 0, false, // motion blur
        false, 0.0f, 0, 0,     // parallax
        false, 0.0f, 0.0f, 0.0f, // vrs
    },
    // "Performance" - minimal post passes, maximum compatibility
    {
        "Performance",
        // In-shader
        true, 0.5f, 0.45f, 1.1f,
        true, 1.05f, 1.02f, 1.0f,
        true, 0.4f,
        true, 0.5f, 0.3f,
        45.0f, 50.0f, 0.4f,
        0.3f, 0.7f, 1.0f,
        true, false, 0.0f,
        0.5f, 0.1f,
        true, 0.85f, 0, 1.0f,
        // Post passes (only cheap ones)
        false, 0, 0, 0,            // no SSAO
        false, 0,                  // no sunshade
        false, 0, 0, 0, 0, 0,      // no god rays
        false, 0, 0, 0,            // no contact shadow
        false, 0, 0, 0, 0,         // no screen-space shadow
        false, 0, 0, 0,            // no bloom
        true, 0.5f, 0.15f, 0.05f,  // FXAA only (light)
        false, 0, 0,               // no specular
        // Modern shader suite defaults (off)
        false, 0.0f,          // pbr
        false, 0.0f, 0,       // lut
        false, 0.0f, 0, 0.0f, // hdr
        false, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, // volumetric
        false, 0.0f, 0.0f, 0.0f, // taa
        false, 0.0f, 0, false, // motion blur
        false, 0.0f, 0, 0,     // parallax
        false, 0.0f, 0.0f, 0.0f, // vrs
    },
    // "Cinematic" - full effects, best for screenshots/video
    {
        "Cinematic",
        // In-shader
        true, 0.55f, 0.5f, 1.15f,
        true, 1.15f, 1.08f, 1.02f,
        true, 0.7f,
        true, 0.7f, 0.4f,
        30.0f, 40.0f, 0.55f,
        0.55f, 0.85f, 1.05f,
        true, true, 1.2f,
        0.9f, 0.2f,
        true, 1.0f, 1, 1.0f,
        // Post passes (all on, slightly stronger)
        true, 1.2f, 12.0f, 0.0005f, // SSAO
        true, 0.8f,                // sunshade
        true, 0.8f, 1.0f, 0.95f, 0.6f, 0.7f, // god rays
        true, 0.7f, 64.0f, 6.0f,   // contact shadow
        true, 0.8f, 128.0f, 10.0f, 0.08f, // screen-space shadow
        true, 0.8f, 0.7f, 16.0f,   // bloom
        true, 0.85f, 0.1f, 0.02f,  // FXAA
        true, 0.6f, 48.0f,         // specular
        // Modern shader suite defaults (off)
        false, 0.0f,          // pbr
        false, 0.0f, 0,       // lut
        false, 0.0f, 0, 0.0f, // hdr
        false, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, // volumetric
        false, 0.0f, 0.0f, 0.0f, // taa
        false, 0.0f, 0, false, // motion blur
        false, 0.0f, 0, 0,     // parallax
        false, 0.0f, 0.0f, 0.0f, // vrs
    },
    // "Retro" - close to vanilla N64 look, just slight polish
    {
        "Retro",
        // In-shader (subtle)
        true, 0.45f, 0.4f, 1.0f,
        false, 1.0f, 1.0f, 1.0f,
        false, 0,
        false, 0, 0,
        45.0f, 60.0f, 0.35f,
        0.2f, 0.6f, 0.95f,
        false, false, 0,
        0.4f, 0.0f,
        false, 0, 0, 1.0f,
        // Post passes (minimal)
        false, 0, 0, 0,
        false, 0,
        false, 0, 0, 0, 0, 0,
        false, 0, 0, 0,
        false, 0, 0, 0, 0,
        false, 0, 0, 0,
        false, 0, 0, 0,
        false, 0, 0,
        // Modern shader suite defaults (off)
        false, 0.0f,          // pbr
        false, 0.0f, 0,       // lut
        false, 0.0f, 0, 0.0f, // hdr
        false, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, // volumetric
        false, 0.0f, 0.0f, 0.0f, // taa
        false, 0.0f, 0, false, // motion blur
        false, 0.0f, 0, 0,     // parallax
        false, 0.0f, 0.0f, 0.0f, // vrs
    },
    // "Indoor/Underground" - optimized for caves, houses, shops (no sun)
    {
        "Indoor/Underground",
        // In-shader (no sun, more ambient)
        true, 0.6f, 0.55f, 1.2f,
        true, 1.05f, 1.0f, 1.0f,
        true, 0.8f,
        true, 0.5f, 0.5f,
        0.0f, -10.0f, 0.2f,  // sun below horizon, weak
        0.1f, 0.9f, 0.95f,
        true, false, 0,
        0.4f, 0.1f,
        true, 0.8f, 0, 1.0f,
        // Post passes (depth-based only, no sun-dependent)
        true, 0.8f, 8.0f, 0.0003f, // SSAO
        false, 0,                 // no sunshade
        false, 0, 0, 0, 0, 0,    // no god rays
        true, 0.5f, 32.0f, 3.0f, // contact shadow (short)
        false, 0, 0, 0, 0,       // no screen-space shadow
        false, 0, 0, 0,          // no bloom
        true, 0.6f, 0.12f, 0.03f, // FXAA
        false, 0, 0,              // no specular
        // Modern shader suite defaults (off)
        false, 0.0f,          // pbr
        false, 0.0f, 0,       // lut
        false, 0.0f, 0, 0.0f, // hdr
        false, 0.0f, 0.0f, 0.0f, 0.0f, 0, 0.0f, // volumetric
        false, 0.0f, 0.0f, 0.0f, // taa
        false, 0.0f, 0, false, // motion blur
        false, 0.0f, 0, 0,     // parallax
        false, 0.0f, 0.0f, 0.0f, // vrs
    }
};
static const int sNumPresets = sizeof(sPresets) / sizeof(sPresets[0]);

// Apply preset by index (0 = Balanced, 1 = Performance, 2 = Cinematic, 3 = Retro, 4 = Indoor)
extern "C" void PostFx_ApplyPreset(int idx) {
    if (idx < 0 || idx >= sNumPresets) {
        idx = 0;
    }
    ApplyPreset(sPresets[idx]);
    SPDLOG_INFO("[PostFx] applied preset: {}", sPresets[idx].name);
}

// Area type detection ----------------------------------------------------------
extern "C" AreaType DetectAreaType(const char* areaName) {
    if (areaName == nullptr) {
        return AREA_UNKNOWN;
    }
    // Indoor keywords
    const char* indoorKeywords[] = {
        "house", "shop", "inn", "museum", "library", "office", "room",
        "hall", "chamber", "cellar", "basement", "attic", "closet",
        "kitchen", "bedroom", "bathroom", "living", "dining", "study",
        "toy", "post", "bank", "cafe", "bar", "restaurant", "hotel",
        "mayor", "gate", "guard", "jail", "prison", "dungeon"
    };
    // Underground keywords
    const char* undergroundKeywords[] = {
        "cave", "cavern", "mine", "tunnel", "sewer", "catacomb",
        "ruin", "temple", "tomb", "crypt", "underground", "subterrain",
        "lava", "volcan", "crystal", "gem", "ore", "depth",
        "mim_", "dro_", "tubba_", "gloom", "shyguy_", "bow_",
        "jr_", "koopa_", "mt_", "vulcan", "flower_", "tree_",
        "wind_", "cloud_", "star_", "peach_", "toad_", "yoshi_"
    };
    // Check for underground first (more specific)
    for (const char* kw : undergroundKeywords) {
        if (std::strstr(areaName, kw) != nullptr) {
            return AREA_UNDERGROUND;
        }
    }
    // Check for indoor
    for (const char* kw : indoorKeywords) {
        if (std::strstr(areaName, kw) != nullptr) {
            return AREA_INDOOR;
        }
    }
    // Default to outdoor
    return AREA_OUTDOOR;
}

extern "C" void UpdateAreaType() {
    const char* area = EnhancedShadingBridge_CurrentAreaName();
    if (area != sPrevAreaName) {
        sPrevAreaName = area;
        sCurrentAreaType = DetectAreaType(area);
        SPDLOG_INFO("[PostFx] area='{}' type={}", area ? area : "null", (int)sCurrentAreaType);
    }
}

// Stability helpers ------------------------------------------------------------
// Returns true if the game is in a stable gameplay state (not loading, not
// transitioning alt assets, not in a soft reset, window has valid size).
// Uses C linkage so it can be called from extern "C" functions.
extern "C" bool IsStableFrame() {
    // Don't run post FX during soft reset or when draw frame is disabled
    extern s32 gOverrideFlags;
    // GLOBAL_OVERRIDES_DISABLE_DRAW_FRAME = 0x00000008
    // GLOBAL_OVERRIDES_SOFT_RESET = 0x00000020
    if (gOverrideFlags & (0x00000008 | 0x00000020)) {
        return false;
    }
    // Check window dimensions (avoid alt-tab / minimize 0-size)
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return false;
    }
    // Access native dimensions through the interpreter
    // (we can't directly access private members, but we can check if renderer is ready)
    return true;
}

// Hands the engine's lighting parameters to the reserved uCustom registers every
// frame. Register 8 = (enabled, wrap, groundScale, ambientBoost); register 11 =
// tint. x == 0 keeps the vanilla N64 lighting path in the shader.
static void PostFx_PushEngineLighting() {
    bool on = CVarGetInteger(POSTFX_ENGINE_LIGHTING_CVAR, 0) != 0;
    float params[4] = {
        on ? 1.0f : 0.0f,
        CVarGetFloat(POSTFX_ENGINE_LIGHTING_WRAP_CVAR, 0.5f),
        CVarGetFloat(POSTFX_ENGINE_LIGHTING_GROUND_CVAR, 0.45f),
        CVarGetFloat(POSTFX_ENGINE_LIGHTING_BOOST_CVAR, 1.1f),
    };
    float tint[4] = { 1.0f, 1.0f, 1.0f, 0.0f };
    gfx_set_custom_uniform(8, params);
    gfx_set_custom_uniform(11, tint);
}

// Whole-world colour grade applied inside the main DirectX 11 shader (register 9
// = (saturation, contrast, brightness); x == 0 leaves the image unchanged).
static void PostFx_PushModernLook() {
    bool on = pbEffectOn(POSTFX_MODERN_CVAR, 1);
    float grade[4] = {
        on ? 1.0f : 0.0f,
        CVarGetFloat(POSTFX_MODERN_SAT_CVAR, 1.1f),
        CVarGetFloat(POSTFX_MODERN_CON_CVAR, 1.05f),
        CVarGetFloat(POSTFX_MODERN_BRI_CVAR, 1.0f),
    };
    gfx_set_custom_uniform(9, grade);
}

// Per-area ambient grade: turns the current map's background-palette average into
// a small multiplicative tint (kept near 1.0 so it reads as a grade, not a wash)
// and hands it to the shader through register 10.
static void PostFx_PushAreaAmbient() {
    bool on = pbEffectOn(POSTFX_AMBIENT_CVAR, 1);
    float strength = on ? CVarGetFloat(POSTFX_AMBIENT_STRENGTH_CVAR, 0.6f) : 0.0f;
    float tint[4] = { 1.0f, 1.0f, 1.0f, 0.0f };

    if (on && strength > 0.0f) {
        float r = 0.5f;
        float g = 0.5f;
        float b = 0.5f;
        port_get_area_ambient(&r, &g, &b);

        float luma = 0.299f * r + 0.587f * g + 0.114f * b;
        if (luma < 0.02f) {
            r = g = b = 0.5f;
            luma = 0.5f;
        }

        // Preserve the palette's hue but fold its brightness in, so dark areas
        // (caves, night) darken the scene and bright ones gently lift it.
        float bright = pbClampf(luma / 0.5f, 0.6f, 1.25f);
        tint[0] = pbClampf(r / luma * bright, 0.5f, 1.5f);
        tint[1] = pbClampf(g / luma * bright, 0.5f, 1.5f);
        tint[2] = pbClampf(b / luma * bright, 0.5f, 1.5f);
        tint[3] = strength;
    }

    gfx_set_custom_uniform(10, tint);
}

// Per-fragment lighting: derives a per-area sun direction, a sky/ground hemisphere
// (hued toward the area's palette but kept near white so it reads as a vertical
// light gradient rather than a second tint) and an optional rim, then hands them
// to the shader through registers 12..16. Register 15.x == 0 keeps it a no-op.
static void PostFx_PushFragmentLighting() {
    const bool on = pbEffectOn(POSTFX_FRAGLIGHT_CVAR, 1);
    const bool hemiOn = CVarGetInteger(POSTFX_HEMISPHERE_CVAR, 1) != 0;
    const bool rimOn = CVarGetInteger(POSTFX_RIM_CVAR, 0) != 0;

    float sun[4];
    const float az = CVarGetFloat(POSTFX_SUN_AZIMUTH_CVAR, 45.0f) * 3.14159265f / 180.0f;
    const float el = pbClampf(CVarGetFloat(POSTFX_SUN_ELEVATION_CVAR, 55.0f), -85.0f, 89.0f) * 3.14159265f / 180.0f;
    sun[0] = cosf(el) * sinf(az);
    sun[1] = sinf(el);
    sun[2] = cosf(el) * cosf(az);
    sun[3] = CVarGetFloat(POSTFX_SUN_INTENSITY_CVAR, 0.45f);

    float r = 0.5f;
    float g = 0.5f;
    float b = 0.5f;
    port_get_area_ambient(&r, &g, &b);
    const float invLuma = (0.299f * r + 0.587f * g + 0.114f * b) > 0.02f
                              ? 1.0f / (0.299f * r + 0.587f * g + 0.114f * b)
                              : 2.0f;
    const float hr = pbClampf(r * invLuma, 0.5f, 1.5f);
    const float hg = pbClampf(g * invLuma, 0.5f, 1.5f);
    const float hb = pbClampf(b * invLuma, 0.5f, 1.5f);
    const float hueMix = hemiOn ? 0.4f : 0.0f;
    const float skyLevel = hemiOn ? 0.85f : 1.0f;
    const float groundLevel = hemiOn ? 0.40f : 1.0f;

    float sky[4] = { (1.0f + (hr - 1.0f) * hueMix) * skyLevel,
                     (1.0f + (hg - 1.0f) * hueMix) * skyLevel,
                     (1.0f + (hb - 1.0f) * hueMix) * skyLevel, 1.0f };
    float ground[4] = { (1.0f + (hr - 1.0f) * hueMix) * groundLevel,
                        (1.0f + (hg - 1.0f) * hueMix) * groundLevel,
                        (1.0f + (hb - 1.0f) * hueMix) * groundLevel, 1.0f };

    float light[4] = { on ? 1.0f : 0.0f,
                       CVarGetFloat(POSTFX_FRAGLIGHT_STR_CVAR, 0.6f),
                       CVarGetFloat(POSTFX_FRAGLIGHT_WRAP_CVAR, 0.35f),
                       CVarGetInteger(POSTFX_FRAGLIGHT_FLIP_CVAR, 0) ? -1.0f : 1.0f };

    float rim[4] = { 0.0f, 3.0f, 0.6f, 0.0f };
    if (rimOn) {
        rim[0] = pbClampf(CVarGetFloat(POSTFX_RIM_INTENSITY_CVAR, 1.0f), 0.0f, 4.0f);
    }

    gfx_set_custom_uniform(12, sun);
    gfx_set_custom_uniform(13, sky);
    gfx_set_custom_uniform(14, ground);
    gfx_set_custom_uniform(15, light);
    gfx_set_custom_uniform(16, rim);
}

// Sunlight art-direction: a warm sun colour, a shadow-deepening ambient level and
// a whole-scene exposure handed to the shader through registers 21 and 22. Only
// affects the per-fragment light, so it is a no-op while that is disabled.
static void PostFx_PushSunlight() {
    const float warmth = pbClampf(CVarGetFloat(POSTFX_SUN_WARMTH_CVAR, 0.45f), 0.0f, 1.0f);

    // Warm the sun toward a low-sun amber as warmth rises (white at 0).
    float sun[4] = {
        1.0f,
        1.0f - 0.16f * warmth,
        1.0f - 0.40f * warmth,
        pbClampf(CVarGetFloat(POSTFX_SUN_AMBIENT_CVAR, 0.80f), 0.05f, 1.50f),
    };

    float opts[4] = {
        pbClampf(CVarGetFloat(POSTFX_SUN_EXPOSURE_CVAR, 1.0f), 0.10f, 2.0f),
        0.0f,
        (float)CVarGetInteger(POSTFX_SUN_DEBUG_CVAR, 0),
        0.0f,
    };

    gfx_set_custom_uniform(21, sun);
    gfx_set_custom_uniform(22, opts);
}

// Albedo-recovery relight (register 17) and filmic tonemap (register 18). Relight
// reuses the fragment light from registers 12..16, so it only has an effect while
// the fragment light is active. Tonemap needs no other state.
static void PostFx_PushRelightAndTonemap() {
    const bool relightOn = pbEffectOn(POSTFX_RELIGHT_CVAR, 1);
    float relight[4] = { relightOn ? 1.0f : 0.0f,
                         pbClampf(CVarGetFloat(POSTFX_RELIGHT_STR_CVAR, 0.85f), 0.0f, 1.0f),
                         pbClampf(CVarGetFloat(POSTFX_RELIGHT_FLOOR_CVAR, 0.15f), 0.02f, 1.0f), 0.0f };

    const bool tonemapOn = pbEffectOn(POSTFX_TONEMAP_CVAR, 1);
    float tonemap[4] = { tonemapOn ? 1.0f : 0.0f,
                         pbClampf(CVarGetFloat(POSTFX_TONEMAP_EXPOSURE_CVAR, 0.9f), 0.1f, 4.0f),
                         CVarGetInteger(POSTFX_TONEMAP_CURVE_CVAR, 0) != 0 ? 1.0f : 0.0f,
                         pbClampf(CVarGetFloat(POSTFX_TONEMAP_BLEND_CVAR, 1.0f), 0.0f, 1.0f) };

    gfx_set_custom_uniform(17, relight);
    gfx_set_custom_uniform(18, tonemap);
}

// Depth-debug post pass. Registers a fullscreen pass that samples the scene
// depth buffer the engine exposes on tile 1 during post-processing, and removes
// it again when the CVar is cleared. The pass id is tracked so we register once.
static int sDepthDebugPassId = -1;

static void PostFx_UpdateDepthDebug() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_DEPTH_DEBUG_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sDepthDebugPassId < 0) {
        sDepthDebugPassId = interp->RegisterPostPass("shaders/post/pb_depthdebug.shader");
        SPDLOG_INFO("[PostFx] depth-debug pass registered id={}", sDepthDebugPassId);
    } else if (!on && sDepthDebugPassId >= 0) {
        interp->UnregisterPostPass(sDepthDebugPassId);
        sDepthDebugPassId = -1;
        SPDLOG_INFO("[PostFx] depth-debug pass removed");
    }
}

// Screen-space ambient occlusion post pass. Hands the current tuning to register
// 19 and registers the pass while the CVar is set. The pass id is tracked so the
// pass is registered exactly once.
static int sSsaoPassId = -1;

static void PostFx_PushSsao() {
    const bool on = CVarGetInteger(POSTFX_SSAO_CVAR, 0) != 0;
    float params[4] = {
        on ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_SSAO_STR_CVAR, 1.0f), 0.0f, 3.0f),
        pbClampf(CVarGetFloat(POSTFX_SSAO_RADIUS_CVAR, 10.0f), 1.0f, 48.0f),
        pbClampf(CVarGetFloat(POSTFX_SSAO_BIAS_CVAR, 0.0004f), 0.0f, 0.02f),
    };
    float dbg[4] = { CVarGetInteger(POSTFX_SSAO_DEBUG_CVAR, 0) != 0 ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
    gfx_set_custom_uniform(19, params);
    gfx_set_custom_uniform(20, dbg);
}

static void PostFx_UpdateSsao() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_SSAO_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sSsaoPassId < 0) {
        sSsaoPassId = interp->RegisterPostPass("shaders/post/pb_ssao.shader");
        SPDLOG_INFO("[PostFx] ssao pass registered id={}", sSsaoPassId);
    } else if (!on && sSsaoPassId >= 0) {
        interp->UnregisterPostPass(sSsaoPassId);
        sSsaoPassId = -1;
        SPDLOG_INFO("[PostFx] ssao pass removed");
    }
}

// Depth-derived sun shading post pass. Reuses the sun/hemisphere uniforms
// already written for the in-shader fragment light (registers 12..14, 21, 22)
// and the view-projection matrix the engine publishes (23..27); only its own
// enable/strength registers are written here.
static int sSunShadePassId = -1;

// 4x4 matrix inverse (for view-projection matrix)
static bool Mat4Inverse(const float m[4][4], float out[4][4]) {
    // Simple Gauss-Jordan elimination
    float aug[4][8];
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            aug[i][j] = m[i][j];
            aug[i][j + 4] = (i == j) ? 1.0f : 0.0f;
        }
    }
    for (int col = 0; col < 4; ++col) {
        // Find pivot
        int pivot = col;
        float maxVal = fabsf(aug[pivot][col]);
        for (int row = col + 1; row < 4; ++row) {
            float val = fabsf(aug[row][col]);
            if (val > maxVal) {
                maxVal = val;
                pivot = row;
            }
        }
        if (maxVal < 1e-6f) return false; // Singular
        if (pivot != col) {
            for (int j = 0; j < 8; ++j) {
                float tmp = aug[col][j];
                aug[col][j] = aug[pivot][j];
                aug[pivot][j] = tmp;
            }
        }
        // Normalize pivot row
        float pivVal = aug[col][col];
        for (int j = 0; j < 8; ++j) aug[col][j] /= pivVal;
        // Eliminate other rows
        for (int row = 0; row < 4; ++row) {
            if (row == col) continue;
            float factor = aug[row][col];
            for (int j = 0; j < 8; ++j) {
                aug[row][j] -= factor * aug[col][j];
            }
        }
    }
    for (int i = 0; i < 4; ++i) {
        for (int j = 0; j < 4; ++j) {
            out[i][j] = aug[i][j + 4];
        }
    }
    return true;
}

static void PostFx_PushSunShade() {
    const bool on = CVarGetInteger(POSTFX_SUNSHADE_CVAR, 0) != 0 && !sIsIndoor;
    float params[4] = {
        on ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_SUNSHADE_STR_CVAR, 0.7f), 0.0f, 1.0f),
        CVarGetFloat(POSTFX_FRAGLIGHT_WRAP_CVAR, 0.35f),
        0.0f,
    };
    float dbg[4] = { CVarGetInteger(POSTFX_SUNSHADE_DEBUG_CVAR, 0) != 0 ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f };
    gfx_set_custom_uniform(28, params);
    gfx_set_custom_uniform(29, dbg);

    if (!on) return;

    // Get camera view-projection matrix from CameraPatches
    float vp[4][4];
    extern void gfx_get_camera_view_projection(float out[4][4]);
    gfx_get_camera_view_projection(vp);

    // Compute inverse VP for world position reconstruction
    float invVP[4][4];
    if (Mat4Inverse(vp, invVP)) {
        // Pass inverse VP rows (registers 23-26)
        for (int i = 0; i < 4; ++i) {
            gfx_set_custom_uniform(23 + i, invVP[i]);
        }
    }

    // Get camera world position (from CameraPatches via interpreter)
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp != nullptr) {
        float camPos[4] = { interp->mCameraWorldPos[0], interp->mCameraWorldPos[1], interp->mCameraWorldPos[2], 0.0f };
        gfx_set_custom_uniform(27, camPos);
    }

    // Pass sun direction + intensity (register 12) - from fragment lighting
    float sunDir[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    const float az = CVarGetFloat(POSTFX_SUN_AZIMUTH_CVAR, 45.0f) * 3.14159265f / 180.0f;
    const float el = CVarGetFloat(POSTFX_SUN_ELEVATION_CVAR, 55.0f) * 3.14159265f / 180.0f;
    sunDir[0] = cosf(el) * sinf(az);
    sunDir[1] = sinf(el);
    sunDir[2] = cosf(el) * cosf(az);
    sunDir[3] = CVarGetFloat(POSTFX_SUN_INTENSITY_CVAR, 0.45f);
    gfx_set_custom_uniform(12, sunDir);

    // Pass hemisphere sky/ground colors (registers 13, 14) - from fragment lighting
    float hemiSky[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    float hemiGround[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    if (CVarGetInteger(POSTFX_HEMISPHERE_CVAR, 1) != 0) {
        float r = 0.5f, g = 0.5f, b = 0.5f;
        extern void port_get_area_ambient(float*, float*, float*);
        port_get_area_ambient(&r, &g, &b);
        hemiSky[0] = r; hemiSky[1] = g; hemiSky[2] = b;
        hemiSky[3] = CVarGetFloat(POSTFX_FRAGLIGHT_STR_CVAR, 0.6f);
        hemiGround[0] = r * 0.5f; hemiGround[1] = g * 0.5f; hemiGround[2] = b * 0.5f;
        hemiGround[3] = hemiSky[3] * 0.5f;
    }
    gfx_set_custom_uniform(13, hemiSky);
    gfx_set_custom_uniform(14, hemiGround);

    // Pass sun color + ambient level (register 21) - from sunlight
    float sunColor[4] = { 1.0f, 1.0f, 1.0f, 1.0f };
    const float warmth = pbClampf(CVarGetFloat(POSTFX_SUN_WARMTH_CVAR, 0.45f), 0.0f, 1.0f);
    sunColor[0] = 1.0f;
    sunColor[1] = 1.0f - 0.16f * warmth;
    sunColor[2] = 1.0f - 0.40f * warmth;
    sunColor[3] = pbClampf(CVarGetFloat(POSTFX_SUN_AMBIENT_CVAR, 0.80f), 0.05f, 1.50f);
    gfx_set_custom_uniform(21, sunColor);

    // Pass exposure + debug mode (register 22)
    float exposure[4] = {
        pbClampf(CVarGetFloat(POSTFX_SUN_EXPOSURE_CVAR, 1.0f), 0.10f, 2.0f),
        0.0f,
        (float)CVarGetInteger(POSTFX_SUN_DEBUG_CVAR, 0),
        0.0f
    };
    gfx_set_custom_uniform(22, exposure);
}

static void PostFx_UpdateSunShade() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_SUNSHADE_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sSunShadePassId < 0) {
        sSunShadePassId = interp->RegisterPostPass("shaders/post/pb_sunshade.shader");
        SPDLOG_INFO("[PostFx] sun-shading pass registered id={}", sSunShadePassId);
    } else if (!on && sSunShadePassId >= 0) {
        interp->UnregisterPostPass(sSunShadePassId);
        sSunShadePassId = -1;
        SPDLOG_INFO("[PostFx] sun-shading pass removed");
    }
}

// Screen-space god rays post pass. Hands the tuning to registers 30/31 (the sun's
// screen position comes from CameraPatches via port_get_sun_screen_uv) and
// registers the pass while the CVar is set. Disabled (register 30.x == 0) when
// the sun is behind the camera or off screen, so the shader passes through.
static int sGodRaysPassId = -1;

static void PostFx_PushGodRays() {
    const bool on = CVarGetInteger(POSTFX_GODRAYS_CVAR, 0) != 0 && !sIsIndoor;

    float sunU = 0.5f;
    float sunV = 0.5f;
    int visible = 0;
    port_get_sun_screen_uv(&sunU, &sunV, &visible);

    float params[4] = {
        (on && visible) ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_GODRAYS_INTENSITY_CVAR, 0.6f), 0.0f, 3.0f),
        pbClampf(CVarGetFloat(POSTFX_GODRAYS_DENSITY_CVAR, 0.8f), 0.0f, 1.0f),
        pbClampf(CVarGetFloat(POSTFX_GODRAYS_DECAY_CVAR, 0.96f), 0.0f, 1.0f),
    };
    float shape[4] = {
        pbClampf(CVarGetFloat(POSTFX_GODRAYS_WEIGHT_CVAR, 0.5f), 0.0f, 1.0f),
        pbClampf(CVarGetFloat(POSTFX_GODRAYS_THRESHOLD_CVAR, 0.65f), 0.0f, 1.0f),
        sunU,
        sunV,
    };
    gfx_set_custom_uniform(30, params);
    gfx_set_custom_uniform(31, shape);
}

static void PostFx_UpdateGodRays() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_GODRAYS_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sGodRaysPassId < 0) {
        sGodRaysPassId = interp->RegisterPostPass("shaders/post/pb_godrays.shader");
        SPDLOG_INFO("[PostFx] god-rays pass registered id={}", sGodRaysPassId);
    } else if (!on && sGodRaysPassId >= 0) {
        interp->UnregisterPostPass(sGodRaysPassId);
        sGodRaysPassId = -1;
        SPDLOG_INFO("[PostFx] god-rays pass removed");
    }
}

// Screen-space contact shadows post pass. Tuning in registers 0/1; the sun
// direction already lives in register 12 (fragment lighting), which the shader
// reads. Requires depth, so it runs at the world/UI boundary like SSAO.
static int sContactShadowPassId = -1;

static void PostFx_PushContactShadows() {
    const bool on = CVarGetInteger(POSTFX_CONTACT_CVAR, 0) != 0;

    float params[4] = {
        on ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_CONTACT_STR_CVAR, 0.7f), 0.0f, 2.0f),
        pbClampf(CVarGetFloat(POSTFX_CONTACT_LEN_CVAR, 24.0f), 1.0f, 128.0f),
        pbClampf(CVarGetFloat(POSTFX_CONTACT_THICK_CVAR, 6.0f), 0.1f, 64.0f),
    };
    float dbg[4] = {
        (float)CVarGetInteger(POSTFX_CONTACT_DEBUG_CVAR, 0),
        0.0f,
        0.0f,
        0.0f,
    };
    gfx_set_custom_uniform(2, params);
    gfx_set_custom_uniform(3, dbg);
}

static void PostFx_UpdateContactShadows() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_CONTACT_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sContactShadowPassId < 0) {
        sContactShadowPassId = interp->RegisterPostPass("shaders/post/pb_contactshadow.shader");
        SPDLOG_INFO("[PostFx] contact-shadow pass registered id={}", sContactShadowPassId);
    } else if (!on && sContactShadowPassId >= 0) {
        interp->UnregisterPostPass(sContactShadowPassId);
        sContactShadowPassId = -1;
        SPDLOG_INFO("[PostFx] contact-shadow pass removed");
    }
}

// Screen-space cast shadows post pass. Registers 36/37; the sun direction comes
// from register 12. When the sun is not on screen the shader falls back to a ray
// along the sun direction, so the pass still works (with a fixed direction).
static int sScreenSpaceShadowPassId = -1;

static void PostFx_PushScreenSpaceShadows() {
    const bool on = CVarGetInteger(POSTFX_SSSHADOW_CVAR, 0) != 0;

    float sunU = -9999.0f;
    float sunV = -9999.0f;
    int visible = 0;
    port_get_sun_screen_uv(&sunU, &sunV, &visible);

    float params[4] = {
        on ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_SSSHADOW_STR_CVAR, 0.7f), 0.0f, 2.0f),
        pbClampf(CVarGetFloat(POSTFX_SSSHADOW_LEN_CVAR, 96.0f), 8.0f, 512.0f),
        pbClampf(CVarGetFloat(POSTFX_SSSHADOW_THICK_CVAR, 8.0f), 0.5f, 128.0f),
    };
    float opts[4] = {
        (float)CVarGetInteger(POSTFX_SSSHADOW_DEBUG_CVAR, 0),
        visible ? sunU : -9999.0f,
        visible ? sunV : -9999.0f,
        pbClampf(CVarGetFloat(POSTFX_SSSHADOW_BIAS_CVAR, 0.05f), 0.0f, 10.0f),
    };
    gfx_set_custom_uniform(36, params);
    gfx_set_custom_uniform(37, opts);
}

static void PostFx_UpdateScreenSpaceShadows() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_SSSHADOW_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }

    if (on && sScreenSpaceShadowPassId < 0) {
        sScreenSpaceShadowPassId = interp->RegisterPostPass("shaders/post/pb_ssshadow.shader");
        SPDLOG_INFO("[PostFx] screen-space-shadow pass registered id={}", sScreenSpaceShadowPassId);
    } else if (!on && sScreenSpaceShadowPassId >= 0) {
        interp->UnregisterPostPass(sScreenSpaceShadowPassId);
        sScreenSpaceShadowPassId = -1;
        SPDLOG_INFO("[PostFx] screen-space-shadow pass removed");
    }
}

// Bloom and FXAA post passes. Both are colour-only, so unlike the depth passes
// they do not require the world/UI split and can run as ordinary end-of-frame
// passes; they compose with each other and with the depth passes in the chain.
static int sBloomPassId = -1;
static int sFxaaPassId = -1;

static void PostFx_PushBloom() {
    const bool on = CVarGetInteger(POSTFX_BLOOM_CVAR, 0) != 0;
    float params[4] = {
        on ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_BLOOM_INT_CVAR, 0.6f), 0.0f, 3.0f),
        pbClampf(CVarGetFloat(POSTFX_BLOOM_THRESH_CVAR, 0.75f), 0.0f, 2.0f),
        pbClampf(CVarGetFloat(POSTFX_BLOOM_RADIUS_CVAR, 12.0f), 1.0f, 64.0f),
    };
    float dbg[4] = { (float)CVarGetInteger(POSTFX_BLOOM_DEBUG_CVAR, 0), 0.0f, 0.0f, 0.0f };
    gfx_set_custom_uniform(4, params);
    gfx_set_custom_uniform(5, dbg);
}

static void PostFx_PushFxaa() {
    const bool on = CVarGetInteger(POSTFX_FXAA_CVAR, 0) != 0;
    float params[4] = {
        on ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_FXAA_SUBPIX_CVAR, 0.75f), 0.0f, 1.0f),
        pbClampf(CVarGetFloat(POSTFX_FXAA_EDGE_CVAR, 0.125f), 0.0f, 1.0f),
        pbClampf(CVarGetFloat(POSTFX_FXAA_EDGEMIN_CVAR, 0.0312f), 0.0f, 1.0f),
    };
    gfx_set_custom_uniform(6, params);
}

static void PostFx_UpdateBloom() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_BLOOM_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    if (on && sBloomPassId < 0) {
        sBloomPassId = interp->RegisterPostPass("shaders/post/pb_bloom.shader");
        SPDLOG_INFO("[PostFx] bloom pass registered id={}", sBloomPassId);
    } else if (!on && sBloomPassId >= 0) {
        interp->UnregisterPostPass(sBloomPassId);
        sBloomPassId = -1;
        SPDLOG_INFO("[PostFx] bloom pass removed");
    }
}

static void PostFx_UpdateFxaa() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_FXAA_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    if (on && sFxaaPassId < 0) {
        sFxaaPassId = interp->RegisterPostPass("shaders/post/pb_fxaa.shader");
        SPDLOG_INFO("[PostFx] FXAA pass registered id={}", sFxaaPassId);
    } else if (!on && sFxaaPassId >= 0) {
        interp->UnregisterPostPass(sFxaaPassId);
        sFxaaPassId = -1;
        SPDLOG_INFO("[PostFx] FXAA pass removed");
    }
}

// Universal specular pass. Depth-driven like sun shading, so it needs the
// depth buffer (tile 1) and therefore participates in the world/UI split.
static int sSpecularPassId = -1;

static void PostFx_PushSpecular() {
    const bool on = CVarGetInteger(POSTFX_SPECULAR_CVAR, 0) != 0;
    float params[4] = {
        on ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_SPECULAR_STR_CVAR, 0.5f), 0.0f, 3.0f),
        pbClampf(CVarGetFloat(POSTFX_SPECULAR_SHINY_CVAR, 32.0f), 1.0f, 256.0f),
        0.0f,
    };
    float dbg[4] = { (float)CVarGetInteger(POSTFX_SPECULAR_DEBUG_CVAR, 0), 0.0f, 0.0f, 0.0f };
    gfx_set_custom_uniform(38, params);
    gfx_set_custom_uniform(39, dbg);
}

static void PostFx_UpdateSpecular() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_SPECULAR_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    if (on && sSpecularPassId < 0) {
        sSpecularPassId = interp->RegisterPostPass("shaders/post/pb_specular.shader");
        SPDLOG_INFO("[PostFx] specular pass registered id={}", sSpecularPassId);
    } else if (!on && sSpecularPassId >= 0) {
        interp->UnregisterPostPass(sSpecularPassId);
        sSpecularPassId = -1;
        SPDLOG_INFO("[PostFx] specular pass removed");
    }
}

// ============ Modern Shader Suite ============

// PBR
static int sPbrPassId = -1;
static void PostFx_PushPbr() {
    const bool on = CVarGetInteger(POSTFX_PBR_CVAR, 0) != 0;
    float params[4] = {
        on ? 1.0f : 0.0f,
        CVarGetInteger(POSTFX_PBR_USE_ORM_CVAR, 0) != 0 ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_PBR_ENV_INT_CVAR, 1.0f), 0.0f, 5.0f),
        (float)CVarGetInteger(POSTFX_PBR_DEBUG_CVAR, 0),
    };
    float ov[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    gfx_set_custom_uniform(40, params);
    gfx_set_custom_uniform(41, ov);
}
static void PostFx_UpdatePbr() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_PBR_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) return;
    if (on && sPbrPassId < 0) {
        sPbrPassId = interp->RegisterPostPass("shaders/post/pb_pbr.shader");
        SPDLOG_INFO("[PostFx] PBR pass registered id={}", sPbrPassId);
        GameEngine_RegisterShaderPassId("shaders/post/pb_pbr.shader", sPbrPassId);
    } else if (!on && sPbrPassId >= 0) {
        interp->UnregisterPostPass(sPbrPassId);
        sPbrPassId = -1;
        SPDLOG_INFO("[PostFx] PBR pass removed");
    }
}

// LUT Color Grading
static int sLutPassId = -1;
static void PostFx_PushLut() {
    const bool on = CVarGetInteger(POSTFX_LUT_CVAR, 0) != 0;
    float params[4] = {
        on ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_LUT_INTENSITY_CVAR, 1.0f), 0.0f, 1.0f),
        (float)CVarGetInteger(POSTFX_LUT_INDEX_CVAR, 0),
        (float)CVarGetInteger(POSTFX_LUT_DEBUG_CVAR, 0),
    };
    float cfg[4] = { 16.0f, 0.0f, 0.0f, 0.0f }; // LUT size
    gfx_set_custom_uniform(42, params);
    gfx_set_custom_uniform(43, cfg);
}
static void PostFx_UpdateLut() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_LUT_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) return;
    if (on && sLutPassId < 0) {
        sLutPassId = interp->RegisterPostPass("shaders/post/pb_lut.shader");
        SPDLOG_INFO("[PostFx] LUT pass registered id={}", sLutPassId);
        GameEngine_RegisterShaderPassId("shaders/post/pb_lut.shader", sLutPassId);
    } else if (!on && sLutPassId >= 0) {
        interp->UnregisterPostPass(sLutPassId);
        sLutPassId = -1;
        SPDLOG_INFO("[PostFx] LUT pass removed");
    }
}

// HDR Tonemap
static int sHdrPassId = -1;
static void PostFx_PushHdr() {
    const bool on = CVarGetInteger(POSTFX_HDR_CVAR, 0) != 0;
    float params[4] = {
        on ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_HDR_EXPOSURE_CVAR, 1.0f), 0.01f, 16.0f),
        (float)CVarGetInteger(POSTFX_HDR_CURVE_CVAR, 0), // 0=ACES, 1=Filmic, 2=Reinhard, 3=Custom
        pbClampf(CVarGetFloat(POSTFX_HDR_ADAPT_CVAR, 0.0f), 0.0f, 1.0f),
    };
    float curve[4] = {
        pbClampf(CVarGetFloat(POSTFX_HDR_WHITE_CVAR, 11.2f), 1.0f, 100.0f),
        pbClampf(CVarGetFloat(POSTFX_HDR_SHOULDER_CVAR, 0.3f), 0.01f, 1.0f),
        pbClampf(CVarGetFloat(POSTFX_HDR_TOE_CVAR, 0.02f), 0.001f, 0.5f),
        pbClampf(CVarGetFloat(POSTFX_HDR_CONTRAST_CVAR, 1.0f), 0.1f, 4.0f),
    };
    gfx_set_custom_uniform(44, params);
    gfx_set_custom_uniform(45, curve);
}
static void PostFx_UpdateHdr() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_HDR_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) return;
    if (on && sHdrPassId < 0) {
        sHdrPassId = interp->RegisterPostPass("shaders/post/pb_hdr_tonemap.shader");
        SPDLOG_INFO("[PostFx] HDR tonemap pass registered id={}", sHdrPassId);
        GameEngine_RegisterShaderPassId("shaders/post/pb_hdr_tonemap.shader", sHdrPassId);
    } else if (!on && sHdrPassId >= 0) {
        interp->UnregisterPostPass(sHdrPassId);
        sHdrPassId = -1;
        SPDLOG_INFO("[PostFx] HDR tonemap pass removed");
    }
}

// Volumetric Light Shafts
static int sVolumetricPassId = -1;
static void PostFx_PushVolumetric() {
    // Uses registers 30/31 from sun (CameraPatches) + 32 for config
    float cfg[4] = {
        (float)CVarGetInteger(POSTFX_VOL_SAMPLES_CVAR, 32),
        pbClampf(CVarGetFloat(POSTFX_VOL_LENGTH_CVAR, 1.0f), 0.1f, 4.0f),
        0.0f, // jitter
        (float)CVarGetInteger(POSTFX_VOL_DEBUG_CVAR, 0),
    };
    gfx_set_custom_uniform(32, cfg);
}
static void PostFx_UpdateVolumetric() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_VOLUMETRIC_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) return;
    if (on && sVolumetricPassId < 0) {
        sVolumetricPassId = interp->RegisterPostPass("shaders/post/pb_volumetric.shader");
        SPDLOG_INFO("[PostFx] Volumetric pass registered id={}", sVolumetricPassId);
        GameEngine_RegisterShaderPassId("shaders/post/pb_volumetric.shader", sVolumetricPassId);
    } else if (!on && sVolumetricPassId >= 0) {
        interp->UnregisterPostPass(sVolumetricPassId);
        sVolumetricPassId = -1;
        SPDLOG_INFO("[PostFx] Volumetric pass removed");
    }
}

// TAA
static int sTaaPassId = -1;
static void PostFx_PushTaa() {
    float params[4] = {
        CVarGetInteger(POSTFX_TAA_CVAR, 0) != 0 ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_TAA_JITTER_CVAR, 0.5f), 0.0f, 2.0f),
        pbClampf(CVarGetFloat(POSTFX_TAA_BLEND_CVAR, 0.9f), 0.0f, 1.0f),
        pbClampf(CVarGetFloat(POSTFX_TAA_CLAMP_CVAR, 0.75f), 0.0f, 1.0f),
    };
    gfx_set_custom_uniform(33, params);
}
static void PostFx_UpdateTaa() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_TAA_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) return;
    if (on && sTaaPassId < 0) {
        sTaaPassId = interp->RegisterPostPass("shaders/post/pb_taa.shader");
        SPDLOG_INFO("[PostFx] TAA pass registered id={}", sTaaPassId);
        GameEngine_RegisterShaderPassId("shaders/post/pb_taa.shader", sTaaPassId);
    } else if (!on && sTaaPassId >= 0) {
        interp->UnregisterPostPass(sTaaPassId);
        sTaaPassId = -1;
        SPDLOG_INFO("[PostFx] TAA pass removed");
    }
}

// Motion Blur
static int sMotionBlurPassId = -1;
static void PostFx_PushMotionBlur() {
    float params[4] = {
        CVarGetInteger(POSTFX_MOTIONBLUR_CVAR, 0) != 0 ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_MB_STRENGTH_CVAR, 1.0f), 0.0f, 4.0f),
        (float)CVarGetInteger(POSTFX_MB_SAMPLES_CVAR, 8),
        CVarGetInteger(POSTFX_MB_CAMERA_CVAR, 0) != 0 ? 1.0f : 0.0f,
    };
    float params2[4] = {
        pbClampf(CVarGetFloat(POSTFX_MB_VELSCALE_CVAR, 1.0f), 0.1f, 10.0f),
        pbClampf(CVarGetFloat(POSTFX_MB_MAXBLUR_CVAR, 0.1f), 0.0f, 0.5f),
        0.0f,
        (float)CVarGetInteger(POSTFX_MB_DEBUG_CVAR, 0),
    };
    gfx_set_custom_uniform(34, params);
    gfx_set_custom_uniform(35, params2);
}
static void PostFx_UpdateMotionBlur() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_MOTIONBLUR_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) return;
    if (on && sMotionBlurPassId < 0) {
        sMotionBlurPassId = interp->RegisterPostPass("shaders/post/pb_motionblur.shader");
        SPDLOG_INFO("[PostFx] Motion Blur pass registered id={}", sMotionBlurPassId);
        GameEngine_RegisterShaderPassId("shaders/post/pb_motionblur.shader", sMotionBlurPassId);
    } else if (!on && sMotionBlurPassId >= 0) {
        interp->UnregisterPostPass(sMotionBlurPassId);
        sMotionBlurPassId = -1;
        SPDLOG_INFO("[PostFx] Motion Blur pass removed");
    }
}

// Parallax Occlusion Mapping
static int sParallaxPassId = -1;
static void PostFx_PushParallax() {
    float params[4] = {
        CVarGetInteger(POSTFX_PARALLAX_CVAR, 0) != 0 ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_PARALLAX_SCALE_CVAR, 0.05f), 0.0f, 0.5f),
        (float)CVarGetInteger(POSTFX_PARALLAX_MINL_CVAR, 8),
        (float)CVarGetInteger(POSTFX_PARALLAX_MAXL_CVAR, 32),
    };
    float params2[4] = { 1.0f, (float)CVarGetInteger(POSTFX_PARALLAX_DEBUG_CVAR, 0), 0.0f, 0.0f };
    gfx_set_custom_uniform(36, params);
    gfx_set_custom_uniform(37, params2);
}
static void PostFx_UpdateParallax() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_PARALLAX_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) return;
    if (on && sParallaxPassId < 0) {
        sParallaxPassId = interp->RegisterPostPass("shaders/post/pb_parallax.shader");
        SPDLOG_INFO("[PostFx] Parallax pass registered id={}", sParallaxPassId);
        GameEngine_RegisterShaderPassId("shaders/post/pb_parallax.shader", sParallaxPassId);
    } else if (!on && sParallaxPassId >= 0) {
        interp->UnregisterPostPass(sParallaxPassId);
        sParallaxPassId = -1;
        SPDLOG_INFO("[PostFx] Parallax pass removed");
    }
}

// VRS (Variable Rate Shading) - simulated via post-process
static int sVrsPassId = -1;
static void PostFx_PushVrs() {
    float params[4] = {
        CVarGetInteger(POSTFX_VRS_CVAR, 0) != 0 ? 1.0f : 0.0f,
        pbClampf(CVarGetFloat(POSTFX_VRS_THRESH_CVAR, 0.02f), 0.0f, 1.0f),
        pbClampf(CVarGetFloat(POSTFX_VRS_MINRATE_CVAR, 0.25f), 0.1f, 1.0f),
        pbClampf(CVarGetFloat(POSTFX_VRS_MAXRATE_CVAR, 1.0f), 0.1f, 1.0f),
    };
    float params2[4] = { 8.0f, (float)CVarGetInteger(POSTFX_VRS_DEBUG_CVAR, 0), 0.0f, 0.0f };
    gfx_set_custom_uniform(38, params);
    gfx_set_custom_uniform(39, params2);
}
static void PostFx_UpdateVrs() {
    const bool on = pbMasterEnabled() && CVarGetInteger(POSTFX_VRS_CVAR, 0) != 0;
    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) return;
    if (on && sVrsPassId < 0) {
        sVrsPassId = interp->RegisterPostPass("shaders/post/pb_vrs.shader");
        SPDLOG_INFO("[PostFx] VRS pass registered id={}", sVrsPassId);
        GameEngine_RegisterShaderPassId("shaders/post/pb_vrs.shader", sVrsPassId);
    } else if (!on && sVrsPassId >= 0) {
        interp->UnregisterPostPass(sVrsPassId);
        sVrsPassId = -1;
        SPDLOG_INFO("[PostFx] VRS pass removed");
    }
}

// Periodic report so a playtest log shows what the in-shader effects are doing.
static void PostFx_LogStatus() {
    static int sFrame = 0;
    static int sLastSig = -2;
    sFrame++;

    const int level = PostFx_ShaderLogLevel();
    if (level <= 0) {
        return;
    }

    const int engineLighting = CVarGetInteger(POSTFX_ENGINE_LIGHTING_CVAR, 0) != 0 ? 1 : 0;
    const int modernLook = pbEffectOn(POSTFX_MODERN_CVAR, 1) ? 1 : 0;
    const int areaAmbient = pbEffectOn(POSTFX_AMBIENT_CVAR, 1) ? 1 : 0;
    const int fragLight = pbEffectOn(POSTFX_FRAGLIGHT_CVAR, 1) ? 1 : 0;
    const int relight = pbEffectOn(POSTFX_RELIGHT_CVAR, 1) ? 1 : 0;
    const int tonemap = pbEffectOn(POSTFX_TONEMAP_CVAR, 1) ? 1 : 0;

    const int sig = engineLighting | (modernLook << 1) | (areaAmbient << 2) | (fragLight << 3) |
                    (relight << 4) | (tonemap << 5);

    const int heartbeat = (level >= 2) ? 60 : 600;
    if (sig == sLastSig && (sFrame % heartbeat) != 0) {
        return;
    }
    sLastSig = sig;

    float ambR = 0.5f;
    float ambG = 0.5f;
    float ambB = 0.5f;
    port_get_area_ambient(&ambR, &ambG, &ambB);

    SPDLOG_INFO(
        "[PostFx] status engineLighting={}(wrap={:.2f} ground={:.2f} boost={:.2f}) "
        "modernLook={}(sat={:.2f} con={:.2f} bri={:.2f}) "
        "areaAmbient={}(str={:.2f}) areaPaletteAvg=({:.2f},{:.2f},{:.2f}) "
        "fragLight={}(str={:.2f} flip={}) sun=({:.2f},{:.2f},{:.2f})@{:.2f} "
        "sunlight=(warm={:.2f} amb={:.2f} exp={:.2f} dbg={}) "
        "relight={}(str={:.2f} floor={:.2f}) tonemap={}(exp={:.2f} curve={:.0f} blend={:.2f}) "
        "area={} areaPreset={}",
        engineLighting, CVarGetFloat(POSTFX_ENGINE_LIGHTING_WRAP_CVAR, 0.5f),
        CVarGetFloat(POSTFX_ENGINE_LIGHTING_GROUND_CVAR, 0.45f),
        CVarGetFloat(POSTFX_ENGINE_LIGHTING_BOOST_CVAR, 1.1f), modernLook,
        CVarGetFloat(POSTFX_MODERN_SAT_CVAR, 1.1f), CVarGetFloat(POSTFX_MODERN_CON_CVAR, 1.05f),
        CVarGetFloat(POSTFX_MODERN_BRI_CVAR, 1.0f), areaAmbient,
        CVarGetFloat(POSTFX_AMBIENT_STRENGTH_CVAR, 0.6f), ambR, ambG, ambB, fragLight,
        CVarGetFloat(POSTFX_FRAGLIGHT_STR_CVAR, 0.6f),
        CVarGetInteger(POSTFX_FRAGLIGHT_FLIP_CVAR, 0) ? 1 : 0,
        cosf(CVarGetFloat(POSTFX_SUN_ELEVATION_CVAR, 55.0f) * 3.14159265f / 180.0f) *
            sinf(CVarGetFloat(POSTFX_SUN_AZIMUTH_CVAR, 45.0f) * 3.14159265f / 180.0f),
        sinf(CVarGetFloat(POSTFX_SUN_ELEVATION_CVAR, 55.0f) * 3.14159265f / 180.0f),
        cosf(CVarGetFloat(POSTFX_SUN_ELEVATION_CVAR, 55.0f) * 3.14159265f / 180.0f) *
            cosf(CVarGetFloat(POSTFX_SUN_AZIMUTH_CVAR, 45.0f) * 3.14159265f / 180.0f),
        CVarGetFloat(POSTFX_SUN_INTENSITY_CVAR, 0.45f),
        CVarGetFloat(POSTFX_SUN_WARMTH_CVAR, 0.45f), CVarGetFloat(POSTFX_SUN_AMBIENT_CVAR, 0.80f),
        CVarGetFloat(POSTFX_SUN_EXPOSURE_CVAR, 1.0f), CVarGetInteger(POSTFX_SUN_DEBUG_CVAR, 0), relight,
        CVarGetFloat(POSTFX_RELIGHT_STR_CVAR, 0.85f), CVarGetFloat(POSTFX_RELIGHT_FLOOR_CVAR, 0.15f),
        tonemap, CVarGetFloat(POSTFX_TONEMAP_EXPOSURE_CVAR, 0.9f),
        CVarGetInteger(POSTFX_TONEMAP_CURVE_CVAR, 0) != 0 ? 1.0 : 0.0,
        CVarGetFloat(POSTFX_TONEMAP_BLEND_CVAR, 1.0f), EnhancedShadingBridge_CurrentAreaName(),
        EnhancedShadingBridge_CurrentAreaHasPreset() ? 1 : 0);
}

// Fully disables every enhanced-shading GPU effect: hands the shaders neutral
// uniforms (each effect's enable register is zeroed, so the in-shader code becomes
// a no-op) and unregisters the depth-consuming post passes. Called when the master
// toggle is off, so the game falls back cleanly to vanilla shading.
static void PostFx_PushNeutralAndTeardownPasses() {
    const float zero[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    // In-shader effects: x (or .a for register 10) == 0 disables each. See the
    // register map in the header comment above.
    gfx_set_custom_uniform(8, zero);  // engine lighting
    gfx_set_custom_uniform(9, zero);  // modern grade
    gfx_set_custom_uniform(10, zero); // area ambient
    gfx_set_custom_uniform(11, zero); // engine-light tint
    gfx_set_custom_uniform(12, zero); // fragment light: sun direction/intensity
    gfx_set_custom_uniform(13, zero); // fragment light: sky
    gfx_set_custom_uniform(14, zero); // fragment light: ground
    gfx_set_custom_uniform(15, zero); // fragment light: enable/strength/wrap/sign
    gfx_set_custom_uniform(16, zero); // rim
    gfx_set_custom_uniform(17, zero); // albedo relight off
    gfx_set_custom_uniform(18, zero); // tonemap off
    gfx_set_custom_uniform(2, zero);  // contact shadows off
    gfx_set_custom_uniform(3, zero);  // contact shadow debug off
    gfx_set_custom_uniform(36, zero); // screen-space shadows off
    gfx_set_custom_uniform(37, zero); // screen-space shadow opts off
    gfx_set_custom_uniform(4, zero);  // bloom off
    gfx_set_custom_uniform(5, zero);  // bloom debug off
    gfx_set_custom_uniform(6, zero);  // fxaa off
    gfx_set_custom_uniform(38, zero); // specular off
    gfx_set_custom_uniform(39, zero); // specular debug off
    gfx_set_custom_uniform(19, zero); // ssao off
    gfx_set_custom_uniform(20, zero); // ssao debug off
    gfx_set_custom_uniform(21, zero); // sun colour/ambient
    gfx_set_custom_uniform(22, zero); // sun exposure/debug view off
    gfx_set_custom_uniform(28, zero); // depth sun shading off
    gfx_set_custom_uniform(29, zero); // depth sun debug off
    gfx_set_custom_uniform(30, zero); // god rays off
    gfx_set_custom_uniform(31, zero); // god ray shape off

    Fast::Interpreter* interp = GameEngine_GetInterpreter();
    if (interp == nullptr) {
        return;
    }
    if (sSsaoPassId >= 0) {
        interp->UnregisterPostPass(sSsaoPassId);
        sSsaoPassId = -1;
        SPDLOG_INFO("[PostFx] ssao pass removed (master off)");
    }
    if (sDepthDebugPassId >= 0) {
        interp->UnregisterPostPass(sDepthDebugPassId);
        sDepthDebugPassId = -1;
        SPDLOG_INFO("[PostFx] depth-debug pass removed (master off)");
    }
    if (sSunShadePassId >= 0) {
        interp->UnregisterPostPass(sSunShadePassId);
        sSunShadePassId = -1;
        SPDLOG_INFO("[PostFx] sun-shading pass removed (master off)");
    }
    if (sGodRaysPassId >= 0) {
        interp->UnregisterPostPass(sGodRaysPassId);
        sGodRaysPassId = -1;
        SPDLOG_INFO("[PostFx] god-rays pass removed (master off)");
    }
    if (sContactShadowPassId >= 0) {
        interp->UnregisterPostPass(sContactShadowPassId);
        sContactShadowPassId = -1;
        SPDLOG_INFO("[PostFx] contact-shadow pass removed (master off)");
    }
    if (sScreenSpaceShadowPassId >= 0) {
        interp->UnregisterPostPass(sScreenSpaceShadowPassId);
        sScreenSpaceShadowPassId = -1;
        SPDLOG_INFO("[PostFx] screen-space-shadow pass removed (master off)");
    }
    if (sBloomPassId >= 0) {
        interp->UnregisterPostPass(sBloomPassId);
        sBloomPassId = -1;
        SPDLOG_INFO("[PostFx] bloom pass removed (master off)");
    }
    if (sFxaaPassId >= 0) {
        interp->UnregisterPostPass(sFxaaPassId);
        sFxaaPassId = -1;
        SPDLOG_INFO("[PostFx] FXAA pass removed (master off)");
    }
    if (sSpecularPassId >= 0) {
        interp->UnregisterPostPass(sSpecularPassId);
        sSpecularPassId = -1;
        SPDLOG_INFO("[PostFx] specular pass removed (master off)");
    }
}

static void PostFx_FrameUpdate() {
    // Stability gate: skip all post-FX work during unstable frames (loading,
    // alt-assets transitions, soft reset, minimized window). This prevents the
    // pass register/teardown churn and black frames seen in logs.
    // Track stability state to re-apply preset when stability returns
    static bool sWasStable = false;
    static bool sFirstFrameEver = true;
    const bool isStable = IsStableFrame();

    // FIRST FRAME EVER: apply preset immediately regardless of stability
    if (sFirstFrameEver) {
        sFirstFrameEver = false;
        if (pbMasterEnabled()) {
            // Apply built-in preset when CVAR changes (except 999 = custom)
            static int sLastBuiltInPreset = -2;
            int desiredPreset = CVarGetInteger(POSTFX_BUILTIN_PRESET_CVAR, 999);
            if (desiredPreset != sLastBuiltInPreset && desiredPreset >= 0 && desiredPreset < sNumPresets) {
                ApplyPreset(sPresets[desiredPreset]);
                sLastBuiltInPreset = desiredPreset;
                SPDLOG_INFO("[PostFx] applied built-in preset on first frame: {}", sPresets[desiredPreset].name);
            } else if (desiredPreset == 999) {
                ApplyPreset(sPresets[0]); // Balanced default
                CVarSetInteger(POSTFX_BUILTIN_PRESET_CVAR, 0);
                SPDLOG_INFO("[PostFx] applied default Balanced preset on first frame");
            }
        }
    }

    // If frame became unstable, don't teardown passes - just skip pass registration
    // Passes will be re-registered when stability returns
    if (!isStable) {
        static int sUnstableLogCounter = 0;
        if ((++sUnstableLogCounter % 60) == 0) {
            SPDLOG_INFO("[PostFx] frame skipped for passes (unstable), wasStable={}", sWasStable);
        }
        sWasStable = false;
        // Still push in-shader uniforms even on unstable frames so lighting/grade works
        if (pbMasterEnabled()) {
            PostFx_PushEngineLighting();
            PostFx_PushModernLook();
            PostFx_PushAreaAmbient();
            PostFx_PushFragmentLighting();
            PostFx_PushSunlight();
            EnhancedShadingBridge_PushLightingUniforms();
            PostFx_PushRelightAndTonemap();
            PostFx_PushSsao(); // push params even if pass not registered
            
            // Modern shader suite params (even on unstable frames for continuity)
            PostFx_PushPbr();
            PostFx_PushLut();
            PostFx_PushHdr();
            PostFx_PushVolumetric();
            PostFx_PushTaa();
            PostFx_PushMotionBlur();
            PostFx_PushParallax();
            PostFx_PushVrs();
        } else {
            PostFx_PushNeutralAndTeardownPasses();
        }
        return;
    }

    // Frame is stable now
    if (!sWasStable) {
        sWasStable = true;
        SPDLOG_INFO("[PostFx] stability gained");
    }

    if (!pbMasterEnabled()) {
        PostFx_PushNeutralAndTeardownPasses();
        return;
    }

    // Auto-detect area type - track internally, DON'T modify CVars (avoids pass re-reg storm)
    UpdateAreaType();
    sIsIndoor = (sCurrentAreaType == AREA_INDOOR || sCurrentAreaType == AREA_UNDERGROUND);

    // The engine lighting and modern grade simply need per-frame values latched in
    // the shader uniforms.
    PostFx_PushEngineLighting();
    PostFx_PushModernLook();
    PostFx_PushAreaAmbient();
    PostFx_PushFragmentLighting();
    PostFx_PushSunlight();
    EnhancedShadingBridge_PushLightingUniforms();
    PostFx_PushRelightAndTonemap();
    PostFx_PushSsao();
    PostFx_UpdateDepthDebug();
    PostFx_UpdateSsao();
    PostFx_PushSunShade();
    PostFx_UpdateSunShade();
    PostFx_PushGodRays();
    PostFx_UpdateGodRays();
    PostFx_PushContactShadows();
    PostFx_UpdateContactShadows();
    PostFx_PushScreenSpaceShadows();

    
    PostFx_PushBloom();
    PostFx_UpdateBloom();
    PostFx_PushFxaa();
    PostFx_UpdateFxaa();
    PostFx_PushSpecular();
    PostFx_UpdateSpecular();
    
    // Modern shader suite
    PostFx_PushPbr();
    PostFx_UpdatePbr();
    PostFx_PushLut();
    PostFx_UpdateLut();
    PostFx_PushHdr();
    PostFx_UpdateHdr();
    PostFx_PushVolumetric();
    PostFx_UpdateVolumetric();
    PostFx_PushTaa();
    PostFx_UpdateTaa();
    PostFx_PushMotionBlur();
    PostFx_UpdateMotionBlur();
    PostFx_PushParallax();
    PostFx_UpdateParallax();
    PostFx_PushVrs();
    PostFx_UpdateVrs();
    
    PostFx_LogStatus();
}

// Called from gfx_draw_frame (main_loop.c) between the world and HUD draws. When
// it returns 1 the game emits the OTR_G_RUN_POST_PASSES marker so the engine
// post-processes the world layer before any UI is drawn. Only true while enhanced
// shading is on and an actual depth-consuming post pass is registered, so stock
// (no-effect) frames never take the split path.
extern "C" s32 PostFx_ShouldSplitWorldLayer(void) {
    if (!pbMasterEnabled() || !IsStableFrame()) {
        return 0;
    }
    return (sSsaoPassId >= 0 || sDepthDebugPassId >= 0 || sSunShadePassId >= 0 || sGodRaysPassId >= 0 ||
            sContactShadowPassId >= 0 || sScreenSpaceShadowPassId >= 0 || sSpecularPassId >= 0 ||
            sPbrPassId >= 0 || sLutPassId >= 0 || sHdrPassId >= 0 || sVolumetricPassId >= 0 ||
            sTaaPassId >= 0 || sMotionBlurPassId >= 0 || sParallaxPassId >= 0 || sVrsPassId >= 0)
               ? 1
               : 0;
}

static void PostFx_OnWindowPreDraw(void) {
    if (PostFx_ShouldSplitWorldLayer()) {
        Fast::Interpreter* interp = GameEngine_GetInterpreter();
        if (interp) {
            interp->RunPostPassesMidFrame();
        }
    }
}

void register_postfx_hooks(void) {
    SPDLOG_INFO("[PostFx] register_postfx_hooks called");
    REGISTER_LISTENER(GameFrameUpdate, EVENT_PRIORITY_NORMAL, [](IEvent* event) { PostFx_FrameUpdate(); });
    REGISTER_LISTENER(WindowPreDraw, EVENT_PRIORITY_NORMAL, [](IEvent* event) { PostFx_OnWindowPreDraw(); });
}
