#include "port/ShipInit.hpp"
#include "port/Engine.h"
#include "port/hooks/Events.h"
#include "port/ui/cvar_prefixes.h"
#include <libultraship/bridge/consolevariablebridge.h>

#include <cmath>

#define LETTERBOX_VIEW_X SCREEN_INSET_X
#define LETTERBOX_VIEW_Y SCREEN_INSET_Y
#define LETTERBOX_VIEW_W (SCREEN_WIDTH - 2 * SCREEN_INSET_X)
#define LETTERBOX_VIEW_H (SCREEN_HEIGHT - 2 * SCREEN_INSET_Y)

#define STATUS_BAR_VANILLA_Y 18
#define BTL_MENU_DROP        SCREEN_INSET_Y
#define REEL_MIRROR_DX       37

extern "C" {
extern Camera gCameras[4];
extern GameStatus* gGameStatusPtr;
extern Gfx* gMainGfxPos;
void set_cam_viewport(s16 id, s16 x, s16 y, s16 width, s16 height);
void hud_element_set_render_pos(s32 id, s32 x, s32 y);
void hud_element_set_transform_scale(s32 id, f32 x, f32 y, f32 z);
void hud_element_draw_complex_hud_next(s32 hid);
// Engine: publishes the camera view-projection matrix for depth-based
// post-processing.
void gfx_set_camera_view_projection(const float out[4][4]);
// Engine: publishes the camera world position for depth-based post-processing.
void gfx_set_camera_world_position(const float pos[3]);
}

// Defined in 43F0.c; transforms a point (row-vector convention) by a Matrix4f.
extern "C" void transform_point(Matrix4f mtx, f32 inX, f32 inY, f32 inZ, f32 inS, f32* outX, f32* outY, f32* outZ,
                                f32* outW);

static s32 cam_abs(s32 v) {
    return v < 0 ? -v : v;
}

static bool cam_is_letterbox_rect(s32 x, s32 y, s32 w, s32 h) {
    return x == LETTERBOX_VIEW_X && y == LETTERBOX_VIEW_Y && w == LETTERBOX_VIEW_W && h == LETTERBOX_VIEW_H;
}

static bool cam_is_letterboxed(Camera* cam) {
    return cam_is_letterbox_rect(cam->viewportStartX, cam->viewportStartY, cam->viewportW, cam->viewportH);
}

static bool cam_demo_active(void) {
    return gGameStatusPtr->demoState != DEMO_STATE_NONE;
}

static bool cam_view_is_widened(void) {
    if (cam_demo_active() && cam_is_letterboxed(&gCameras[CAM_DEFAULT])) {
        return false;
    }
    return OTRGetRectDimensionFromLeftEdge(0) < 0;
}

static bool cam_has_fullscreen_viewport(s32 camID) {
    return camID == CAM_DEFAULT || camID == CAM_BATTLE
        || gCameras[camID].viewportW >= SCREEN_WIDTH - 2 * SCREEN_INSET_X;
}

static bool cam_is_framed(s32 camID) {
    return (camID == CAM_DEFAULT || camID == CAM_BATTLE) && !cam_view_is_widened();
}

extern "C" void get_cam_frame_x(s32 camID, s32* left, s32* right) {
    Camera* camera = &gCameras[camID];
    s32 startX = 0;
    s32 endX = SCREEN_WIDTH;

    if (!cam_view_is_widened()) {
        startX = camera->viewportStartX;
        endX = camera->viewportStartX + cam_abs(camera->viewportW);
    }

    *left = OTRGetRectDimensionFromLeftEdge(startX);
    *right = OTRGetRectDimensionFromRightEdge(SCREEN_WIDTH - endX);
}

extern "C" void get_cam_scissor_x(s32 camID, s32* left, s32* right) {
    Camera* camera = &gCameras[camID];

    if (cam_is_framed(camID)) {
        get_cam_frame_x(camID, left, right);
        *left = OTRGetScissorCoordX(*left);
        *right = OTRGetScissorCoordX(*right);
    } else if (cam_has_fullscreen_viewport(camID)) {
        *left = 0;
        *right = SCREEN_WIDTH;
    } else {
        *left = OTRGetScissorCoordX(camera->viewportStartX);
        *right = OTRGetScissorCoordX(camera->viewportStartX + camera->viewportW);
    }
}

