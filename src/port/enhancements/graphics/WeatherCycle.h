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

typedef struct WeatherAmbient {
    u8 r;
    u8 g;
    u8 b;
    u8 alpha;
    u8 starDim;
} WeatherAmbient;

s32 Port_GetCurrentWeather(void);
s32 Port_GetWeatherAmbient(WeatherAmbient* out);
void Port_WeatherDrawSkyOverlay(void);

#ifdef __cplusplus
}
#endif

#endif
