import struct

def byteswap_bgm(data):
    """Replicates Torch PM64AudioFactory ByteSwapAudioData for a standalone big-endian
    BGM file. The C code reinterprets the raw buffer as native (little-endian) u32/u16
    and applies BSWAP, which on the byte level is simply reversing each word's bytes.
    `data` is a bytearray; mutated in place and returned."""
    size = len(data)
    BGM_DRUM_INFO_SIZE = 0x0C
    BGM_INSTRUMENT_INFO_SIZE = 0x08

    def rev32(o):
        data[o:o+4] = data[o:o+4][::-1]
    def rev16(o):
        data[o:o+2] = data[o:o+2][::-1]
    def rd32(o):
        return struct.unpack_from("<I", data, o)[0]
    def rd16(o):
        return struct.unpack_from("<H", data, o)[0]

    fileOffset = 0

    # --- BGM header: sig, size, name (u32), pad at 0x0C ---
    rev32(0x00)
    rev32(0x04)
    rev32(0x08)

    # --- BGMFileInfo u16s at 0x14: compositions[4], drums, drumCount, instruments, instrumentCount ---
    for i in range(8):
        rev16(0x14 + i*2)

    # read back (now little-endian values)
    comp = [rd16(0x14 + i*2) for i in range(4)]
    drums = rd16(0x14 + 4*2)
    drumCount = rd16(0x14 + 5*2)
    instr = rd16(0x14 + 6*2)
    instrCount = rd16(0x14 + 7*2)

    # --- composition command arrays (u32 words) ---
    swappedPhrases = set()
    for c in range(4):
        compOff = comp[c]
        if compOff == 0:
            continue
        compAbs = fileOffset + compOff * 4
        while compAbs + 4 <= size:
            rawBE = struct.unpack_from(">I", data, compAbs)[0]
            if rawBE == 0:
                break  # BGM_COMP_END is 0 in both endiannesses
            rev32(compAbs)
            # after rev32 the value in memory is LE-swapped; interpret as the C code sees it
            sw = rd32(compAbs)
            if ((sw >> 28) & 0xF) == 1:
                phraseRel = sw & 0xFFFF
                phraseAbs = fileOffset + compOff * 4 + phraseRel * 4
                if phraseAbs not in swappedPhrases and phraseAbs + 16*4 <= size:
                    swappedPhrases.add(phraseAbs)
                    for t in range(16):
                        rev32(phraseAbs + t*4)
            compAbs += 4

    # --- BGMDrumInfo entries (u16 at +0, u16 at +2) ---
    if drums != 0 and drumCount > 0:
        drumsAbs = fileOffset + drums * 4
        for d in range(drumCount):
            eo = drumsAbs + d * BGM_DRUM_INFO_SIZE
            if eo + BGM_DRUM_INFO_SIZE <= size:
                rev16(eo)
                rev16(eo+2)

    # --- BGMInstrumentInfo entries (u16 at +0) ---
    if instr != 0 and instrCount > 0:
        instrAbs = fileOffset + instr * 4
        for ins in range(instrCount):
            eo = instrAbs + ins * BGM_INSTRUMENT_INFO_SIZE
            if eo + BGM_INSTRUMENT_INFO_SIZE <= size:
                rev16(eo)

    return data
