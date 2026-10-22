with open('src/port/enhancements/audio/BattleMusicInject.c', 'r') as f:
    content = f.read()

# Find and replace the PortInjectBattleMusic function
old_code = ''');

    bool safeMode = CVarGetInteger(CVAR_ENHANCEMENT("TTYD64BattleMusicSafeMode"), 1) != 0;
    GameEngine_LogInfo("[BattleMusic] injecting TTYD64 tracks into SBN (safeMode=%d)", safeMode);

    AuGlobals* globals = gSoundGlobals;

    // Count how many tracks we'll inject (respecting safe mode)
    s32 injectCount = 0;
    for (s32 i = 0; i < TTYD_FILE_COUNT; i++) {
        if (!safeMode || kTtydFiles[i].safe) {
            injectCount++;
        }
    }
    if (injectCount == 0) {
        GameEngine_LogInfo("[BattleMusic] no safe tracks to inject");
        return;
    }

    // Copy the original file list so we can extend it without touching memory
    // we do not own.
    const s32 originalCount = globals->fileListLength;
    SBNFileEntry* grown = (SBNFileEntry*)malloc((originalCount + injectCount) * sizeof(SBNFileEntry));
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
        if (safeMode && !kTtydFiles[i].safe) continue;
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
        if (safeMode && !kTtydFiles[i].safe) {
            GameEngine_LogInfo("[BattleMusic] skipping unsafe track for song 0x%02X", kTtydFiles[i].songId);
            continue;
        }
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
            GameEngine_LogInfo("[BattleMusic] remapped song 0x%02X -> fileIdx %d (safe=%d)", songId, fileIdx, kTtydFiles[i].safe);
        }
        cursor += (kTtydFiles[i].size + 0xF) & ~(size_t)0xF;
    }

    // The grown list is now longer than fileListLength; update it so
    // au_fetch_SBN_file accepts the synthetic indices.
    globals->fileListLength = sAugFileCount;
    GameEngine_LogInfo("[BattleMusic] injection complete, auxSize=%d fileListLength=%d injected=%d", gAudioAuxSize, globals->fileListLength, injectCount);
}'''

new_code = ''');

    GameEngine_LogInfo("[BattleMusic] injecting %d TTYD64 tracks into SBN", TTYD_FILE_COUNT);

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
}'''

if old_code in content:
    content = content.replace(old_code, new_code)
    with open('src/port/enhancements/audio/BattleMusicInject.c', 'w') as f:
        f.write(content)
    print('Updated BattleMusicInject.c')
else:
    print('Pattern not found, trying alternative...')
    # Try to find the function differently
    idx = content.find('void PortInjectBattleMusic(void)')
    if idx >= 0:
        print(f'Found function at {idx}')
        # Find the end of the function
        brace_count = 0
        for i in range(idx, len(content)):
            if content[i] == '{':
                brace_count += 1
            elif content[i] == '}':
                brace_count -= 1
                if brace_count == 0:
                    print(f'Function ends at {i}')
                    print(content[idx:i+1])
                    break

with open('src/port/enhancements/audio/BattleMusicInject.c', 'w') as f:
    f.write(content)
print('Updated BattleMusicInject.c')