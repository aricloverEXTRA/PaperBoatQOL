#include "common.h"
#include "effects_internal.h"
#include "assets/effects.h"
#include "assets/map_textures.h"

#define SNOWFALL_VARIANT_SNOW  0
#define SNOWFALL_VARIANT_RAIN  1
#define SNOWFALL_VARIANT_STORM 2
#define RAIN_VERTEX_POOL_SIZE  128

// gSPVertex stores the pointer in the display list, so the quad data must outlive
// this appendGfx call. Particles are copied into the renderer at execution time.
static Vtx sRainVerts[RAIN_VERTEX_POOL_SIZE][4];

void snowfall_init(EffectInstance* effect);
void snowfall_update(EffectInstance* effect);
void snowfall_render(EffectInstance* effect);
void snowfall_appendGfx(void* effect);


const char* D_E008AA50[] = { D_09000D50_38DDC0, D_09000D50_38DDC0, D_09000D50_38DDC0 };

void func_E008A000(SnowfallFXData* data, s32 variant) {
    Camera* camera = &gCameras[gCurrentCameraID];

    data->unk_08 = (camera->lookAt_eye.x + effect_rand_int(2000)) - 1000.0f;
    data->unk_0C = (camera->lookAt_eye.y + effect_rand_int(2000)) - 1000.0f;
    data->unk_10 = (camera->lookAt_eye.z + effect_rand_int(2000)) - 1000.0f;
    data->unk_04 = variant;
    if (variant == SNOWFALL_VARIANT_SNOW) {
        data->unk_14 = (effect_rand_int(20) - 10.0f) * 0.05;
        data->unk_18 = -1.2 - (effect_rand_int(80) * 0.01);
        data->unk_1C = (effect_rand_int(20) - 10.0f) * 0.05;
    } else {
        data->unk_14 = 1.2f + (effect_rand_int(10) - 5.0f) * 0.08f;
        data->unk_18 = -13.0f - (effect_rand_int(80) * 0.08f);
        data->unk_1C = (effect_rand_int(10) - 5.0f) * 0.02f;
        if (variant == SNOWFALL_VARIANT_STORM) {
            data->unk_14 += 1.2f;
            data->unk_18 -= 5.0f;
        }
    }
    data->unk_28 = 255;
    data->wrapEpoch = 0;
}

EffectInstance* snowfall_main(s32 arg0, s32 arg1) {
    EffectBlueprint effectBp;
    EffectInstance* effect;
    SnowfallFXData* data;
    s32 numParts = arg1 + 1;
    s32 i;

    effectBp.init = snowfall_init;
    effectBp.update = snowfall_update;
    effectBp.renderScene = snowfall_render;
    effectBp.unk_00 = 0;
    effectBp.renderUI = nullptr;
    effectBp.effectID = EFFECT_SNOWFALL;

    effect = create_effect_instance(&effectBp);
    effect->numParts = numParts;

    data = effect->data.snowfall = general_heap_malloc(numParts * sizeof(*data));
    ASSERT(data != nullptr);

    data->timeLeft = 100;
    data->lifeTime = 0;
    data->unk_28 = 255;
    if (arg0 == SNOWFALL_VARIANT_RAIN) {
        data->unk_28 = 175;
    } else if (arg0 == SNOWFALL_VARIANT_STORM) {
        data->unk_28 = 205;
    }
    data->unk_04 = arg0;
    data->unk_2C = arg1;
    data->unk_00 = 1;

    data++;
    for (i = 0; i < arg1; i++, data++) {
        func_E008A000(data, arg0);
        data->unk_30 = 0;
    }

    return effect;
}

void snowfall_init(EffectInstance* effect) {
}

