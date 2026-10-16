/**
 * Performance Benchmark Integration for PaperBoat
 * 
 * This file provides real-time performance monitoring and benchmarking
 * capabilities integrated directly into the game engine.
 */

#include "common.h"
#include "port/Engine.h"
#include "port/hooks/Events.h"
#include <string>

#define BENCHMARK_MAX_SAMPLES 1000
#define BENCHMARK_DURATION_FRAMES 1800  // 30 seconds at 60 FPS

typedef struct BenchmarkState {
    b32 active;
    b32 enhancedShadingEnabled;
    s32 shadingMode;
    b32 dynamicLightsEnabled;
    b32 hemisphereEnabled;
    b32 rimLightingEnabled;
    b32 subsurfaceEnabled;
    b32 aoEnabled;
    s32 msaaLevel;
    f32 internalResolution;
    s32 resolutionWidth;
    s32 resolutionHeight;
    s32 areaID;
    s32 mapID;
    
    // Frame timing
    u32 frameCount;
    u32 sampleCount;
    f32 frameTimes[BENCHMARK_MAX_SAMPLES];
    u64 startTime;
    u64 endTime;
    u64 lastFrameTime;
    
    // Results
    f32 avgFPS;
    f32 minFPS;
    f32 maxFPS;
    f32 avgFrameTimeMS;
    f32 p1FrameTimeMS;
    f32 p99FrameTimeMS;
} BenchmarkState;

static BenchmarkState gBenchmarkState = {0};

extern void benchmark_start(const char* configName);
extern void benchmark_stop(void);
extern void benchmark_frame_update(void);
extern void benchmark_save_results(const char* filename);
extern void benchmark_print_results(void);

void benchmark_start(const char* configName) {
    if (gBenchmarkState.active) {
        return;
    }
    
    gBenchmarkState.active = 1;
    gBenchmarkState.frameCount = 0;
    gBenchmarkState.sampleCount = 0;
    gBenchmarkState.startTime = osGetTime();
    gBenchmarkState.lastFrameTime = 0;
    gBenchmarkState.avgFPS = 0.0f;
    gBenchmarkState.minFPS = 1000.0f;
    gBenchmarkState.maxFPS = 0.0f;
    
    // Capture current settings
    gBenchmarkState.enhancedShadingEnabled = CVarGetInteger(CVAR_ENHANCED_SHADING("Enabled"), 1);
    gBenchmarkState.shadingMode = CVarGetInteger(CVAR_ENHANCED_SHADING("Mode"), 1);
    gBenchmarkState.dynamicLightsEnabled = CVarGetInteger(CVAR_ENHANCED_SHADING("DynamicLights"), 1);
    gBenchmarkState.hemisphereEnabled = CVarGetInteger(CVAR_ENHANCED_SHADING("HemisphereLighting"), 1);
    gBenchmarkState.rimLightingEnabled = CVarGetInteger(CVAR_ENHANCED_SHADING("RimLighting"), 0);
    gBenchmarkState.subsurfaceEnabled = CVarGetInteger(CVAR_ENHANCED_SHADING("SubsurfaceScattering"), 0);
    gBenchmarkState.aoEnabled = CVarGetInteger(CVAR_ENHANCED_SHADING("AmbientOcclusion"), 0);
    gBenchmarkState.msaaLevel = CVarGetInteger(CVAR_MSAA_VALUE, 1);
    gBenchmarkState.internalResolution = CVarGetFloat(CVAR_INTERNAL_RESOLUTION, 1.0f);
    gBenchmarkState.resolutionWidth = SCREEN_WIDTH;
    gBenchmarkState.resolutionHeight = SCREEN_HEIGHT;
    gBenchmarkState.areaID = gGameStatusPtr->areaID;
    gBenchmarkState.mapID = gGameStatusPtr->mapID;
    
    osSyncPrintf("BENCHMARK STARTED: %s\n", configName);
    osSyncPrintf("  Enhanced Shading: %s (Mode: %d)\n", 
        gBenchmarkState.enhancedShadingEnabled ? "ON" : "OFF", gBenchmarkState.shadingMode);
    osSyncPrintf("  Dynamic Lights: %s\n", gBenchmarkState.dynamicLightsEnabled ? "ON" : "OFF");
    osSyncPrintf("  Hemisphere: %s | Rim: %s | Subsurface: %s | AO: %s\n",
        gBenchmarkState.hemisphereEnabled ? "ON" : "OFF",
        gBenchmarkState.rimLightingEnabled ? "ON" : "OFF",
        gBenchmarkState.subsurfaceEnabled ? "ON" : "OFF",
        gBenchmarkState.aoEnabled ? "ON" : "OFF");
    osSyncPrintf("  Resolution: %dx%d | MSAA: %dx | Internal Res: %.1fx\n",
        gBenchmarkState.resolutionWidth, gBenchmarkState.resolutionHeight,
        gBenchmarkState.msaaLevel, gBenchmarkState.internalResolution);
    osSyncPrintf("  Area: %d Map: %d\n", gBenchmarkState.areaID, gBenchmarkState.mapID);
}

