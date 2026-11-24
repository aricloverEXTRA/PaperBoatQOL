#include "port/ShipInit.hpp"
#include "port/Engine.h"
#include "port/enhancements/graphics/EnvironmentCycleGate.h"
#include "port/enhancements/graphics/WeatherCycle.h"
#include "port/hooks/Events.h"
#include "port/ui/cvar_prefixes.h"

#include "common.h"
#include "assets/map_textures.h"

#include <chrono>
#include <cmath>
#include <ctime>

extern "C" void sfx_play_sound(s32 soundID);
extern "C" EffectInstance* gEffectInstances[96];
extern "C" EffectInstance* port_spawn_weather_snowfall(s32 variant, s32 count);
extern "C" void port_remove_weather_effect(EffectInstance* effect);
extern "C" void port_configure_weather_effect(EffectInstance* effect);

namespace {

constexpr int FIXED_CYCLE[] = {
    WEATHER_TYPE_CLEAR,
    WEATHER_TYPE_RAIN,
    WEATHER_TYPE_CLEAR,
    WEATHER_TYPE_THUNDERSTORM,
    WEATHER_TYPE_CLEAR,
    WEATHER_TYPE_SNOW,
};

constexpr int SNOWFALL_VARIANT_SNOW = 0;
constexpr int SNOWFALL_VARIANT_RAIN = 1;
constexpr int SNOWFALL_VARIANT_STORM = 2;
constexpr s32 EFFECT_INSTANCE_COUNT = 96;

ListenerID sUpdateListener = -1;
std::chrono::steady_clock::time_point sLastTick;
uint32_t sSessionSeed = 0;
uint32_t sSeed = 1;
int sSettingsKey = -1;
float sEventSeconds = 0.0f;
int sEventIndex = 0;
int sCurrentWeather = WEATHER_TYPE_CLEAR;
int sLastThunderSlot = -1;
EffectInstance* sWeatherEffect = nullptr;
int sEffectWeather = -1;
int sEffectContext = -1;
s32 sEffectArea = -1;
s32 sEffectMap = -1;
bool sRainTexturePrefetched = false;

uint32_t Hash(uint32_t value) {
    value ^= value >> 16;
    value *= 0x7FEB352D;
    value ^= value >> 15;
    value *= 0x846CA68B;
    value ^= value >> 16;
    return value;
}

int ClampInt(int value, int minValue, int maxValue, int fallback) {
    if (value < minValue || value > maxValue) {
        return fallback;
    }
    return value;
}

int Duration() {
    return ClampInt(Port_FeatureInt("graphics.weather_cycle.duration", 20), 5, 120, 20);
}

int Randomness() {
    return ClampInt(Port_FeatureInt("graphics.weather_cycle.randomness", 50), 0, 100, 50);
}

int SeedSource() {
    return ClampInt(Port_FeatureInt("graphics.weather_cycle.seed_source", 1), 0, 1, 1);
}

int ForceWeather() {
    return ClampInt(Port_FeatureInt("graphics.weather_cycle.force_weather", 0), 0, 3, 0);
}

uint32_t CurrentSeed() {
    if (SeedSource() == 0) {
        if (sSessionSeed == 0) {
            sSessionSeed = Hash(static_cast<uint32_t>(
                std::chrono::steady_clock::now().time_since_epoch().count())) | 1;
        }
        return sSessionSeed;
    }

    std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm localTime = {};
#if defined(_WIN32)
    localtime_s(&localTime, &now);
#else
    localtime_r(&now, &localTime);
#endif
    uint32_t date = static_cast<uint32_t>(localTime.tm_year + 1900) * 10000u +
                    static_cast<uint32_t>(localTime.tm_mon + 1) * 100u +
                    static_cast<uint32_t>(localTime.tm_mday);
    return Hash(date);
}

int RandomWeather(uint32_t value) {
    uint32_t roll = value % 100;
    if (roll < 40) {
        return WEATHER_TYPE_CLEAR;
    }
    if (roll < 70) {
        return WEATHER_TYPE_RAIN;
    }
    if (roll < 82) {
        return WEATHER_TYPE_THUNDERSTORM;
    }
    return WEATHER_TYPE_SNOW;
}

int SelectWeather(int index) {
    int forced = ForceWeather();
    if (forced != 0) {
        return forced;
    }

    int fixed = FIXED_CYCLE[index % (sizeof(FIXED_CYCLE) / sizeof(FIXED_CYCLE[0]))];
    int randomness = Randomness();
    if (randomness == 0) {
        return fixed;
    }

    uint32_t roll = Hash(sSeed ^ static_cast<uint32_t>(index) * 0x9E3779B9u) % 100;
    if (roll < static_cast<uint32_t>(randomness)) {
        return RandomWeather(Hash(sSeed + static_cast<uint32_t>(index) * 0x85EBCA6Bu));
    }
    return fixed;
}

int SanitizeWeather(int weather, s32 areaID) {
    if (areaID == AREA_DRO || areaID == AREA_FLO) {
        return WEATHER_TYPE_CLEAR;
    }
    if (areaID == AREA_SAM && (weather == WEATHER_TYPE_RAIN || weather == WEATHER_TYPE_THUNDERSTORM)) {
        return WEATHER_TYPE_SNOW;
    }
    return weather;
}

int CurrentWeather() {
    s32 areaID = gGameStatusPtr != nullptr ? gGameStatusPtr->areaID : -1;
    return SanitizeWeather(sCurrentWeather, areaID);
}

float ThunderInterval() {
    return 5.0f + static_cast<float>(Hash(sSeed ^ static_cast<uint32_t>(sEventIndex) * 0xC2B2AE35u) % 6);
}

float LightningPhase() {
    return std::fmodf(sEventSeconds, ThunderInterval());
}

int LightningFlashAlpha() {
    float phase = LightningPhase();
    if (phase < 0.08f) {
        return 190;
    }
    if (phase < 0.14f) {
        return 80;
    }
    return 0;
}

bool LightningBoltVisible() {
    return LightningPhase() < 0.10f;
}

bool EffectAlive(EffectInstance* effect) {
    if (effect == nullptr) {
        return false;
    }
    for (s32 i = 0; i < EFFECT_INSTANCE_COUNT; i++) {
        if (gEffectInstances[i] == effect) {
            return true;
        }
    }
    return false;
}

void RemoveWeatherEffect() {
    if (EffectAlive(sWeatherEffect)) {
        port_remove_weather_effect(sWeatherEffect);
    }
    sWeatherEffect = nullptr;
    sEffectWeather = -1;
    sEffectContext = -1;
    sEffectArea = -1;
    sEffectMap = -1;
}

int SnowfallMode(int weather) {
    if (weather == WEATHER_TYPE_RAIN) {
        return SNOWFALL_VARIANT_RAIN;
    }
    if (weather == WEATHER_TYPE_THUNDERSTORM) {
        return SNOWFALL_VARIANT_STORM;
    }
    return SNOWFALL_VARIANT_SNOW;
}

int SnowfallCount(int weather) {
    if (weather == WEATHER_TYPE_THUNDERSTORM) {
        return 110;
    }
    if (weather == WEATHER_TYPE_RAIN) {
        return 80;
    }
    return 48;
}

void UpdateWeatherEffect() {
    int weather = CurrentWeather();
    int context = gGameStatusPtr != nullptr ? gGameStatusPtr->context : -1;
    s32 areaID = gGameStatusPtr != nullptr ? gGameStatusPtr->areaID : -1;
    s32 mapID = gGameStatusPtr != nullptr ? gGameStatusPtr->mapID : -1;

    if (!Port_FeatureInt("graphics.weather_cycle", 0) || !Port_IsCycleGameplayActive() ||
        weather == WEATHER_TYPE_CLEAR) {
        RemoveWeatherEffect();
        return;
    }

    if (EffectAlive(sWeatherEffect) && sEffectWeather == weather && sEffectContext == context &&
        sEffectArea == areaID && sEffectMap == mapID) {
        return;
    }

    RemoveWeatherEffect();
    if (!sRainTexturePrefetched) {
        GameEngine_PrefetchTexture(iwa_dropwater_btif);
        sRainTexturePrefetched = true;
    }

    sWeatherEffect = port_spawn_weather_snowfall(SnowfallMode(weather), SnowfallCount(weather));
    port_configure_weather_effect(sWeatherEffect);
    sEffectWeather = weather;
    sEffectContext = context;
    sEffectArea = areaID;
    sEffectMap = mapID;
}

void ResetWeather() {
    RemoveWeatherEffect();
    sLastTick = std::chrono::steady_clock::now();
    sSeed = CurrentSeed();
    sEventSeconds = 0.0f;
    sEventIndex = 0;
    sCurrentWeather = SelectWeather(0);
    sLastThunderSlot = -1;
}

void TickWeather() {
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    float deltaSeconds = std::chrono::duration<float>(now - sLastTick).count();
    sLastTick = now;

    if (deltaSeconds < 0.0f || deltaSeconds > 1.0f) {
        deltaSeconds = 0.0f;
    }

    sEventSeconds += deltaSeconds;
    int duration = Duration();
    while (sEventSeconds >= static_cast<float>(duration)) {
        sEventSeconds -= static_cast<float>(duration);
        sEventIndex++;
        sCurrentWeather = SelectWeather(sEventIndex);
        sLastThunderSlot = -1;
    }

    UpdateWeatherEffect();

    if (CurrentWeather() != WEATHER_TYPE_THUNDERSTORM || !Port_IsCycleGameplayActive() ||
        !Port_FeatureInt("graphics.weather_cycle.thunder_sound", 0)) {
        return;
    }

    int slot = static_cast<int>(sEventSeconds / ThunderInterval());
    if (slot != sLastThunderSlot) {
        sLastThunderSlot = slot;
        sfx_play_sound(SOUND_THUNDER_BOLT);
    }
}

void DrawSkyLightning(s32 left, s32 right, s32 top, s32 bottom, int alpha) {
    s32 width = right - left;
    s32 height = bottom - top;
    s32 skyBottom;
    uint32_t hash;
    s32 x;
    s32 y;
    s32 segHeight;
    s32 i;

    if (width <= 0 || height <= 0) {
        return;
    }

    gDPSetScissor(gMainGfxPos++, G_SC_NON_INTERLACE, left, top, right, bottom);
    gDPPipeSync(gMainGfxPos++);
    gDPSetCycleType(gMainGfxPos++, G_CYC_1CYCLE);
    gDPSetRenderMode(gMainGfxPos++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    gDPSetCombineMode(gMainGfxPos++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);

    skyBottom = top + height * 55 / 100;
    gDPSetPrimColor(gMainGfxPos++, 0, 0, 190, 215, 255, alpha / 6);
    gDPFillWideRectangle(gMainGfxPos++, left, top, right, skyBottom);

    if (LightningBoltVisible()) {
        int slot = static_cast<int>(sEventSeconds / ThunderInterval());
        hash = Hash(sSeed ^ static_cast<uint32_t>(slot) * 0x9E3779B9u);
        x = left + static_cast<s32>(hash & 0xFFFF) % width;
        y = top + static_cast<s32>((hash >> 16) & 0xFFFF) % (height / 3 + 1);
        segHeight = height / 12;
        if (segHeight < 6) {
            segHeight = 6;
        }

        for (i = 0; i < 9; i++) {
            uint32_t stepHash = Hash(hash ^ (static_cast<uint32_t>(i) * 0x85EBCA6Bu));
            s32 drift = static_cast<s32>(stepHash & 7) - 3;
            s32 step = width / 100 + 1;
            s32 nextX = x + drift * step;
            s32 nextY = y + segHeight;
            s32 minX = x < nextX ? x : nextX;
            s32 maxX = x > nextX ? x : nextX;

            if (nextY > bottom) {
                nextY = bottom;
            }

            gDPSetPrimColor(gMainGfxPos++, 0, 0, 190, 220, 255, alpha / 3);
            gDPFillWideRectangle(gMainGfxPos++, minX - 4, y, maxX + 4, nextY);
            gDPSetPrimColor(gMainGfxPos++, 0, 0, 255, 255, 255, alpha);
            gDPFillWideRectangle(gMainGfxPos++, minX - 1, y, maxX + 1, nextY);

            x = nextX;
            y = nextY;
            if (y >= bottom) {
                break;
            }
        }
    }

    gDPPipeSync(gMainGfxPos++);
    gDPSetScissor(gMainGfxPos++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gDPPipeSync(gMainGfxPos++);
}

} // namespace

extern "C" s32 Port_GetCurrentWeather(void) {
    if (!Port_FeatureInt("graphics.weather_cycle", 0) || !Port_IsCycleGameplayActive()) {
        return WEATHER_TYPE_CLEAR;
    }
    return CurrentWeather();
}

extern "C" void Port_WeatherDrawSkyOverlay(void) {
    Camera* cam;
    s32 left;
    s32 right;
    s32 top;
    s32 bottom;
    int alpha;

    if (Port_GetCurrentWeather() != WEATHER_TYPE_THUNDERSTORM) {
        return;
    }

    alpha = LightningFlashAlpha();
    if (alpha == 0) {
        return;
    }

    cam = &gCameras[gCurrentCameraID];
    get_cam_frame_x(gCurrentCameraID, &left, &right);
    top = cam->viewportStartY;
    bottom = top + cam->viewportH;
    DrawSkyLightning(left, right, top, bottom, alpha);
}

void RegisterWeatherCycle_Init() {
    UNREGISTER_LISTENER(GameFrameUpdate, sUpdateListener);
    sUpdateListener = -1;
    RemoveWeatherEffect();

    if (!Port_FeatureInt("graphics.weather_cycle", 0)) {
        sSettingsKey = -1;
        return;
    }

    int settingsKey = Duration() | (Randomness() << 8) | (SeedSource() << 16) | (ForceWeather() << 24);
    if (settingsKey != sSettingsKey) {
        ResetWeather();
        sSettingsKey = settingsKey;
    }

    sUpdateListener = REGISTER_LISTENER(GameFrameUpdate, EVENT_PRIORITY_NORMAL, [](IEvent*) {
        TickWeather();
    });
}

static RegisterShipInitFunc initWeatherCycleFunc(
    RegisterWeatherCycle_Init,
    {
        CVAR_ENHANCEMENT("Graphics.WeatherCycle.Enabled"),
        CVAR_ENHANCEMENT("Graphics.WeatherCycle.Duration"),
        CVAR_ENHANCEMENT("Graphics.WeatherCycle.Randomness"),
        CVAR_ENHANCEMENT("Graphics.WeatherCycle.SeedSource"),
        CVAR_ENHANCEMENT("Graphics.WeatherCycle.ForceWeather"),
        CVAR_ENHANCEMENT("Graphics.WeatherCycle.ThunderSound"),
    }
);