void func_E008A290(SnowfallFXData* data, s32 arg1, s32 arg2, s32 arg3) {
    Camera* camera = &gCameras[gCurrentCameraID];
    f32 unk_08 = data->unk_08;
    f32 unk_0C = data->unk_0C;
    f32 unk_10 = data->unk_10;
    f32 lookX = camera->lookAt_obj.x;
    f32 lookY = camera->lookAt_obj.y;
    f32 lookZ = camera->lookAt_obj.z;
    s32 temp_f12;
    s32 temp_f12_2;
    s32 temp_f12_3;

    temp_f12 = unk_08 - lookX;
    if (arg1 < temp_f12) {
        temp_f12 = temp_f12 % arg1;
        data->unk_08 = (lookX - arg1) + temp_f12;
        data->wrapEpoch++;
    } else if (-arg1 > temp_f12) {
        temp_f12 = -temp_f12;
        temp_f12 = temp_f12 % arg1;
        data->unk_08 = (lookX + arg1) - temp_f12;
        data->wrapEpoch++;
    }

    temp_f12_2 = unk_0C - lookY;
    if (arg2 < temp_f12_2) {
        temp_f12_2 = temp_f12_2 % arg2;
        data->unk_0C = (lookY - arg2) + temp_f12_2;
        data->wrapEpoch++;
    } else if (temp_f12_2 < -arg2) {
        if (arg3 < 100) {
            data->unk_18 = 0;
            data->unk_28 = 0;
        } else {
            temp_f12_2 = -temp_f12_2;
            temp_f12_2 = temp_f12_2 % arg2;
            data->unk_0C = (lookY + arg2) - temp_f12_2;
            data->wrapEpoch++;
        }
    }

    temp_f12_3 = unk_10 - lookZ;
    if (arg1 < temp_f12_3) {
        temp_f12_3 = temp_f12_3 % arg1;
        data->unk_10 = (lookZ - arg1) + temp_f12_3;
        data->wrapEpoch++;
    } else if (temp_f12_3 < -arg1) {
        temp_f12_3 = -temp_f12_3;
        temp_f12_3 = temp_f12_3 % arg1;
        data->unk_10 = (lookZ + arg1) - temp_f12_3;
        data->wrapEpoch++;
    }
}

void snowfall_update(EffectInstance* effect) {
    Camera* camera = &gCameras[gCurrentCameraID];
    SnowfallFXData* data = effect->data.snowfall;
    s32 timeLeft;
    s32 boomLength;
    s32 temp_s5;
    s32 temp_s6;
    s32 i;

    if (data->timeLeft < 100) {
        data->timeLeft--;
    }
    if (data->timeLeft < 0) {
        remove_effect(effect);
        return;
    }

    data->lifeTime++;
    timeLeft = data->timeLeft;
    temp_s5 = data->unk_2C;
    if (timeLeft < 10) {
        data->unk_28 = timeLeft * 25;
    }
    boomLength = camera->curBoomLength;
    temp_s6 = (boomLength * 0.3) + 1.0;

    data++;
    for (i = 0; i < temp_s5; i++, data++) {
        if (data->unk_30 <= 0 || --data->unk_30 <= 0) {
            data->unk_08 += data->unk_14;
            data->unk_0C += data->unk_18;
            data->unk_10 += data->unk_1C;
            func_E008A290(data, boomLength, temp_s6, timeLeft);
        }
    }
}

void snowfall_render(EffectInstance* effect) {
    RenderTask renderTask;
    RenderTask* retTask;

    renderTask.appendGfx = snowfall_appendGfx;
    renderTask.appendGfxArg = effect;
    renderTask.dist = 0;
    renderTask.renderMode = RENDER_MODE_CLOUD_NO_ZCMP;

    retTask = queue_render_task(&renderTask);
    retTask->renderMode |= RENDER_TASK_FLAG_REFLECT_FLOOR;
}

