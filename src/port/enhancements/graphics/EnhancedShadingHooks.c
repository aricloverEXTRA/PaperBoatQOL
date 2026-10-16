#include "common.h"
#include "port/hooks/impl/EventSystem.h"
#include "sprite/sprite_enhanced_shading.h"

static void on_map_load_handler(IEvent* event) {
    const char* mapName;

    if (gGameStatusPtr == NULL) {
        return;
    }

    mapName = ((OnMapLoad*)event)->mapName;
    enhanced_shading_load_area_defaults(mapName, gGameStatusPtr->mapID, gGameStatusPtr->entryID);
}

static void on_map_ready_handler(IEvent* event) {
    const char* mapName;
    b32 enabled;

    if (gGameStatusPtr == NULL) {
        return;
    }

    mapName = ((OnMapReady*)event)->mapName;

    enabled = CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
    if (enabled) {
        s32 mode = CVarGetInteger(CVAR_ENHANCED_SHADING("Mode"), 0);
        if (mode == 1) {
            enhanced_shading_load_area_defaults(mapName, gGameStatusPtr->mapID, gGameStatusPtr->entryID);
        } else if (mode == 2) {
            b32 fallback = CVarGetInteger(CVAR_ENHANCED_SHADING("FallbackToVanilla"), 1);
            enhanced_shading_set_fallback_to_vanilla(fallback);
        }
    }
}

void register_enhanced_shading_hooks(void) {
    REGISTER_LISTENER(OnMapLoad, EVENT_PRIORITY_NORMAL, on_map_load_handler);
    REGISTER_LISTENER(OnMapReady, EVENT_PRIORITY_NORMAL, on_map_ready_handler);
}
