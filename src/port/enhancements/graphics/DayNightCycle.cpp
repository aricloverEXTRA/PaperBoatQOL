#include "port/ShipInit.hpp"
#include "port/Engine.h"
#include "port/enhancements/graphics/DayNightCycle.h"
#include "port/hooks/Events.h"
#include "port/ui/cvar_prefixes.h"

#include "common.h"
#include "game_modes.h"

#include <ship/Context.h>
#include <ship/config/ConsoleVariable.h>

#include <chrono>
#include <cmath>
#include <ctime>
#include <string>

namespace {

constexpr int32_t FULL_DAY_MINUTES = 24 * 60;
constexpr int32_t DEFAULT_START_MINUTES = 6 * 60;

struct TintKeyframe {
    float hour;
    u8 r;
    u8 g;
    u8 b;
    u8 alpha;
};

constexpr TintKeyframe sTintKeyframes[] = {
    { 0.0f, 8, 14, 44, 215 },
    { 4.5f, 8, 14, 44, 215 },
    { 5.5f, 76, 48, 92, 155 },
    { 6.5f, 255, 166, 98, 90 },
    { 8.0f, 255, 255, 255, 0 },
    { 16.5f, 255, 255, 255, 0 },
    { 18.0f, 255, 142, 80, 95 },
    { 19.5f, 70, 42, 88, 175 },
    { 21.0f, 8, 14, 44, 215 },
    { 24.0f, 8, 14, 44, 215 },
};

ListenerID sUpdateListener = -1;
std::chrono::steady_clock::time_point sLastTick;
float sCycleMinutes = DEFAULT_START_MINUTES;
bool sCycleInitialized = false;
int sLastTimeSource = -1;
int sLastStartHour = -1;
int sLastDayLength = -1;

bool IsOutdoorArea(s32 areaID) {
    switch (areaID) {
        case AREA_KMR:
        case AREA_MAC:
        case AREA_NOK:
        case AREA_IWA:
        case AREA_DRO:
        case AREA_SBK:
        case AREA_ARN:
        case AREA_JAN:
        case AREA_KZN:
        case AREA_FLO:
        case AREA_SAM:
        case AREA_OSR:
            return true;
        default:
            return false;
    }
}

bool IsTintableContext() {
    if (gGameStatusPtr == nullptr || (gGameStatusPtr->demoBattleFlags & DEMO_BTL_FLAG_ENABLED)) {
        return false;
    }
    return gGameStatusPtr->context == CONTEXT_WORLD || gGameStatusPtr->context == CONTEXT_BATTLE;
}

float CurrentHour() {
    if (Port_FeatureInt("graphics.day_night_cycle.time_source", 0) == 1) {
        std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm localTime = {};
#if defined(_WIN32)
        localtime_s(&localTime, &now);
#else
        localtime_r(&now, &localTime);
#endif
        float hour = static_cast<float>(localTime.tm_hour) + static_cast<float>(localTime.tm_min) / 60.0f +
                     static_cast<float>(localTime.tm_sec) / 3600.0f;

        // Shift the cycle, not the clock: DST gets earlier sunrises and later sunsets;
        // standard time gets the reverse, which lengthens nights after DST ends.
        if (Port_FeatureInt("graphics.day_night_cycle.daylight_saving", 0)) {
            constexpr float SEASONAL_SHIFT = 2.0f;
            float shift = localTime.tm_isdst > 0 ? SEASONAL_SHIFT : -SEASONAL_SHIFT;
            hour += hour < 12.0f ? shift : -shift;
        }

        return hour;
    }

    return sCycleMinutes / 60.0f;
}

int Strength() {
    int strength = Port_FeatureInt("graphics.day_night_cycle.strength", 70);
    if (strength < 0) {
        return 0;
    }
    if (strength > 100) {
        return 100;
    }
    return strength;
}

u8 LerpChannel(u8 from, u8 to, float t) {
    return static_cast<u8>(static_cast<int>(from) + (static_cast<int>(to) - static_cast<int>(from)) * t);
}

float NightAmount(float hour) {
    if (hour < 4.5f) {
        return 1.0f;
    }
    if (hour < 6.5f) {
        return 1.0f - (hour - 4.5f) / 2.0f;
    }
    if (hour < 17.5f) {
        return 0.0f;
    }
    if (hour < 20.5f) {
        return (hour - 17.5f) / 3.0f;
    }
    return 1.0f;
}

DayNightTint TintForHour(float hour) {
    if (hour < 0.0f) {
        hour += 24.0f;
    } else if (hour >= 24.0f) {
        hour = std::fmodf(hour, 24.0f);
    }

    const TintKeyframe* from = &sTintKeyframes[0];
    const TintKeyframe* to = &sTintKeyframes[1];
    for (size_t i = 0; i + 1 < sizeof(sTintKeyframes) / sizeof(sTintKeyframes[0]); ++i) {
        if (hour >= sTintKeyframes[i].hour && hour <= sTintKeyframes[i + 1].hour) {
            from = &sTintKeyframes[i];
            to = &sTintKeyframes[i + 1];
            break;
        }
    }

    float t = 0.0f;
    if (to->hour != from->hour) {
        t = (hour - from->hour) / (to->hour - from->hour);
        if (t < 0.0f) {
            t = 0.0f;
        } else if (t > 1.0f) {
            t = 1.0f;
        }
    }

    int strength = Strength();
    float scale = static_cast<float>(strength) / 100.0f;
    float alpha = static_cast<float>(LerpChannel(from->alpha, to->alpha, t)) * scale;
    float stars = NightAmount(hour) * 255.0f * scale;

    return {
        LerpChannel(from->r, to->r, t),
        LerpChannel(from->g, to->g, t),
        LerpChannel(from->b, to->b, t),
        static_cast<u8>(alpha),
        static_cast<u8>(stars),
    };
}

void TickCycle() {
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    float deltaSeconds = std::chrono::duration<float>(now - sLastTick).count();
    sLastTick = now;

    if (deltaSeconds < 0.0f || deltaSeconds > 1.0f) {
        deltaSeconds = 0.0f;
    }

    if (Port_FeatureInt("graphics.day_night_cycle.freeze_time", 0)) {
        return;
    }

    int dayLength = Port_FeatureInt("graphics.day_night_cycle.day_length", 24);
    if (dayLength < 1) {
        dayLength = 1;
    }

    sCycleMinutes += deltaSeconds * 24.0f / static_cast<float>(dayLength);
    if (sCycleMinutes >= static_cast<float>(FULL_DAY_MINUTES)) {
        sCycleMinutes = std::fmodf(sCycleMinutes, static_cast<float>(FULL_DAY_MINUTES));
    }
}

void ResetCycle() {
    sLastTick = std::chrono::steady_clock::now();
    int startHour = Port_FeatureInt("graphics.day_night_cycle.start_time", 6);
    if (startHour < 0) {
        startHour = 0;
    } else if (startHour > 23) {
        startHour = 23;
    }
    sCycleMinutes = static_cast<float>(startHour * 60);
}

void MigrateLegacyCVar() {
    static bool migrated = false;
    if (migrated) {
        return;
    }

    auto context = Ship::Context::GetRawInstance();
    if (context == nullptr) {
        return;
    }

    auto conf = context->GetConfig();
    if (conf == nullptr) {
        return;
    }
    auto cvars = context->GetConsoleVariables();
    if (cvars == nullptr) {
        return;
    }

    const std::string legacyKey = CVAR_ENHANCEMENT("Graphics.DayNightCycle");
    auto legacyVar = cvars->Get(legacyKey.c_str());
    if (legacyVar == nullptr || legacyVar->Type != Ship::ConsoleVariableType::Integer) {
        return;
    }

    int32_t legacyValue = legacyVar->Integer;
    migrated = true;

    // ClearVariable removes both the runtime CVar and the persisted key, so a later
    // save cannot recreate the scalar/parent collision that caused the crash.
    cvars->ClearVariable(legacyKey.c_str());
    conf->SetInt(CVAR_ENHANCEMENT("Graphics.DayNightCycle.Enabled"), legacyValue);
    conf->Save();
    cvars->SetInteger(CVAR_ENHANCEMENT("Graphics.DayNightCycle.Enabled"), legacyValue);
}

bool IsGameplayMode() {
    switch (get_game_mode()) {
        case GAME_MODE_ENTER_DEMO_WORLD:
        case GAME_MODE_ENTER_WORLD:
        case GAME_MODE_WORLD:
        case GAME_MODE_CHANGE_MAP:
        case GAME_MODE_GAME_OVER:
        case GAME_MODE_BATTLE:
        case GAME_MODE_END_BATTLE:
        case GAME_MODE_PAUSE:
        case GAME_MODE_UNPAUSE:
            return true;
        default:
            return false;
    }
}

// Map 12 is kmr_21, the in-world title screen.
bool IsInWorldTitleMap() {
    return gGameStatusPtr->areaID == AREA_KMR && gGameStatusPtr->mapID == 12;
}

} // namespace