void benchmark_stop(void) {
    if (!gBenchmarkState.active) {
        return;
    }
    
    gBenchmarkState.active = 0;
    gBenchmarkState.endTime = osGetTime();
    
    // Calculate results
    if (gBenchmarkState.sampleCount > 0) {
        f32 sum = 0.0f;
        f32 minFT = 1000.0f;
        f32 maxFT = 0.0f;
        
        for (u32 i = 0; i < gBenchmarkState.sampleCount; i++) {
            f32 ft = gBenchmarkState.frameTimes[i];
            sum += ft;
            if (ft < minFT) minFT = ft;
            if (ft > maxFT) maxFT = ft;
        }
        
        gBenchmarkState.avgFrameTimeMS = sum / gBenchmarkState.sampleCount;
        gBenchmarkState.avgFPS = 1000.0f / gBenchmarkState.avgFrameTimeMS;
        gBenchmarkState.minFPS = 1000.0f / maxFT;
        gBenchmarkState.maxFPS = 1000.0f / minFT;
        
        // Calculate percentiles (simple bubble sort for small arrays)
        f32 sorted[BENCHMARK_MAX_SAMPLES];
        for (u32 i = 0; i < gBenchmarkState.sampleCount; i++) {
            sorted[i] = gBenchmarkState.frameTimes[i];
        }
        
        for (u32 i = 0; i < gBenchmarkState.sampleCount - 1; i++) {
            for (u32 j = 0; j < gBenchmarkState.sampleCount - i - 1; j++) {
                if (sorted[j] > sorted[j + 1]) {
                    f32 tmp = sorted[j];
                    sorted[j] = sorted[j + 1];
                    sorted[j + 1] = tmp;
                }
            }
        }
        
        u32 p1Idx = gBenchmarkState.sampleCount / 100;
        u32 p99Idx = (gBenchmarkState.sampleCount * 99) / 100;
        gBenchmarkState.p1FrameTimeMS = sorted[p1Idx];
        gBenchmarkState.p99FrameTimeMS = sorted[p99Idx];
    }
    
    osSyncPrintf("BENCHMARK COMPLETED\n");
    osSyncPrintf("  Frames: %u | Duration: %.1fs\n", 
        gBenchmarkState.frameCount, (gBenchmarkState.endTime - gBenchmarkState.startTime) / 1000.0f);
    osSyncPrintf("  Avg FPS: %.1f | Min: %.1f | Max: %.1f\n", 
        gBenchmarkState.avgFPS, gBenchmarkState.minFPS, gBenchmarkState.maxFPS);
    osSyncPrintf("  Avg Frame Time: %.2fms | 1%%: %.2fms | 99%%: %.2fms\n",
        gBenchmarkState.avgFrameTimeMS, gBenchmarkState.p1FrameTimeMS, gBenchmarkState.p99FrameTimeMS);
}

