// In-memory project (spec 02 §3), pattern names (§5) and recents list (§7).
//
// Most of the project "values block" has only partially known semantics
// (spec 02 §3.3, §12 item 2), so it is kept as the raw 0x700-byte image and
// exposed through typed accessors for the known fields. This keeps load/save
// byte-exact for fields nobody has interpreted yet.
#pragma once

#include <cstdint>
#include <cstring>

#include "model/constants.h"
#include "model/instrument.h"

namespace ot {

// File offsets inside project.mt (spec 02 §3.1-§3.3).
namespace mt {
constexpr uint32_t kFileSize = 0x914;
constexpr uint32_t kSongOff = 0x010;
constexpr uint32_t kSongSize = 0x100;
constexpr uint32_t kValuesOff = 0x110;
constexpr uint32_t kValuesSize = 0x700;
constexpr uint32_t kNameOff = 0x810;
constexpr uint32_t kNameNulOff = 0x830;
constexpr uint32_t kFolderOff = 0x831;
constexpr uint32_t kFolderNulOff = 0x90E;
constexpr uint32_t kCrcOff = 0x910;
constexpr uint32_t kNameMax = kNameNulOff - kNameOff;        // 32 chars
constexpr uint32_t kFolderMax = kFolderNulOff - kFolderOff;  // 221 chars

// Values-block fields (file offsets), spec 02 §3.3.
constexpr uint32_t kEditorNote = 0x110;
constexpr uint32_t kSelectedInstrument = 0x111;
constexpr uint32_t kUnknown113 = 0x113;
constexpr uint32_t kCurrentPattern = 0x114;
constexpr uint32_t kEngineParams115 = 0x115;   // 3 bytes; 0x117 used by pattern v<3 upgrade
constexpr uint32_t kBeatSliceRef = 0x117;
constexpr uint32_t kDelayFeedback = 0x11A;
constexpr uint32_t kDelayTime = 0x11C;
constexpr uint32_t kTrackBool8 = 0x12C;        // u8[8]
constexpr uint32_t kMode134 = 0x134;
constexpr uint32_t kSizeMismatchFlag = 0x140;
constexpr uint32_t kTrackWord8 = 0x142;        // u16[8]
constexpr uint32_t kTrackIndex12 = 0x152;      // u8[12]
constexpr uint32_t kTrackRecord12 = 0x15E;     // u16[12][4]
constexpr uint32_t kTempo = 0x1C0;             // f32 BPM
constexpr uint32_t kDefaultPatternLength = 0x1C4;
constexpr uint32_t kTrackMode12 = 0x2F6;       // u8[12]
constexpr uint32_t kReverbSize = 0x418;
constexpr uint32_t kReverbDamp = 0x41C;
constexpr uint32_t kReverbPreDelay = 0x420;
constexpr uint32_t kReverbDiffusion = 0x424;
constexpr uint32_t kTrackNames1to8 = 0x428;    // char[21] x 8
constexpr uint32_t kTrackNameLen1to8 = 21;
constexpr uint32_t kLevels10 = 0x4D0;          // u8[10]
constexpr uint32_t kMidiRecords = 0x4FD;       // 16 x 16 B
constexpr uint32_t kMidiRecordSize = 16;
constexpr uint32_t kU16_5FE = 0x5FE;
constexpr uint32_t kFlag600 = 0x600;
constexpr uint32_t kSel601 = 0x601;
constexpr uint32_t kFlag602 = 0x602;
constexpr uint32_t kTrackNames9to16 = 0x603;   // char[8] x 8
constexpr uint32_t kTrackNameLen9to16 = 8;
constexpr uint32_t kTrackFlags8 = 0x742;       // u8[8]
constexpr uint32_t kPercent80C = 0x80C;
}  // namespace mt

struct Project {
    // Song block (spec 02 §3.2).
    uint8_t playlist[kSongSlots] = {};  // pattern number per slot, 0 = end of song
    uint8_t playlistPos = 0;

    // Raw values block (file 0x110..0x80F).
    uint8_t values[mt::kValuesSize] = {};

    char name[mt::kNameMax + 1] = {};
    char folder[mt::kFolderMax + 1] = {};

    Project() { setDefault(); }

    // New Project values (spec 07 Q6 table; FW:0x234DC).
    void setDefault();
    // Load pre-fill (spec 02 §3.3 "Default" column, FW:0x73404 / 0x260A4):
    // only survives in a truncated project.mt (threshold 0x4000, levels 100).
    void setLoadPrefill();

    // --- raw access by *file* offset (0x110..0x80F) -----------------------
    uint8_t u8(uint32_t off) const { return values[off - mt::kValuesOff]; }
    void setU8(uint32_t off, uint8_t v) { values[off - mt::kValuesOff] = v; }
    uint16_t u16(uint32_t off) const;
    void setU16(uint32_t off, uint16_t v);
    uint32_t u32(uint32_t off) const;
    void setU32(uint32_t off, uint32_t v);
    float f32(uint32_t off) const;
    void setF32(uint32_t off, float v);

    // --- typed accessors ----------------------------------------------------
    float tempo() const { return f32(mt::kTempo); }
    void setTempo(float bpm) { setF32(mt::kTempo, bpm); }
    uint8_t currentPattern() const { return u8(mt::kCurrentPattern); }
    void setCurrentPattern(uint8_t p) { setU8(mt::kCurrentPattern, p); }
    uint16_t defaultPatternLength() const { return u16(mt::kDefaultPatternLength); }
    uint8_t beatSliceReference() const { return u8(mt::kBeatSliceRef); }

    // Track names: tracks 0..7 (21-byte slots), 8..15 (8-byte slots). Copies
    // into dst (NUL-terminated, cap >= 1). Returns false for bad index.
    bool trackName(int track, char* dst, uint32_t cap) const;
    bool setTrackName(int track, const char* s);

    MidiInstrument midiInstrument(int idx) const;      // idx 0..15
    void setMidiInstrument(int idx, const MidiInstrument& m);

    void setName(const char* s);
    void setFolder(const char* s);
};

// Pattern names, patternsMetadata (spec 02 §5). Index = pattern number,
// entry 0 unused.
struct PatternNames {
    static constexpr int kCount = 256;
    static constexpr int kMaxLen = 30;
    char names[kCount][kMaxLen + 1] = {};

    void clear() { std::memset(names, 0, sizeof(names)); }
    void set(int pattern, const char* s);
    const char* get(int pattern) const { return (pattern >= 0 && pattern < kCount) ? names[pattern] : ""; }
};

// Projects/recents (spec 02 §7): 10 slots of NUL-terminated folder paths.
struct Recents {
    static constexpr int kSlots = 10;
    static constexpr int kSlotSize = 500;
    char paths[kSlots][kSlotSize] = {};

    void clear() { std::memset(paths, 0, sizeof(paths)); }
    // Display name: last path component, truncated to 32 chars (spec 02 §7).
    static void displayName(const char* path, char* dst, uint32_t cap);
    // Most-recent-first insert with de-duplication. NOTE: this policy is an
    // OpenTracker choice; spec 02 §12 item 6 leaves it unknown.
    void touch(const char* path);
};

}  // namespace ot
