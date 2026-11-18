#ifndef PORT_DAY_NIGHT_CYCLE_H
#define PORT_DAY_NIGHT_CYCLE_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct DayNightTint {
    u8 r;
    u8 g;
    u8 b;
    u8 alpha;
    u8 stars;
} DayNightTint;

s32 Port_GetDayNightTint(DayNightTint* out);
void Port_DayNightDrawWorldOverlay(void);

#ifdef __cplusplus
}
#endif

#endif