void benchmark_frame_update(void) {
    if (!gBenchmarkState.active) {
        return;
    }
    
    gBenchmarkState.frameCount++;
    
    // Sample every frame for detailed analysis
    if (gBenchmarkState.sampleCount < BENCHMARK_MAX_SAMPLES) {
        u64 now = osGetTime();
        if (gBenchmarkState.lastFrameTime != 0) {
            f32 frameTime = (f32)(now - gBenchmarkState.lastFrameTime);
            if (frameTime > 0.0f) {
                gBenchmarkState.frameTimes[gBenchmarkState.sampleCount] = frameTime;
                gBenchmarkState.sampleCount++;
            }
        }
        gBenchmarkState.lastFrameTime = now;
    }
    
    // Auto-stop after duration
    if (gBenchmarkState.frameCount >= BENCHMARK_DURATION_FRAMES) {
        benchmark_stop();
    }
}

void benchmark_save_results(const char* filename) {
    // Save results to file
    // In a real implementation, this would write JSON to the filesystem
    osSyncPrintf("Benchmark results would be saved to: %s\n", filename);
}

void benchmark_print_results(void) {
    osSyncPrintf("\n=== BENCHMARK RESULTS ===\n");
    osSyncPrintf("Configuration:\n");
    osSyncPrintf("  Enhanced Shading: %s (Mode %d)\n", 
        gBenchmarkState.enhancedShadingEnabled ? "Enabled" : "Disabled", gBenchmarkState.shadingMode);
    osSyncPrintf("  Dynamic Lights: %s\n", gBenchmarkState.dynamicLightsEnabled ? "Enabled" : "Disabled");
    osSyncPrintf("  Hemisphere: %s\n", gBenchmarkState.hemisphereEnabled ? "Enabled" : "Disabled");
    osSyncPrintf("  Rim Lighting: %s\n", gBenchmarkState.rimLightingEnabled ? "Enabled" : "Disabled");
    osSyncPrintf("  Subsurface: %s\n", gBenchmarkState.subsurfaceEnabled ? "Enabled" : "Disabled");
    osSyncPrintf("  Ambient Occlusion: %s\n", gBenchmarkState.aoEnabled ? "Enabled" : "Disabled");
    osSyncPrintf("  Resolution: %dx%d @ %dx MSAA\n", 
        gBenchmarkState.resolutionWidth, gBenchmarkState.resolutionHeight, gBenchmarkState.msaaLevel);
    osSyncPrintf("  Internal Resolution: %.1fx\n", gBenchmarkState.internalResolution);
    osSyncPrintf("  Area: %d Map: %d\n", gBenchmarkState.areaID, gBenchmarkState.mapID);
    osSyncPrintf("\nPerformance:\n");
    osSyncPrintf("  Average FPS: %.1f\n", gBenchmarkState.avgFPS);
    osSyncPrintf("  Minimum FPS: %.1f\n", gBenchmarkState.minFPS);
    osSyncPrintf("  Maximum FPS: %.1f\n", gBenchmarkState.maxFPS);
    osSyncPrintf("  Average Frame Time: %.2f ms\n", gBenchmarkState.avgFrameTimeMS);
    osSyncPrintf("  1%% Low Frame Time: %.2f ms\n", gBenchmarkState.p1FrameTimeMS);
    osSyncPrintf("  99%% High Frame Time: %.2f ms\n", gBenchmarkState.p99FrameTimeMS);
    
    // Performance rating
    osSyncPrintf("\nRating: ");
    if (gBenchmarkState.avgFPS >= 55.0f && gBenchmarkState.p1FrameTimeMS <= 18.0f) {
        osSyncPrintf("EXCELLENT - Perfect for GT 1030 and above\n");
    } else if (gBenchmarkState.avgFPS >= 30.0f && gBenchmarkState.p1FrameTimeMS <= 33.0f) {
        osSyncPrintf("GOOD - Playable on GT 1030\n");
    } else if (gBenchmarkState.avgFPS >= 20.0f) {
        osSyncPrintf("ACCEPTABLE - May need lower settings\n");
    } else {
        osSyncPrintf("POOR - Requires optimization or lower settings\n");
    }
}

// Register benchmark frame update hook
void register_benchmark_hooks(void) {
    REGISTER_LISTENER(GameFrameUpdate, EVENT_PRIORITY_NORMAL, [](IEvent*) { benchmark_frame_update(); });
}