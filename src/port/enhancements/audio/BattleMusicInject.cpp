#include "BattleMusicInject.h"

extern "C" {
#include <libultra/types.h>
#include <libultra/audio.h>
}

#include <libultraship/libultra/types.h>
#include <libultraship/libultra/audio.h>
#include <libultraship/libultra/debug.h>

#include "port/Engine.h"
#include <libultraship/bridge/consolevariablebridge.h>
#include "port/ui/cvar_prefixes.h"

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

extern u8 gTtydBgm_TTYD_BattleTheme_Updated[];
extern size_t gTtydBgm_TTYD_BattleTheme_Updated_size;
extern u8 gTtydBgm_TTYD_BattleWon[];
extern size_t gTtydBgm_TTYD_BattleWon_size;
extern u8 gTtydBgm_TTYD_level_up_theme[];
extern size_t gTtydBgm_TTYD_level_up_theme_size;

#define SONG_NORMAL_BATTLE 0x02
#define SONG_BATTLE_END    0x8B
#define SONG_LEVEL_UP      0x8E

#define TTYD_FILE_COUNT 3

typedef struct TtydBgmFile {
    const unsigned char* bytes;
    unsigned int size;
    s32 songId;
} TtydBgmFile;

static const TtydBgmFile kTtydFiles[TTYD_FILE_COUNT] = {
    { gTtydBgm_TTYD_BattleTheme_Updated, gTtydBgm_TTYD_BattleTheme_Updated_size, SONG_NORMAL_BATTLE },
    { gTtydBgm_TTYD_BattleWon, gTtydBgm_TTYD_BattleWon_size, SONG_BATTLE_END },
    { gTtydBgm_TTYD_level_up_theme, gTtydBgm_TTYD_level_up_theme_size, SONG_LEVEL_UP },
};

static bool sBattleMusicInjected = false;

void PortInjectBattleMusic(void) {
    if (sBattleMusicInjected) {
        return;
    }

    AuGlobals* globals = gSoundGlobals;
    if (!globals) {
        return;
    }

    SPDLOG_INFO("[BattleMusic] Injecting %d TTYD64 tracks into SBN", TTYD_FILE_COUNT);

    const s32 originalCount = globals->fileListLength;
    SBNFileEntry* grown = (SBNFileEntry*)malloc((originalCount + TTYD_FILE_COUNT) * sizeof(SBNFileEntry));
    if (!grown) {
        return;
    }
    memcpy(grown, globals->sbnFileList, originalCount * sizeof(SBNFileEntry));

    size_t total = 0;
    for (s32 i = 0; i < TTYD_FILE_COUNT; i++) {
        total += (kTtydFiles[i].size + 0xF) & ~(size_t)0xF;
    }
    u8* aux = (u8*)malloc(total);
    if (!aux) {
        free(grown);
        return;
    }

    size_t cursor = 0;
    for (s32 i = 0; i < TTYD_FILE_COUNT; i++) {
        memcpy(aux + cursor, kTtydFiles[i].bytes, kTtydFiles[i].size);
        cursor += (kTtydFiles[i].size + 0xF) & ~(size_t)0xF;
    }

    globals->sbnFileList = grown;
    s32 sAugFileCount = originalCount;
    u32 gAudioAuxSize = total;

    for (s32 i = 0; i < TTYD_FILE_COUNT; i++) {
        SBNFileEntry* entry = &grown[sAugFileCount];
        entry->offset = 0x40000000 | cursor;
        entry->data = aux + cursor;
        entry->size = kTtydFiles[i].size;
        entry->flags = 0;
        entry->mediaType = AU_FMT_BGM;
        sAugFileCount++;
        cursor += (kTtydFiles[i].size + 0xF) & ~(size_t)0xF;
    }

    for (s32 i = 0; i < TTYD_FILE_COUNT; i++) {
        const s32 songId = kTtydFiles[i].songId;
        if (songId < globals->songListLength) {
            globals->songList[songId].bgmFileIndex = (u16)(originalCount + i);
            SPDLOG_INFO("[BattleMusic] Remapped song 0x%02X -> fileIdx %d", songId, originalCount + i);
        }
    }

    globals->fileListLength = sAugFileCount;
    SPDLOG_INFO("[BattleMusic] Injection complete, auxSize=%u fileListLength=%d", gAudioAuxSize, globals->fileListLength);
    sBattleMusicInjected = true;
}