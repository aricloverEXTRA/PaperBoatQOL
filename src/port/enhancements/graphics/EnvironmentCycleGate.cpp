#include "port/enhancements/graphics/EnvironmentCycleGate.h"

#include "common.h"
#include "game_modes.h"

namespace {

bool IsCycleContext() {
    if (gGameStatusPtr == nullptr || (gGameStatusPtr->demoBattleFlags & DEMO_BTL_FLAG_ENABLED)) {
        return false;
    }
    return gGameStatusPtr->context == CONTEXT_WORLD || gGameStatusPtr->context == CONTEXT_BATTLE;
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

// osr_03 is the castle destruction scene; kmr_30 is the post-credits "The End" scene.
bool IsFinaleScene() {
    return (gGameStatusPtr->areaID == AREA_OSR && gGameStatusPtr->mapID == 3) ||
           (gGameStatusPtr->areaID == AREA_KMR && gGameStatusPtr->mapID == 16);
}

} // namespace

bool Port_IsCycleArea(s32 areaID) {
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

bool Port_IsCycleGameplayActive(void) {
    if (!IsCycleContext() || !Port_IsCycleArea(gGameStatusPtr->areaID) || !IsGameplayMode()) {
        return false;
    }
    return !IsInWorldTitleMap() && !IsFinaleScene();
}