static void cam_fit_viewport(s32 camID, Camera* camera) {
    s32 left;
    s32 right;
    s32 centerX;
    f32 zoom;
    f32 centerY;

    if (cam_is_framed(camID)) {
        get_cam_scissor_x(camID, &left, &right);
        zoom = (f32) (right - left) / cam_abs(camera->viewportW);
        centerY = SCREEN_HEIGHT / 2 + (camera->viewportStartY + camera->viewportH / 2 - SCREEN_HEIGHT / 2) * zoom;

        camera->vp.vp.vscale[0] = 2.0f * (right - left);
        camera->vp.vp.vscale[1] = 2.0f * camera->viewportH * zoom;
        camera->vp.vp.vtrans[0] = 4 * (left + (right - left) / 2);
        camera->vp.vp.vtrans[1] = 4.0f * centerY;
        camera->vpAlt.vp.vscale[0] = camera->vp.vp.vscale[0];
        camera->vpAlt.vp.vscale[1] = camera->vp.vp.vscale[1];
        camera->vpAlt.vp.vtrans[0] = gGameStatusPtr->altViewportOffset.x + camera->vp.vp.vtrans[0];
        camera->vpAlt.vp.vtrans[1] = gGameStatusPtr->altViewportOffset.y + camera->vp.vp.vtrans[1];
        return;
    }

    if (camID == CAM_DEFAULT || camID == CAM_BATTLE) {
        camera->vp.vp.vscale[0] = 2.0f * SCREEN_WIDTH;
        camera->vp.vp.vtrans[0] = 4 * (SCREEN_WIDTH / 2);
        camera->vpAlt.vp.vscale[0] = camera->vp.vp.vscale[0];
        camera->vpAlt.vp.vtrans[0] = gGameStatusPtr->altViewportOffset.x + camera->vp.vp.vtrans[0];
        return;
    }

    if (cam_has_fullscreen_viewport(camID)) {
        return;
    }

    centerX = OTRGetScissorCoordX(camera->viewportStartX + (camera->viewportW / 2));
    camera->vp.vp.vtrans[0] = 4 * centerX;
    camera->vpAlt.vp.vtrans[0] = gGameStatusPtr->altViewportOffset.x + 4 * centerX;
}

static f32 cam_get_display_aspect(s32 camID, Camera* camera) {
    f32 nativeW = cam_abs(camera->viewportW);

    if (cam_view_is_widened() && (camID == CAM_DEFAULT || camID == CAM_BATTLE)) {
        nativeW = SCREEN_WIDTH;
    }

    return nativeW / (f32) camera->viewportH;
}

// FULL HEIGHT VIEW

static bool cam_full_height_enabled(void) {
    return CVarGetInteger(CVAR_ENHANCEMENT("Graphics.FullHeightView"), 0) != 0 && !cam_demo_active();
}

static bool cam_is_full_height_rect(s32 x, s32 y, s32 w, s32 h) {
    return x == LETTERBOX_VIEW_X && y == 0 && w == LETTERBOX_VIEW_W && h == SCREEN_HEIGHT;
}

static bool cam_is_full_height(Camera* cam) {
    return cam_is_full_height_rect(cam->viewportStartX, cam->viewportStartY, cam->viewportW, cam->viewportH);
}

extern "C" b32 port_cam_full_height(s32 camID) {
    if (camID != CAM_DEFAULT && camID != CAM_BATTLE) {
        return false;
    }

    return cam_full_height_enabled() && cam_is_full_height(&gCameras[camID]);
}

extern "C" b32 port_hud_full_height(void) {
    return port_cam_full_height(gGameStatusPtr->context == CONTEXT_BATTLE ? CAM_BATTLE : CAM_DEFAULT);
}

extern "C" s32 port_hud_clip_top(void) {
    return port_hud_full_height() ? 0 : SCREEN_INSET_Y;
}

extern "C" s32 port_hud_clip_bottom(void) {
    return SCREEN_HEIGHT - port_hud_clip_top();
}

extern "C" s32 port_status_bar_y(void) {
    return port_hud_full_height() ? 0 : STATUS_BAR_VANILLA_Y;
}

extern "C" s32 port_btl_menu_y(void) {
    return port_cam_full_height(CAM_BATTLE) ? BTL_MENU_DROP : 0;
}

static void cam_sync_full_height(void) {
    static const s32 ids[] = { CAM_DEFAULT, CAM_BATTLE };
    const bool on = cam_full_height_enabled();

    for (s32 id : ids) {
        Camera* camera = &gCameras[id];

        if (on && cam_is_letterboxed(camera)) {
            set_cam_viewport(id, LETTERBOX_VIEW_X, 0, LETTERBOX_VIEW_W, SCREEN_HEIGHT);
        } else if (!on && cam_is_full_height(camera)) {
            set_cam_viewport(id, LETTERBOX_VIEW_X, LETTERBOX_VIEW_Y, LETTERBOX_VIEW_W, LETTERBOX_VIEW_H);
        }
    }
}

