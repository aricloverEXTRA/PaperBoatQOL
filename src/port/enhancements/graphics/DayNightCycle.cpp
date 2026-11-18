#include "port/ShipInit.hpp"
#include "port/Engine.h"
#include "port/hooks/Events.h"
#include "port/ui/cvar_prefixes.h"

#include "common.h"
#include "model.h"

#include <chrono>
#include <cmath>
#include <ctime>
#include <vector>

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
    { 0.0f, 28, 38, 86, 180 },
    { 4.5f, 28, 38, 86, 180 },
    { 6.0f, 255, 160, 96, 100 },
    { 8.0f, 255, 255, 255, 0 },
    { 16.0f, 255, 255, 255, 0 },
    { 18.0f, 255, 145, 85, 100 },
    { 20.0f, 28, 38, 86, 180 },
    { 24.0f, 28, 38, 86, 180 },
};

struct TintState {
    u8 r;
    u8 g;
    u8 b;
    u8 alpha;
};

ListenerID sUpdateListener = -1;
ListenerID sDrawListener = -1;

std::chrono::steady_clock::time_point sLastTick;
float sCycleMinutes = DEFAULT_START_MINUTES;

bool sApplied = false;
s32 sAppliedArea = -1;
s32 sAppliedMap = -1;
u8 sSavedBackgroundTintMode = 0;
u8 sSavedShroudR = 255;
u8 sSavedShroudG = 255;
u8 sSavedShroudB = 255;
u8 sSavedShroudAlpha = 0;
std::vector<u8> sSavedModelTints;

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

float CurrentHour() {
    if (Port_FeatureInt("graphics.day_night_cycle.time_source", 0) == 1) {
        std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm localTime = {};
#if defined(_WIN32)
        localtime_s(&localTime, &now);
#else
        localtime_r(&now, &localTime);
#endif
        return static_cast<float>(localTime.tm_hour) + static_cast<float>(localTime.tm_min) / 60.0f +
               static_cast<float>(localTime.tm_sec) / 3600.0f;
    }

    return sCycleMinutes / 60.0f;
}

u8 LerpChannel(u8 from, u8 to, float t) {
    return static_cast<u8>(static_cast<int>(from) + (static_cast<int>(to) - static_cast<int>(from)) * t);
}

TintState TintForHour(float hour) {
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

    int strength = Port_FeatureInt("graphics.day_night_cycle.strength", 70);
    if (strength < 0) {
        strength = 0;
    } else if (strength > 100) {
        strength = 100;
    }

    return {
        LerpChannel(from->r, to->r, t),
        LerpChannel(from->g, to->g, t),
        LerpChannel(from->b, to->b, t),
        static_cast<u8>(LerpChannel(from->alpha, to->alpha, t) * strength / 100),
    };
}

void SaveState() {
    if (sApplied) {
        return;
    }

    sAppliedArea = gGameStatusPtr != nullptr ? gGameStatusPtr->areaID : -1;
    sAppliedMap = gGameStatusPtr != nullptr ? gGameStatusPtr->mapID : -1;

    if (gBackgroundTintModePtr != nullptr) {
        sSavedBackgroundTintMode = *gBackgroundTintModePtr;
    }
    mdl_get_shroud_tint_params(&sSavedShroudR, &sSavedShroudG, &sSavedShroudB, &sSavedShroudAlpha);

    sSavedModelTints.clear();
    if (gCurrentModels != nullptr) {
        const ModelList& models = *gCurrentModels;
        sSavedModelTints.reserve(ARRAY_COUNT(models));
        for (size_t i = 0; i < ARRAY_COUNT(models); ++i) {
            Model* model = models[i];
            sSavedModelTints.push_back(model != nullptr ? model->customGfxIndex : 0);
        }
    }

    sApplied = true;
}

bool SavedMapMatches() {
    return gGameStatusPtr != nullptr && gGameStatusPtr->areaID == sAppliedArea &&
           gGameStatusPtr->mapID == sAppliedMap;
}

void RestoreState() {
    if (!sApplied) {
        return;
    }

    if (SavedMapMatches()) {
        if (gBackgroundTintModePtr != nullptr) {
            *gBackgroundTintModePtr = sSavedBackgroundTintMode;
        }
        mdl_set_shroud_tint_params(sSavedShroudR, sSavedShroudG, sSavedShroudB, sSavedShroudAlpha);

        if (gCurrentModels != nullptr) {
            const ModelList& models = *gCurrentModels;
            size_t count = ARRAY_COUNT(models);
            if (count > sSavedModelTints.size()) {
                count = sSavedModelTints.size();
            }
            for (size_t i = 0; i < count; ++i) {
                Model* model = models[i];
                if (model != nullptr) {
                    model->customGfxIndex = sSavedModelTints[i];
                }
            }
        }
    }

    sSavedModelTints.clear();
    sApplied = false;
    sAppliedArea = -1;
    sAppliedMap = -1;
}

void TickCycle() {
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    float deltaSeconds = std::chrono::duration<float>(now - sLastTick).count();
    sLastTick = now;

    if (deltaSeconds < 0.0f || deltaSeconds > 1.0f) {
        deltaSeconds = 0.0f;
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

void ApplyCycle() {
    if (gGameStatusPtr == nullptr || gBackgroundTintModePtr == nullptr || gCurrentModels == nullptr ||
        !IsOutdoorArea(gGameStatusPtr->areaID)) {
        RestoreState();
        return;
    }

    if (sApplied && !SavedMapMatches()) {
        RestoreState();
    }

    SaveState();

    TintState tint = TintForHour(CurrentHour());
    mdl_set_shroud_tint_params(tint.r, tint.g, tint.b, tint.alpha);
    *gBackgroundTintModePtr = ENV_TINT_SHROUD;
    mdl_set_all_tint_type(ENV_TINT_SHROUD);
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

} // namespace

void RegisterDayNightCycle_Init() {
    RestoreState();

    UNREGISTER_LISTENER(GameFrameUpdate, sUpdateListener);
    UNREGISTER_LISTENER(BackgroundPreDraw, sDrawListener);
    sUpdateListener = -1;
    sDrawListener = -1;

    if (!Port_FeatureInt("graphics.day_night_cycle", 0)) {
        return;
    }

    ResetCycle();
    sUpdateListener = REGISTER_LISTENER(GameFrameUpdate, EVENT_PRIORITY_NORMAL, [](IEvent*) {
        TickCycle();
    });
    sDrawListener = REGISTER_LISTENER(BackgroundPreDraw, EVENT_PRIORITY_NORMAL, [](IEvent*) {
        ApplyCycle();
    });
}

static RegisterShipInitFunc initDayNightCycleFunc(
    RegisterDayNightCycle_Init,
    {
        CVAR_ENHANCEMENT("Graphics.DayNightCycle"),
        CVAR_ENHANCEMENT("Graphics.DayNightCycle.TimeSource"),
        CVAR_ENHANCEMENT("Graphics.DayNightCycle.DayLength"),
        CVAR_ENHANCEMENT("Graphics.DayNightCycle.StartTime"),
        CVAR_ENHANCEMENT("Graphics.DayNightCycle.Strength"),
    }
);
