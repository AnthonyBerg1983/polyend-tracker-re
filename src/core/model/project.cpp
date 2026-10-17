// Project model defaults and accessors (spec 02 §3.2, §3.3, §5, §7).
#include "model/project.h"

#include <cstdio>

#include "formats/bytes.h"

namespace ot {

uint16_t Project::u16(uint32_t off) const { return rdU16(&values[off - mt::kValuesOff]); }
void Project::setU16(uint32_t off, uint16_t v) { wrU16(&values[off - mt::kValuesOff], v); }
uint32_t Project::u32(uint32_t off) const { return rdU32(&values[off - mt::kValuesOff]); }
void Project::setU32(uint32_t off, uint32_t v) { wrU32(&values[off - mt::kValuesOff], v); }
float Project::f32(uint32_t off) const { return rdF32(&values[off - mt::kValuesOff]); }
void Project::setF32(uint32_t off, float v) { wrF32(&values[off - mt::kValuesOff], v); }

void Project::setDefault() {
    // New Project (spec 07 Q2, Q3, Q6; FW:0x234DC): the load pre-fill, then
    // the values the firmware's New Project writes.
    setLoadPrefill();
    setU16(0x120, 100);          // limiter attack, ms
    setF32(0x124, 0.512f);       // limiter release, ms (below the UI minimum; kept)
    setU16(0x128, 30200);        // limiter threshold -> LIMTHR[92] = -6.0 dBFS
    setU8(0x12A, 0x10);          // not limiter (unknown)
    setU8(0x12B, 0x01);
    for (int t = 0; t < 8; ++t) setU8(0x12C + uint32_t(t), 0);   // track mute flags
    setU8(0x309, 86);            // line-in level 0 dB
    for (int i = 0; i < 8; ++i) setU8(mt::kLevels10 + uint32_t(i), 86);   // track levels 0 dB
    setU8(0x4D8, 86);            // Global Mixer Reverb
    setU8(0x4D9, 86);            // Global Mixer Delay
    setU8(0x4DA, 0);             // reverb / delay / dry mute
    setU8(0x4DB, 0);
    setU8(0x4DC, 0);
    setU8(0x4DD, 86);            // Dry Mix
    setU8(0x4FA, 0);             // limiter mode
    setU8(0x4FB, 0);             // side-chain off
    setU8(0x4FC, 1);             // Sends mode = post-fader
    setU16(mt::kU16_5FE, 20000); // delay filter, Hz
}

void Project::setLoadPrefill() {
    // spec 02 §3.2: playlist slot0 = 1, rest 0; position 0.
    std::memset(playlist, 0, sizeof(playlist));
    playlist[0] = 1;
    playlistPos = 0;

    // spec 02 §3.3 "Default" column; unlisted bytes 0.
    std::memset(values, 0, sizeof(values));
    setU8(mt::kCurrentPattern, 1);
    setU8(0x115, 0);
    setU8(0x116, 11);
    setU8(0x117, 24);
    setU8(0x118, 8);
    // Fields with no firmware default but a tracker-lib template value
    // ("tl-tpl" in spec 02 §3.3) take the template value.
    setU8(mt::kDelayFeedback, 50);
    setU16(mt::kDelayTime, 500);
    setU16(0x120, 100);
    setF32(0x124, 0.512f);
    setU32(0x128, 0x01104000);
    setF32(0x13C, 87.5f);
    for (int i = 0; i < 8; ++i) setU16(mt::kTrackWord8 + 2 * i, 1);
    for (int i = 0; i < 12; ++i) setU8(mt::kTrackIndex12 + i, uint8_t(i + 1));
    setTempo(130.0f);
    setU16(mt::kDefaultPatternLength, 32);
    setU16(0x30A, 1);
    setU16(0x30C, 1);
    setU16(0x30E, 1);
    setF32(mt::kReverbSize, 0.5f);
    setF32(mt::kReverbDamp, 0.5f);
    setF32(mt::kReverbPreDelay, 0.5f);
    setF32(mt::kReverbDiffusion, 0.68f);
    char buf[24];
    for (int i = 0; i < 8; ++i) {
        std::snprintf(buf, sizeof(buf), "Track %d", i + 1);
        setTrackName(i, buf);
    }
    for (int i = 0; i < 10; ++i) setU8(mt::kLevels10 + i, 100);
    setU8(0x4DD, 100);
    setU32(0x4F8, 0x00000103);
    setU8(0x4FC, 1);
    for (int i = 0; i < kMidiInstrumentCount; ++i) {
        MidiInstrument m;
        m.channel = uint8_t(i);  // (I) matches the tracker-lib template
        setMidiInstrument(i, m);
    }
    setU16(mt::kU16_5FE, 20000);
    for (int i = 0; i < 8; ++i) {
        std::snprintf(buf, sizeof(buf), "Midi %d", i + 9);
        setTrackName(8 + i, buf);
    }

    setName("New Project");
    setFolder("");
}

bool Project::trackName(int track, char* dst, uint32_t cap) const {
    if (track < 0 || track >= 16) return false;
    if (track < 8)
        rdStr(&values[mt::kTrackNames1to8 + track * mt::kTrackNameLen1to8 - mt::kValuesOff],
              mt::kTrackNameLen1to8, dst, cap);
    else
        rdStr(&values[mt::kTrackNames9to16 + (track - 8) * mt::kTrackNameLen9to16 - mt::kValuesOff],
              mt::kTrackNameLen9to16, dst, cap);
    return true;
}

bool Project::setTrackName(int track, const char* s) {
    if (track < 0 || track >= 16) return false;
    if (track < 8)
        wrStr(&values[mt::kTrackNames1to8 + track * mt::kTrackNameLen1to8 - mt::kValuesOff],
              mt::kTrackNameLen1to8, s);
    else
        wrStr(&values[mt::kTrackNames9to16 + (track - 8) * mt::kTrackNameLen9to16 - mt::kValuesOff],
              mt::kTrackNameLen9to16, s);
    return true;
}

MidiInstrument Project::midiInstrument(int idx) const {
    MidiInstrument m;
    if (idx < 0 || idx >= kMidiInstrumentCount) return m;
    const uint8_t* p = &values[mt::kMidiRecords + idx * mt::kMidiRecordSize - mt::kValuesOff];
    m.channel = p[0];
    m.velocity = p[1];
    std::memcpy(m.cc, p + 2, 6);
    std::memcpy(m.extra, p + 8, 8);
    return m;
}

void Project::setMidiInstrument(int idx, const MidiInstrument& m) {
    if (idx < 0 || idx >= kMidiInstrumentCount) return;
    uint8_t* p = &values[mt::kMidiRecords + idx * mt::kMidiRecordSize - mt::kValuesOff];
    p[0] = m.channel;
    p[1] = m.velocity;
    std::memcpy(p + 2, m.cc, 6);
    std::memcpy(p + 8, m.extra, 8);
}

void Project::setName(const char* s) { copyStr(name, sizeof(name), s); }
void Project::setFolder(const char* s) { copyStr(folder, sizeof(folder), s); }

void PatternNames::set(int pattern, const char* s) {
    if (pattern < 0 || pattern >= kCount) return;
    copyStr(names[pattern], sizeof(names[pattern]), s);
}

void Recents::displayName(const char* path, char* dst, uint32_t cap) {
    const char* last = path;
    for (const char* p = path; *p; ++p)
        if (*p == '/' && p[1]) last = p + 1;
    char tmp[33];
    uint32_t n = 0;
    while (last[n] && last[n] != '/' && n < 32) {
        tmp[n] = last[n];
        ++n;
    }
    tmp[n] = 0;
    copyStr(dst, cap, tmp);
}

void Recents::touch(const char* path) {
    if (!path || !*path) return;
    int found = kSlots - 1;
    for (int i = 0; i < kSlots; ++i) {
        if (std::strncmp(paths[i], path, kSlotSize) == 0) {
            found = i;
            break;
        }
    }
    for (int i = found; i > 0; --i) std::memcpy(paths[i], paths[i - 1], kSlotSize);
    std::memset(paths[0], 0, kSlotSize);
    copyStr(paths[0], kSlotSize, path);
}

}  // namespace ot