extern "C" s32 Port_GetDayNightTint(DayNightTint* out) {
    if (out == nullptr || !Port_FeatureInt("graphics.day_night_cycle", 0)) {
        return 0;
    }
    if (!IsTintableContext() || !IsOutdoorArea(gGameStatusPtr->areaID) || !IsGameplayMode() ||
        IsInWorldTitleMap()) {
        return 0;
    }

    *out = TintForHour(CurrentHour());
    return 1;
}

extern "C" void Port_DayNightDrawWorldOverlay(void) {
    DayNightTint tint;
    s32 visLeft;
    s32 visRight;
    s32 alpha;

    if (!Port_GetDayNightTint(&tint) || tint.alpha == 0) {
        return;
    }

    visLeft = OTRGetRectDimensionFromLeftEdge(0);
    visRight = OTRGetRectDimensionFromRightEdge(0);
    if (visLeft >= visRight) {
        return;
    }

    // The background already receives the full tint; ease the world pass so the
    // combined image stays readable.
    alpha = tint.alpha * 3 / 4;
    if (alpha == 0) {
        return;
    }

    gDPPipeSync(gMainGfxPos++);
    gDPSetColorDither(gMainGfxPos++, G_CD_MAGICSQ);
    gDPSetAlphaDither(gMainGfxPos++, G_AD_PATTERN);
    gDPSetCycleType(gMainGfxPos++, G_CYC_1CYCLE);
    gDPSetRenderMode(gMainGfxPos++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    gDPSetCombineMode(gMainGfxPos++, G_CC_PRIMITIVE, G_CC_PRIMITIVE);
    gDPSetPrimColor(gMainGfxPos++, 0, 0, tint.r, tint.g, tint.b, (u8) alpha);
    gDPSetScissor(gMainGfxPos++, G_SC_NON_INTERLACE, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT);
    gDPFillWideRectangle(gMainGfxPos++, visLeft, 0, visRight, SCREEN_HEIGHT);

    gDPSetColorDither(gMainGfxPos++, G_CD_DISABLE);
    gDPPipeSync(gMainGfxPos++);
}

void RegisterDayNightCycle_Init() {
    MigrateLegacyCVar();

    UNREGISTER_LISTENER(GameFrameUpdate, sUpdateListener);
    sUpdateListener = -1;

    if (!Port_FeatureInt("graphics.day_night_cycle", 0)) {
        sCycleInitialized = false;
        return;
    }

    int timeSource = Port_FeatureInt("graphics.day_night_cycle.time_source", 0);
    int startHour = Port_FeatureInt("graphics.day_night_cycle.start_time", 6);
    int dayLength = Port_FeatureInt("graphics.day_night_cycle.day_length", 24);
    if (!sCycleInitialized || timeSource != sLastTimeSource || startHour != sLastStartHour ||
        dayLength != sLastDayLength) {
        ResetCycle();
        sLastTimeSource = timeSource;
        sLastStartHour = startHour;
        sLastDayLength = dayLength;
        sCycleInitialized = true;
    }

    sUpdateListener = REGISTER_LISTENER(GameFrameUpdate, EVENT_PRIORITY_NORMAL, [](IEvent*) {
        TickCycle();
    });
}

static RegisterShipInitFunc initDayNightCycleFunc(
    RegisterDayNightCycle_Init,
    {
        CVAR_ENHANCEMENT("Graphics.DayNightCycle.Enabled"),
        CVAR_ENHANCEMENT("Graphics.DayNightCycle.TimeSource"),
        CVAR_ENHANCEMENT("Graphics.DayNightCycle.DaylightSaving"),
        CVAR_ENHANCEMENT("Graphics.DayNightCycle.DayLength"),
        CVAR_ENHANCEMENT("Graphics.DayNightCycle.StartTime"),
        CVAR_ENHANCEMENT("Graphics.DayNightCycle.FreezeTime"),
    }
);
