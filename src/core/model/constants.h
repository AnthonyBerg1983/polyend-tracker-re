// Data-model constants (spec 02 §4.3, §8.2).
#pragma once

#include <cstdint>

namespace ot {

constexpr int kTrackCount = 12;            // tracks per pattern (older files: 8), spec 02 §4.3
constexpr int kLegacyTrackCount = 8;       // pre-v5 / 6184-byte patterns, spec 02 §4.4
constexpr int kStepCount = 128;            // max steps per track, spec 02 §4.3
constexpr int kFxPerStep = 2;              // FX columns per step, spec 02 §4.3
constexpr int kDefaultTrackLastStep = 31;  // 32 steps, spec 02 §4.2

constexpr int kPatternCount = 255;         // pattern numbers 1..255, 0 = empty slot, spec 02 §4.3
constexpr int kSongSlots = 255;            // playlist length, spec 02 §3.2

constexpr int kSampleInstrumentCount = 48; // instruments 0..47 (files 01..48), spec 02 §4.3
constexpr int kMidiInstrumentCount = 16;   // instruments 48..63, spec 02 §4.3
constexpr int kInstrumentCount = kSampleInstrumentCount + kMidiInstrumentCount;
constexpr int kFirstMidiInstrument = kSampleInstrumentCount;

// Step note sentinels (spec 02 §4.2). 0..127 are note numbers.
constexpr int8_t kNoteEmpty = -1;
constexpr int8_t kNoteFade = -2;
constexpr int8_t kNoteCut = -3;
constexpr int8_t kNoteOff = -4;
constexpr int8_t kNoteMax = 127;

inline bool isSampleInstrument(uint8_t i) { return i < kSampleInstrumentCount; }
inline bool isMidiInstrument(uint8_t i) { return i >= kSampleInstrumentCount && i < kInstrumentCount; }

// Sample bank (spec 02 §8.2).
constexpr uint32_t kSampleBankBytes = 0xB40000;   // 11 796 480 bytes
constexpr uint32_t kSampleRate = 44100;

}  // namespace ot
