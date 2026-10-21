// Runtime SBN battle-music injection.
//
// The TTYD64 battle tracks are baked into the executable as native-endian
// (little-endian) PM64 BGM sequences (see TtydBgmData.inc.c). At audio-init
// time we patch the
// already-loaded SBN in memory rather than rebuilding the ROM's audio blob:
//
//   1. Copy the four sequences into one contiguous auxiliary buffer.
//   2. Grow the SBN file list with four synthetic AU_FMT_BGM entries whose
//      offsets carry a 0x40000000 tag and index into that aux buffer.
//   3. Overwrite the vanilla battle song-list entries (0x02, 0x03, 0x8B, 0x8E)
//      so they point at the new file indices while keeping their instrument
//      banks.
//
// au_read_rom() and au_fetch_SBN_file() understand the 0x40000000 tag (see
// engine.c) so all reads of the injected sequences resolve into the aux buffer.
// Everything is additive: when the enhancement is off the vanilla SBN is left
// completely untouched and the stock tracks play.

#include "audio/audio.h"
#include "audio/core.h"
#include "port/ui/cvar_prefixes.h"
#include <libultraship/bridge/consolevariablebridge.h>

#include <string.h>
#include <stddef.h>
#include <stdlib.h>

#include "port/Engine.h"

#include "TtydBgmData.inc.c"

// Song IDs remapped to TTYD64 tracks (see enums.h SONG_*).
#define SONG_NORMAL_BATTLE 0x02
#define SONG_SPECIAL_BATTLE 0x03
#define SONG_BATTLE_END 0x8B
#define SONG_LEVEL_UP 0x8E

// Tag bit that marks an aux-buffer offset. Vanilla SBN offsets are far below
// this (the whole blob is ~10.7 MB), so there is no collision.
#define AUX_OFFSET_TAG 0x40000000

// Embedded sequence data (raw .bgm bytes).
typedef struct TtydBgmFile {
    const unsigned char* bytes;
    unsigned int size;
    s32 songId;
} TtydBgmFile;

#define TTYD_FILE_COUNT 4

static const TtydBgmFile kTtydFiles[TTYD_FILE_COUNT] = {
    { gTtydBgm_TTYD_BattleTheme_Updated, gTtydBgm_TTYD_BattleTheme_Updated_size, SONG_NORMAL_BATTLE },
    { gTtydBgm_TTYD_mini_boss_theme, gTtydBgm_TTYD_mini_boss_theme_size, SONG_SPECIAL_BATTLE },
    { gTtydBgm_TTYD_BattleWon, gTtydBgm_TTYD_BattleWon_size, SONG_BATTLE_END },
    { gTtydBgm_TTYD_level_up_theme, gTtydBgm_TTYD_level_up_theme_size, SONG_LEVEL_UP },
};

// Shared with engine.c: aux buffer that synthetic SBN entries read from.
u8* gAudioAuxData = NULL;
s32 gAudioAuxSize = 0;

// Grow the file list so synthetic entries can be appended. The original list
// lives inside gSbnData (owned by the ResourceManager) and is not writable past
// its length, so we copy it into a heap buffer the first time it grows.
static SBNFileEntry* sAugFileList = NULL;
static s32 sAugFileCount = 0;

// Returns the index of a newly appended file entry, or -1 on failure.
static s32 au_append_file_entry(SBNFileEntry* list, s32* count, s32 offset, u32 data) {
    const s32 idx = *count;
    list[idx].offset = offset;
    list[idx].data = data;
    *count = idx + 1;
    return idx;
}

// Idempotent. Safe to call when the enhancement is disabled (no-op).
void PortInjectBattleMusic(void) {
    if (CVarGetInteger(CVAR_ENHANCEMENT("TTYD64BattleMusic"), 0) == 0) {
        GameEngine_LogInfo("[BattleMusic] disabled via CVar");
        return;
    }
    if (gSoundGlobals == NULL || gSoundGlobals->songList == NULL || gSoundGlobals->sbnFileList == NULL) {
        GameEngine_LogInfo("[BattleMusic] gSoundGlobals or lists not ready");
        return;
    }
    if (sAugFileList != NULL) {
        GameEngine_LogInfo("[BattleMusic] already injected");
        return; // already injected
    }

    GameEngine_LogInfo("[BattleMusic] injecting 4 TTYD64 tracks into SBN");

    AuGlobals* globals = gSoundGlobals;

    // Copy the original file list so we can extend it without touching memory
    // we do not own.
    const s32 originalCount = globals->fileListLength;
    SBNFileEntry* grown = (SBNFileEntry*)malloc((originalCount + TTYD_FILE_COUNT) * sizeof(SBNFileEntry));
    if (grown == NULL) {
        return;
    }
    memcpy(grown, globals->sbnFileList, originalCount * sizeof(SBNFileEntry));
    sAugFileList = grown;
    sAugFileCount = originalCount;
    // Point the engine at our grown copy; the original stays in gSbnData.
    globals->sbnFileList = sAugFileList;

    // Build the aux buffer: sequences back-to-back, 16-byte aligned.
    size_t total = 0;
    s32 i;
    for (i = 0; i < TTYD_FILE_COUNT; i++) {
        total += (kTtydFiles[i].size + 0xF) & ~(size_t)0xF;
    }
    u8* aux = (u8*)malloc(total);
    if (aux == NULL) {
        return;
    }
    gAudioAuxData = aux;
    gAudioAuxSize = (s32)total;

    size_t cursor = 0;
    for (i = 0; i < TTYD_FILE_COUNT; i++) {
        memcpy(aux + cursor, kTtydFiles[i].bytes, kTtydFiles[i].size);
        // data byte layout: high byte = AU_FMT_BGM, low 24 bits = size.
        const u32 packed = ((u32)AU_FMT_BGM << 0x18) | ((u32)kTtydFiles[i].size & 0xFFFFFF);
        const s32 fileIdx = au_append_file_entry(sAugFileList, &sAugFileCount, (s32)(AUX_OFFSET_TAG | cursor), packed);
        // Overwrite the vanilla song entry so vanilla IDs now resolve to TTYD.
        // The instrument banks (bkFileIndex) are left untouched so the new
        // sequence plays through the same bank the original battle theme used.
        const s32 songId = kTtydFiles[i].songId;
        if (songId < globals->songListLength) {
            globals->songList[songId].bgmFileIndex = (u16)fileIdx;
            GameEngine_LogInfo("[BattleMusic] remapped song 0x%02X -> fileIdx %d", songId, fileIdx);
        }
        cursor += (kTtydFiles[i].size + 0xF) & ~(size_t)0xF;
    }

    // The grown list is now longer than fileListLength; update it so
    // au_fetch_SBN_file accepts the synthetic indices.
    globals->fileListLength = sAugFileCount;
    GameEngine_LogInfo("[BattleMusic] injection complete, auxSize=%d fileListLength=%d", gAudioAuxSize, globals->fileListLength);
}