// Projects the world sun direction to screen space for the god-rays post pass.
// The port publishes the camera view-projection matrix (row-major, row-vector:
// clip = world * VP, matching cam_main.c's guMtxCatF) via gfx_set_camera_view_projection;
// we reuse it here so the shafts track the sun as the camera turns. Returns the
// sun's uv and whether it is in front of the camera and on screen.
extern "C" void port_get_sun_screen_uv(float* outU, float* outV, int* outVisible) {
    *outU = 0.5f;
    *outV = 0.5f;
    *outVisible = 0;

    const float az = CVarGetFloat(CVAR_ENHANCED_SHADING("SunAzimuth"), 45.0f) * 3.14159265f / 180.0f;
    const float el =
        CVarGetFloat(CVAR_ENHANCED_SHADING("SunElevation"), 55.0f) * 3.14159265f / 180.0f;
    const float sunDir[3] = { cosf(el) * sinf(az), sinf(el), cosf(el) * cosf(az) };

    const Vec3f* eye = &gCameras[CAM_DEFAULT].lookAt_eye;
    // A point far along the sun direction from the camera, in world space.
    const float px = eye->x + sunDir[0] * 1000.0f;
    const float py = eye->y + sunDir[1] * 1000.0f;
    const float pz = eye->z + sunDir[2] * 1000.0f;

    // mtxPerspective is V*P (row-vector convention), exactly what the world pass
    // loads; transform_point applies it the same way the rest of the game does.
    f32 cx;
    f32 cy;
    f32 cz;
    f32 cw;
    transform_point(gCameras[CAM_DEFAULT].mtxPerspective, px, py, pz, 1.0f, &cx, &cy, &cz, &cw);

    if (cw <= 0.0001f) {
        return; // sun is behind the camera
    }

    const float ndcX = cx / cw;
    const float ndcY = cy / cw;
    const float u = ndcX * 0.5f + 0.5f;
    const float v = 0.5f - ndcY * 0.5f; // hardware NDC -> uv (y flipped)

    if (u < -0.5f || u > 1.5f || v < -0.5f || v > 1.5f) {
        return; // sun is well off screen
    }

    *outU = u;
    *outV = v;
    *outVisible = 1;
}

static void RegisterCameraPatches_Init() {
    REGISTER_LISTENER(CameraSetViewport, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        auto* ev = (CameraSetViewport*) event;

        if (!cam_full_height_enabled() || (ev->camID != CAM_DEFAULT && ev->camID != CAM_BATTLE)) {
            return;
        }
        if (!cam_is_letterbox_rect(*ev->x, *ev->y, *ev->width, *ev->height)) {
            return;
        }

        *ev->y = 0;
        *ev->height = SCREEN_HEIGHT;
    });

    REGISTER_LISTENER(GameFrameUpdate, EVENT_PRIORITY_NORMAL, [](IEvent*) { cam_sync_full_height(); });

    REGISTER_LISTENER(BattleMenuDrawReel, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        auto* ev = (BattleMenuDrawReel*) event;

        if (!cam_view_is_widened() || !CVarGetInteger(CVAR_ENHANCEMENT("Graphics.RoundedReel"), 0)) {
            return;
        }

        // The HUD scissor (12..308) cuts into the mirrored half on some wide ratios,
        // so clip that half to the window edges instead.
        gDPSetScissor(gMainGfxPos++, G_SC_NON_INTERLACE, 0, port_hud_clip_top(), SCREEN_WIDTH, port_hud_clip_bottom());
        hud_element_set_transform_scale(ev->hid, -1.0f, 1.0f, 1.0f);
        hud_element_set_render_pos(ev->hid, ev->x - REEL_MIRROR_DX, ev->y);
        hud_element_draw_complex_hud_next(ev->hid);
        hud_element_set_transform_scale(ev->hid, 1.0f, 1.0f, 1.0f);
        gDPSetScissor(gMainGfxPos++, G_SC_NON_INTERLACE, 12, port_hud_clip_top(), 308, port_hud_clip_bottom());
    });

    REGISTER_LISTENER(CameraFitViewport, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        auto* ev = (CameraFitViewport*) event;
        cam_fit_viewport(ev->camID, ev->camera);
    });

    REGISTER_LISTENER(CameraPerspective, EVENT_PRIORITY_NORMAL, [](IEvent* event) {
        auto* ev = (CameraPerspective*) event;

        if (ev->camera->viewportH != 0) {
            *ev->aspect = cam_get_display_aspect(ev->camID, ev->camera);
        }
    });

    // Publishes the world camera's view-projection matrix once per rendered
    // frame. cam_main.c folds the view into the projection with
    // guMtxCatF(mtxViewPlayer, mtxPerspective, mtxPerspective), so
    // cam->mtxPerspective is already V*P and is exactly what the world pass
    // loads. Hand it to the engine here (frame start, before the world draws) so
    // a depth-based post pass can reconstruct world-space geometry.
    REGISTER_LISTENER(GameFrameUpdate, EVENT_PRIORITY_NORMAL, [](IEvent*) {
        gfx_set_camera_view_projection(gCameras[CAM_DEFAULT].mtxPerspective);
        // Publish camera world position for depth-based post-processing shaders
        // The camera eye position is in lookAt_eye
        float camPos[3] = {
            gCameras[CAM_DEFAULT].lookAt_eye.x,
            gCameras[CAM_DEFAULT].lookAt_eye.y,
            gCameras[CAM_DEFAULT].lookAt_eye.z
        };
        gfx_set_camera_world_position(camPos);
    });
}

static RegisterShipInitFunc cameraPatchesInitFunc(RegisterCameraPatches_Init);
