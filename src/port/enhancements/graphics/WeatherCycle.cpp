#include "port/ShipInit.hpp"
#include "port/Engine.h"
#include "port/enhancements/graphics/EnvironmentCycleGate.h"
#include "port/enhancements/graphics/WeatherCycle.h"
#include "port/hooks/Events.h"
#include "port/ui/cvar_prefixes.h"

#include "common.h"
#include "assets/effects.h"

#include <chrono>
#include <cmath>
#include <ctime>

extern "C" void sfx_play_sound(s32 soundID);

namespace {

constexpr int FIXED_CYCLE[] = {
    WEATHER_TYPE_CLEAR,
    WEATHER_TYPE_RAIN,
    WEATHER_TYPE_CLEAR,
    WEATHER_TYPE_THUNDERSTORM,
    WEATHER_TYPE_CLEAR,
    WEATHER_TYPE_SNOW,
};

ListenerID sUpdateListener = -1;
std::chrono::steady_clock::time_point sLastTick;
uint32_t sSessionSeed = 0;
uint32_t sSeed = 1;
int sSettingsKey = -1;
float sEventSeconds = 0.0f;
int sEventIndex = 0;
int sCurrentWeather = WEATHER_TYPE_CLEAR;
int sLastThunderSlot = -1;
uint32_t sFrame = 0;
bool sSnowTexturePrefetched = false;

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
    if (areaID == AREA_DRO) {
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

int LightningAlpha() {
    if (CurrentWeather() != WEATHER_TYPE_THUNDERSTORM) {
        return 0;
    }

    float phase = std::fmodf(sEventSeconds, ThunderInterval());
    if (phase < 0.08f) {
        return 190;
    }
    if (phase < 0.14f) {
        return 80;
    }
    return 0;
}

void ResetWeather() {
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
    sFrame++;

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

void DrawRain(s32 left, s32 right, s32 top, s32 bottom, bool storm) {
    s32 width = right - left;
    s32 height = bottom - top;
    if (width <= 0 || height <= 0) {
        return;
    }

    gDPPipeSync(gMainGfxPos++);
    gDPSetColorDither(gMainGfxPos++, G_CD_MAGICSQ);
    gDPSetAlphaDither(gMainGfxPos++, G_AD_PATTERN);
    gDPSetCycleType(gMainGfxPos++, G_CYC_1CYCLE);
    gDPSetRenderMode(gMainGfxPos++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    gDPSetCombineMode(gMainGfxPos++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gMainGfxPos++, 0, 0, storm ? 165 : 175, storm ? 185 : 195, 235, storm ? 165 : 120);

    for (s32 i = 0; i < (storm ? 110 : 80); i++) {
        uint32_t hash = Hash(static_cast<uint32_t>(i) * 0x9E3779B9u);
        s32 speed = 16 + static_cast<s32>((hash >> 8) & 7);
        s32 x = left + static_cast<s32>((hash & 0xFFFF) % static_cast<uint32_t>(width));
        s32 y = top + static_cast<s32>(
            ((hash >> 16) + sFrame * static_cast<uint32_t>(speed)) % static_cast<uint32_t>(height + 24)
        ) - 12;
        s32 slant = storm ? 3 : 1;

        gDPFillWideRectangle(gMainGfxPos++, x, y, x + slant, y + (storm ? 15 : 11));
    }

    gDPPipeSync(gMainGfxPos++);
}

void DrawSnow(s32 left, s32 right, s32 top, s32 bottom) {
    s32 width = right - left;
    s32 height = bottom - top;
    if (width <= 0 || height <= 0) {
        return;
    }

    if (!sSnowTexturePrefetched) {
        GameEngine_PrefetchTexture(D_09000000_38D070);
        sSnowTexturePrefetched = true;
    }

    gDPPipeSync(gMainGfxPos++);
    gDPSetCycleType(gMainGfxPos++, G_CYC_1CYCLE);
    gDPSetTexturePersp(gMainGfxPos++, G_TP_NONE);
    gDPSetTextureFilter(gMainGfxPos++, G_TF_POINT);
    gDPSetTextureLUT(gMainGfxPos++, G_TT_NONE);
    gDPSetTextureDetail(gMainGfxPos++, G_TD_CLAMP);
    gDPSetTextureLOD(gMainGfxPos++, G_TL_TILE);
    gDPSetColorDither(gMainGfxPos++, G_CD_DISABLE);
    gDPSetAlphaDither(gMainGfxPos++, G_AD_NOISE);
    gDPSetRenderMode(gMainGfxPos++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    gSPTexture(gMainGfxPos++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(gMainGfxPos++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(gMainGfxPos++, 0, 0, 255, 255, 255, 210);
    gDPLoadTextureTile(
        gMainGfxPos++, D_09000000_38D070, G_IM_FMT_I, G_IM_SIZ_4b, 16, 16, 0, 0, 15, 15, 0,
        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
        G_TX_NOLOD
    );

    for (s32 i = 0; i < 56; i++) {
        uint32_t hash = Hash(static_cast<uint32_t>(i) * 0x85EBCA6Bu);
        s32 speed = 2 + static_cast<s32>((hash >> 8) & 3);
        s32 size = 4 + static_cast<s32>((hash >> 12) & 3);
        s32 x = left + static_cast<s32>((hash & 0xFFFF) % static_cast<uint32_t>(width));
        s32 y = top + static_cast<s32>(
            ((hash >> 16) + sFrame * static_cast<uint32_t>(speed)) % static_cast<uint32_t>(height + 32)
        ) - 16;
        s32 step = 1024 * 16 / size;

        gSPWideTextureRectangle(
            gMainGfxPos++, x * 4, y * 4, (x + size) * 4, (y + size) * 4, G_TX_RENDERTILE, 0, 0, step, step
        );
    }

    gDPPipeSync(gMainGfxPos++);
}

void DrawLightning(s32 left, s32 right, s32 top, s32 bottom, int alpha) {
    gDPPipeSync(gMainGfxPos++);
    gDPSetColorDither(gMainGfxPos++, G_CD_DISABLE);
    gDPSetAlphaDither(gMainGfxPos++, G_AD_PATTERN);
    gDPSetCycleType(gMainGfxPos++, G_CYC_1CYCLE);
    gDPSetRenderMode(gMainGfxPos++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    gDPSetCombineMode(gMainGfxPos++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gMainGfxPos++, 0, 0, 255, 250, 230, static_cast<u8>(alpha));
    gDPFillWideRectangle(gMainGfxPos++, left, top, right, bottom);
    gDPPipeSync(gMainGfxPos++);
}

} // namespace

extern "C" s32 Port_GetCurrentWeather(void) {
    if (!Port_FeatureInt("graphics.weather_cycle", 0) || !Port_IsCycleGameplayActive()) {
        return WEATHER_TYPE_CLEAR;
    }
    return CurrentWeather();
}

extern "C" void Port_WeatherDrawWorldOverlay(void) {
    Camera* cam;
    s32 left;
    s32 right;
    s32 top;
    s32 bottom;
    int weather;
    int flash;

    if (Port_GetCurrentWeather() == WEATHER_TYPE_CLEAR) {
        return;
    }

    cam = &gCameras[gCurrentCameraID];
    get_cam_frame_x(gCurrentCameraID, &left, &right);
    top = cam->viewportStartY;
    bottom = top + cam->viewportH;
    if (cam->viewportH <= 0 || left >= right) {
        return;
    }

    weather = CurrentWeather();
    gDPSetScissor(gMainGfxPos++, G_SC_NON_INTERLACE, left, top, right, bottom);

    if (weather == WEATHER_TYPE_SNOW) {
        DrawSnow(left, right, top, bottom);
    } else {
        DrawRain(left, right, top, bottom, weather == WEATHER_TYPE_THUNDERSTORM);
    }

    flash = LightningAlpha();
    if (flash != 0) {
        DrawLightning(left, right, top, bottom, flash);
    }

    gDPSetScissor(gMainGfxPos++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gDPPipeSync(gMainGfxPos++);
}

void RegisterWeatherCycle_Init() {
    UNREGISTER_LISTENER(GameFrameUpdate, sUpdateListener);
    sUpdateListener = -1;

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
