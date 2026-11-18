#include "port/Engine.h"
#include "port/romhack/Romhack.h"

#include <libultraship.h>
#include <ship/resource/File.h>
#include <ship/resource/ResourceManager.h>
#include <spdlog/spdlog.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <set>
#include <utility>
#include <vector>

namespace {

constexpr uint32_t kBgmSignature = 0x42474D20u; // 'BGM '
constexpr size_t kBgmHeaderSize = 0x24;
constexpr size_t kDrumInfoSize = 0x0C;
constexpr size_t kInstrumentInfoSize = 0x08;
constexpr size_t kPhraseTrackCount = 16;
constexpr size_t kPhraseTrackSize = sizeof(uint32_t);

std::atomic<bool> sChapterBattleMusicRequested { false };

uint32_t ReadU32BE(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

uint16_t ReadU16BE(const uint8_t* p) {
    return static_cast<uint16_t>((static_cast<uint16_t>(p[0]) << 8) | static_cast<uint16_t>(p[1]));
}

void Swap32(uint8_t* p) {
    std::swap(p[0], p[3]);
    std::swap(p[1], p[2]);
}

void Swap16(uint8_t* p) {
    std::swap(p[0], p[1]);
}

bool InRange(uint64_t offset, uint64_t size, size_t total) {
    return offset <= total && size <= static_cast<uint64_t>(total) - offset;
}

// Values are the verified enum Areas indices from include/enums.h. `hos` is
// checked first in the switch so Star Haven wins before Chapter 8's `kpa`.
const char* BgmPathForArea(int32_t areaID) {
    switch (areaID) {
        case 5: // AREA_HOS - Shooting Star Summit / Star Haven
            return "audio/PM64_StarHaven_Battle.bgm";
        case 8: // AREA_IWA
        case 9: // AREA_DRO
        case 10: // AREA_SBK
        case 11: // AREA_ISK
            return "audio/PM64_Ch2_Battle.bgm";
        case 12: // AREA_MIM
        case 13: // AREA_OBK
        case 14: // AREA_ARN
        case 15: // AREA_DGB
            return "audio/PM64_Ch3_Battle.bgm";
        case 16: // AREA_OMO
            return "audio/PM64_Ch4_Battle.bgm";
        case 17: // AREA_JAN
        case 18: // AREA_KZN
            return "audio/PM64_Ch5_Battle.bgm";
        case 19: // AREA_FLO
            return "audio/PM64_Ch6_Battle.bgm";
        case 20: // AREA_SAM
        case 21: // AREA_PRA
            return "audio/PM64_Ch7_Battle.bgm";
        case 22: // AREA_KPA
            return "audio/PM64_Ch8_Battle.bgm";
        default:
            // kmr, mac, tik, kkj, nok, trd, osr, mgm, tst, area_pit, etc.
            return nullptr;
    }
}

// Loose .bgm files are big-endian N64 data. Mirror the BGM half of Torch's
// PM64:AUDIO factory so the bytes in dataBGM are native-endian before the
// normal BGM player touches them.
bool ConvertBgmToNative(uint8_t* data, size_t dataSize, uint32_t* outName) {
    if (data == nullptr || dataSize < kBgmHeaderSize) {
        return false;
    }
    if (ReadU32BE(data) != kBgmSignature) {
        return false;
    }

    uint32_t declaredSize = ReadU32BE(data + 4);
    if (declaredSize < kBgmHeaderSize || declaredSize > dataSize) {
        return false;
    }
    dataSize = declaredSize;

    uint16_t compositionOffsets[4];
    for (size_t i = 0; i < 4; i++) {
        compositionOffsets[i] = ReadU16BE(data + 0x14 + i * 2);
    }
    uint16_t drumsOffset = ReadU16BE(data + 0x1C);
    uint16_t drumCount = ReadU16BE(data + 0x1E);
    uint16_t instrumentsOffset = ReadU16BE(data + 0x20);
    uint16_t instrumentCount = ReadU16BE(data + 0x22);

    Swap32(data + 0x00); // signature
    Swap32(data + 0x04); // size
    Swap32(data + 0x08); // name
    for (size_t off = 0x14; off < 0x24; off += 2) {
        Swap16(data + off); // BGMFileInfo u16 fields
    }

    uint32_t name = 0;
    std::memcpy(&name, data + 0x08, sizeof(name));
    // bgm_control.c treats <= 0xFFFF as an AuResult error code. Packed 4CC
    // names may have the high bit set (e.g. "New " == 0x4E657720), so this
    // must stay an unsigned comparison, matching bgm_control.c's 0xFFFFU.
    if (name <= 0xFFFFu) {
        return false;
    }

    std::set<uint64_t> swappedPhrases;
    for (uint16_t compositionOffset : compositionOffsets) {
        if (compositionOffset == 0) {
            continue;
        }

        uint64_t compAbs = static_cast<uint64_t>(compositionOffset) * 4;
        if (!InRange(compAbs, sizeof(uint32_t), dataSize)) {
            return false;
        }

        while (compAbs + sizeof(uint32_t) <= dataSize) {
            uint32_t rawCommand = 0;
            std::memcpy(&rawCommand, data + compAbs, sizeof(rawCommand));
            if (rawCommand == 0) { // BGM_COMP_END
                break;
            }

            Swap32(data + compAbs);

            uint32_t command = 0;
            std::memcpy(&command, data + compAbs, sizeof(command));
            if (((command >> 28) & 0xF) == 1) { // BGM_COMP_PLAY_PHRASE
                uint64_t phraseAbs = compAbs + static_cast<uint64_t>(command & 0xFFFFu) * 4;
                if (!InRange(phraseAbs, kPhraseTrackCount * kPhraseTrackSize, dataSize)) {
                    return false;
                }
                if (swappedPhrases.insert(phraseAbs).second) {
                    for (size_t track = 0; track < kPhraseTrackCount; track++) {
                        Swap32(data + phraseAbs + track * sizeof(uint32_t));
                    }
                }
            }
            compAbs += sizeof(uint32_t);
        }
    }

    if (drumsOffset != 0 && drumCount != 0) {
        uint64_t drumsAbs = static_cast<uint64_t>(drumsOffset) * 4;
        if (!InRange(drumsAbs, static_cast<uint64_t>(drumCount) * kDrumInfoSize, dataSize)) {
            return false;
        }
        for (uint16_t i = 0; i < drumCount; i++) {
            uint8_t* entry = data + drumsAbs + static_cast<uint64_t>(i) * kDrumInfoSize;
            Swap16(entry + 0x0); // bankPatch
            Swap16(entry + 0x2); // keyBase
        }
    }

    if (instrumentsOffset != 0 && instrumentCount != 0) {
        uint64_t instrumentsAbs = static_cast<uint64_t>(instrumentsOffset) * 4;
        if (!InRange(instrumentsAbs, static_cast<uint64_t>(instrumentCount) * kInstrumentInfoSize, dataSize)) {
            return false;
        }
        for (uint16_t i = 0; i < instrumentCount; i++) {
            uint8_t* entry = data + instrumentsAbs + static_cast<uint64_t>(i) * kInstrumentInfoSize;
            Swap16(entry + 0x0); // bankPatch
        }
    }

    *outName = name;
    return true;
}

} // namespace

extern "C" void Port_MarkChapterBattleMusicRequest(void) {
    sChapterBattleMusicRequested.store(true, std::memory_order_release);
}

extern "C" void Port_ClearChapterBattleMusicRequest(void) {
    sChapterBattleMusicRequested.store(false, std::memory_order_relaxed);
}

extern "C" int32_t Port_TakeChapterBattleMusicRequest(void) {
    return sChapterBattleMusicRequested.exchange(false, std::memory_order_acquire) ? 1 : 0;
}

extern "C" int32_t Port_LoadChapterBattleMusicForArea(int32_t areaID, void* dst, size_t dstSize) {
    if (dst == nullptr || dstSize == 0) {
        return 0;
    }
    const char* romhackId = Romhack_GetId();
    if (romhackId == nullptr || std::strcmp(romhackId, "TTYD64") != 0) {
        return 0;
    }
    if (!Port_FeatureInt("enhancements.audio.chapter_battle_music", 0)) {
        return 0;
    }

    const char* path = BgmPathForArea(areaID);
    if (path == nullptr) {
        return 0;
    }

    auto context = Ship::Context::GetRawInstance();
    if (context == nullptr) {
        return 0;
    }
    auto resourceManager = context->GetResourceManager();
    if (resourceManager == nullptr) {
        return 0;
    }
    auto archiveManager = resourceManager->GetArchiveManager();
    if (archiveManager == nullptr || !archiveManager->HasFile(path)) {
        SPDLOG_DEBUG("Chapter battle music: {} not found in mounted archives", path);
        return 0;
    }

    auto file = archiveManager->LoadFile(path);
    if (file == nullptr || file->Buffer == nullptr || file->Buffer->size() <= file->BufferOffset) {
        SPDLOG_DEBUG("Chapter battle music: {} could not be read", path);
        return 0;
    }

    const uint8_t* raw = reinterpret_cast<const uint8_t*>(file->Buffer->data()) + file->BufferOffset;
    size_t rawSize = file->Buffer->size() - file->BufferOffset;
    if (rawSize < kBgmHeaderSize) {
        SPDLOG_DEBUG("Chapter battle music: {} is too small", path);
        return 0;
    }

    uint32_t declaredSize = ReadU32BE(raw + 4);
    if (declaredSize < kBgmHeaderSize || declaredSize > rawSize || declaredSize > dstSize) {
        SPDLOG_DEBUG("Chapter battle music: {} has invalid size {} (file {}, buffer {})", path, declaredSize,
                     rawSize, dstSize);
        return 0;
    }

    // Never point the audio engine at the LUS-owned File buffer; convert a
    // private copy and copy that into the preallocated dataBGM slot.
    std::vector<uint8_t> bgm(raw, raw + declaredSize);
    uint32_t name = 0;
    if (!ConvertBgmToNative(bgm.data(), bgm.size(), &name)) {
        SPDLOG_DEBUG("Chapter battle music: invalid BGM in {}", path);
        return 0;
    }

    std::memcpy(dst, bgm.data(), bgm.size());
    SPDLOG_INFO("Chapter battle music: loaded {} (name 0x{:08X})", path, name);
    return static_cast<int32_t>(name);
}