static void snowfall_appendGfxRain(SnowfallFXData* data, u8 alpha, s32 count) {
    Camera* camera = &gCameras[gCurrentCameraID];
    Matrix4f sp20;
    Mtx* rotationMtx;
    Mtx* translationMtx;
    s32 i;

    if (alpha == 0 || count <= 0) {
        return;
    }
    if (count > RAIN_VERTEX_POOL_SIZE) {
        count = RAIN_VERTEX_POOL_SIZE;
    }

    gDPPipeSync(gMainGfxPos++);
    gDPSetCycleType(gMainGfxPos++, G_CYC_1CYCLE);
    gDPSetTexturePersp(gMainGfxPos++, G_TP_NONE);
    gDPSetTextureFilter(gMainGfxPos++, G_TF_POINT);
    gDPSetTextureLUT(gMainGfxPos++, G_TT_NONE);
    gDPSetTextureDetail(gMainGfxPos++, G_TD_CLAMP);
    gDPSetTextureLOD(gMainGfxPos++, G_TL_TILE);
    gDPSetColorDither(gMainGfxPos++, G_CD_DISABLE);
    gDPSetAlphaDither(gMainGfxPos++, G_AD_NOISE);
    gDPSetRenderMode(gMainGfxPos++, G_RM_CLD_SURF, G_RM_CLD_SURF2);
    gSPTexture(gMainGfxPos++, 0xFFFF, 0xFFFF, 0, G_TX_RENDERTILE, G_ON);
    gDPSetCombineMode(gMainGfxPos++, G_CC_MODULATEIA_PRIM, G_CC_MODULATEIA_PRIM);
    gDPSetPrimColor(gMainGfxPos++, 0, 0, 180, 220, 255, alpha);
    gDPLoadTextureTile(
        gMainGfxPos++, iwa_dropwater_btif, G_IM_FMT_I, G_IM_SIZ_8b, 32, 64, 0, 0, 31, 63, 0,
        G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMIRROR | G_TX_CLAMP, G_TX_NOMASK, G_TX_NOMASK, G_TX_NOLOD,
        G_TX_NOLOD
    );

    guRotateF(sp20, -camera->curYaw, 0.0f, 1.0f, 0.0f);
    guMtxF2L(sp20, &gDisplayContext->matrixStack[gMatrixListPos]);
    rotationMtx = &gDisplayContext->matrixStack[gMatrixListPos++];

    gSPClearGeometryMode(gMainGfxPos++, G_CULL_BOTH);

    data++;
    for (i = 0; i < count; i++, data++) {
        Vtx* verts = &sRainVerts[i];

        if (data->unk_30 > 0 || data->unk_28 == 0) {
            continue;
        }

        memset(verts, 0, sizeof(*verts));
        verts[0].v.ob[0] = -4; verts[0].v.ob[1] =  38; verts[0].v.ob[2] = 0;
        verts[0].v.tc[0] = 0;  verts[0].v.tc[1] = 0;
        verts[0].v.cn[0] = 255; verts[0].v.cn[1] = 255; verts[0].v.cn[2] = 255; verts[0].v.cn[3] = 255;

        verts[1].v.ob[0] = -4; verts[1].v.ob[1] = -38; verts[1].v.ob[2] = 0;
        verts[1].v.tc[0] = 0;  verts[1].v.tc[1] = 63 * 32;
        verts[1].v.cn[0] = 255; verts[1].v.cn[1] = 255; verts[1].v.cn[2] = 255; verts[1].v.cn[3] = 255;

        verts[2].v.ob[0] =  4; verts[2].v.ob[1] =  38; verts[2].v.ob[2] = 0;
        verts[2].v.tc[0] = 31 * 32; verts[2].v.tc[1] = 0;
        verts[2].v.cn[0] = 255; verts[2].v.cn[1] = 255; verts[2].v.cn[2] = 255; verts[2].v.cn[3] = 255;

        verts[3].v.ob[0] =  4; verts[3].v.ob[1] = -38; verts[3].v.ob[2] = 0;
        verts[3].v.tc[0] = 31 * 32; verts[3].v.tc[1] = 63 * 32;
        verts[3].v.cn[0] = 255; verts[3].v.cn[1] = 255; verts[3].v.cn[2] = 255; verts[3].v.cn[3] = 255;

        guTranslateF(sp20, data->unk_08, data->unk_0C, data->unk_10);
        guMtxF2L(sp20, &gDisplayContext->matrixStack[gMatrixListPos]);
        translationMtx = &gDisplayContext->matrixStack[gMatrixListPos++];

        gSPMatrix(gMainGfxPos++, translationMtx, G_MTX_PUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
        gSPMatrix(gMainGfxPos++, rotationMtx, G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);
        gSPVertex(gMainGfxPos++, verts, 4, 0);
        gSP2Triangles(gMainGfxPos++, 0, 1, 2, 0, 1, 3, 2, 0);
        gSPPopMatrix(gMainGfxPos++, G_MTX_MODELVIEW);
    }

    gSPSetGeometryMode(gMainGfxPos++, G_CULL_BACK);
    gDPPipeSync(gMainGfxPos++);
}

EffectInstance* port_spawn_weather_snowfall(s32 variant, s32 count) {
    return snowfall_main(variant, count);
}

void port_remove_weather_effect(EffectInstance* effect) {
    if (effect != nullptr) {
        remove_effect(effect);
    }
}

void port_configure_weather_effect(EffectInstance* effect) {
    SnowfallFXData* data;

    if (effect == nullptr) {
        return;
    }
    data = effect->data.snowfall;
    if (data != nullptr) {
        data->timeLeft = 100;
        data->unk_00 = 1;
    }
}

void snowfall_appendGfx(void* effect) {
    EffectInstance* effectTemp = effect;
    SnowfallFXData* data = effectTemp->data.snowfall;
    Matrix4f sp18;
    const char* dlist;
    Mtx* mtx;
    u8 bgRGB;
    u8 bgAlpha;
    s32 unk_28;
    s32 unk_2C;
    s32 i;

    if (data->unk_00 != 0) {
        unk_28 = data->unk_28;
        bgAlpha = 255;
        unk_2C = data->unk_2C;
        dlist = D_E008AA50[data->unk_04];
        if (*gBackgroundTintModePtr == ENV_TINT_SHROUD) {
            mdl_get_shroud_tint_params(&bgRGB, &bgRGB, &bgRGB, &bgAlpha);
            unk_28 = 255 - bgAlpha;
        }

        if (data->unk_04 != SNOWFALL_VARIANT_SNOW) {
            snowfall_appendGfxRain(data, (u8) unk_28, unk_2C);
            return;
        }

        if (unk_28 != 0) {
            gDPPipeSync(gMainGfxPos++);
            gSPSegment(gMainGfxPos++, 0x09, VIRTUAL_TO_PHYSICAL(((EffectInstance*)effect)->shared->graphics));

            // D_09000C00 was a 2-command fragment that fell through to D_09000C10 in ROM
            gDPPipeSync(gMainGfxPos++);
            gSPTexture(gMainGfxPos++, 0xFFFF, 0xFFFF, 2, G_TX_RENDERTILE, G_ON);
            gSPDisplayList(gMainGfxPos++, D_09000C10_38DC80);
            gDPSetPrimColor(gMainGfxPos++, 0, 0, 255, 255, 255, unk_28);

            guRotateF(sp18, -gCameras[gCurrentCameraID].curYaw, 0.0f, 1.0f, 0.0f);
            guMtxF2L(sp18, &gDisplayContext->matrixStack[gMatrixListPos]);

            mtx = &gDisplayContext->matrixStack[gMatrixListPos++];

            data++;
            for (i = 0; i < unk_2C; i++, data++) {
                // Give every flake its own interpolation subtree, keyed on (flake, wrapEpoch).
                FrameInterpolation_RecordOpenChild(data, (uintptr_t) data->wrapEpoch);
                if (data->unk_30 <= 0 && data->unk_28 != 0) {
                    guTranslateF(sp18, data->unk_08, data->unk_0C, data->unk_10);
                    guMtxF2L(sp18, &gDisplayContext->matrixStack[gMatrixListPos]);

                    gSPMatrix(gMainGfxPos++, &gDisplayContext->matrixStack[gMatrixListPos++],
                              G_MTX_PUSH | G_MTX_LOAD | G_MTX_MODELVIEW);
                    gSPMatrix(gMainGfxPos++, mtx,
                              G_MTX_NOPUSH | G_MTX_MUL | G_MTX_MODELVIEW);
                    gSPDisplayList(gMainGfxPos++, dlist);
                    gSPPopMatrix(gMainGfxPos++, G_MTX_MODELVIEW);
                }
                FrameInterpolation_RecordCloseChild();
            }
            gDPPipeSync(gMainGfxPos++);
        }
    }
}
