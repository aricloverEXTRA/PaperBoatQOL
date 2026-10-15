#include "common.h"
#include "port/hooks/Events.h"
#include "port/hooks/EventSystem.h"
#include "sprite/sprite_enhanced_shading.h"

static void on_map_load_handler(const char* mapName) {
    if (gGameStatusPtr == NULL) return;
    
    s32 areaID = gGameStatusPtr->areaID;
    s32 mapID = gGameStatusPtr->mapID;
    s32 entryID = gGameStatusPtr->entryID;
    
    enhanced_shading_load_area_defaults(mapName, mapID, entryID);
}

static void on_map_ready_handler(const char* mapName) {
    if (gGameStatusPtr == NULL) return;
    
    s32 areaID = gGameStatusPtr->areaID;
    s32 mapID = gGameStatusPtr->mapID;
    s32 entryID = gGameStatusPtr->entryID;
    
    b32 enabled = CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
    if (enabled) {
        int mode = CVarGetInteger(CVAR_ENHANCED_SHADING("Mode"), 0);
        if (mode == 1) {
            enhanced_shading_load_area_defaults(mapName, mapID, entryID);
        } else if (mode == 2) {
            b32 fallback = CVarGetInteger(CVAR_ENHANCED_SHADING("FallbackToVanilla"), 1);
            enhanced_shading_set_fallback_to_vanilla(fallback);
        }
    }
}

void register_enhanced_shading_hooks(void) {
    REGISTER_EVENT_HANDLER(OnMapLoad, on_map_load_handler);
    REGISTER_EVENT_HANDLER(OnMapReady, on_map_ready_handler);
}