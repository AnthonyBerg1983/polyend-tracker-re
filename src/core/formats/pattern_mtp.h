// pattern_NN.mtp codec (spec 02 §4), fixed 0x242C bytes, 12 tracks.
#pragma once

#include <cstddef>
#include <cstdint>

#include "formats/format_common.h"
#include "model/pattern.h"

namespace ot {

namespace mtp {
constexpr uint32_t kFileSize = 0x242C;      // 9260
constexpr uint32_t kLegacyFileSize = 6184;  // 8-track variant (0x1C + 769*8 + 4)
constexpr uint32_t kReservedOff = 0x10;     // 12 reserved bytes, written 0
constexpr uint32_t kTracksOff = 0x1C;
constexpr uint32_t kTrackSize = 0x301;      // 769
constexpr uint32_t kStepSize = 6;
constexpr uint32_t kCrcOff = 0x2428;
constexpr uint8_t kStructVersion = 5;
constexpr uint8_t kFwVersion[4] = {1, 9, 2, 1};
}  // namespace mtp

struct PatternLoadInfo {
    uint8_t structVersion = 0;
    uint16_t sizeField = 0;
    bool upgraded = false;         // structure < 5
    bool upperTracksReset = false; // tracks 8..11 were reset (v<5 or size != 0x242C)
    uint32_t storedCrc = 0;        // informational only (spec 02 §2)
    uint32_t computedCrc = 0;
};

// Inputs for the pre-v5 upgrade chain (spec 10 §2.2).
struct PatternUpgradeContext {
    // Play mode per sample instrument (0..47); nullptr skips the v<3 step.
    const uint8_t* instrumentPlayModes = nullptr;
    // Project byte 0x117 (spec 02 §3.3); v<3 transposes by (12 - value).
    uint8_t beatSliceReference = 24;
    // FX 18 (Volume) value curves for v<2 and v<4. nullptr = the spec 10
    // §0.4 curves G2 / G4 (overrides are for tests).
    uint8_t (*volumeCurveV2)(uint8_t) = nullptr;
    uint8_t (*volumeCurveV4)(uint8_t) = nullptr;
};

// Save per spec 02 §4.4 "Save": header KS / 2 / 1.9.2.1 / 5.5.5.5 / 0x242C, CRC.
void encodePattern(const Pattern& p, uint8_t out[mtp::kFileSize]);

// Load per spec 02 §4.4 "Load". Never fails: ids/type/CRC are not checked,
// missing bytes read as zero.
void decodePattern(const uint8_t* file, size_t len, Pattern& p, const PatternUpgradeContext* ctx = nullptr,
                   PatternLoadInfo* info = nullptr);

}  // namespace ot
