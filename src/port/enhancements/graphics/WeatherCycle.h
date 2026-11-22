#ifndef PORT_WEATHER_CYCLE_H
#define PORT_WEATHER_CYCLE_H

#include "common.h"

#ifdef __cplusplus
extern "C" {
#endif

enum WeatherType {
    WEATHER_TYPE_CLEAR = 0,
    WEATHER_TYPE_RAIN = 1,
    WEATHER_TYPE_THUNDERSTORM = 2,
    WEATHER_TYPE_SNOW = 3,
};

s32 Port_GetCurrentWeather(void);
void Port_WeatherDrawWorldOverlay(void);

#ifdef __cplusplus
}
#endif

#endif
