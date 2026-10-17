// pattern_NN.mtp codec, spec 02 §4.
#include "formats/pattern_mtp.h"

#include <cstring>

#include "formats/bytes.h"
#include "formats/crc32.h"
#include "formats/legacy_upgrade.h"

namespace ot {

void encodePattern(const Pattern& p, uint8_t out[mtp::kFileSize]) {
    std::memset(out, 0, mtp::kFileSize);
    FileHeader h{{'K', 'S'}, 2, {}, {}, uint16_t(mtp::kFileSize)};
    std::memcpy(h.fwVersion, mtp::kFwVersion, 4);
    std::memset(h.structVersion, mtp::kStructVersion, 4);
    writeFileHeader(out, h);
    for (int t = 0; t < kTrackCount; ++t) {
        uint8_t* tr = out + mtp::kTracksOff + t * mtp::kTrackSize;
        const Track& track = p.tracks[t];
        tr[0] = track.lastStep;
        for (int s = 0; s < kStepCount; ++s) {
            uint8_t* st = tr + 1 + s * mtp::kStepSize;
            const Step& step = track.steps[s];
            st[0] = uint8_t(step.note);
            st[1] = step.instrument;
            // On disk: FX slot 1 first, then FX slot 0 (spec 02 §4.2).
            st[2] = step.fx[1].type;
            st[3] = step.fx[1].value;
            st[4] = step.fx[0].type;
            st[5] = step.fx[0].value;
        }
    }
    wrU32(out + mtp::kCrcOff, crc32(out, mtp::kCrcOff));
}

namespace {
void resetUpperTracks(Pattern& p) {
    for (int t = kLegacyTrackCount; t < kTrackCount; ++t) p.tracks[t].clear(p.tracks[0].lastStep);
}
}  // namespace

void decodePattern(const uint8_t* file, size_t len, Pattern& p, const PatternUpgradeContext* ctx,
                   PatternLoadInfo* info) {
    // Read up to 0x242C bytes into a zeroed buffer (spec 02 §0, §4.4 step 1).
    auto byteAt = [&](uint32_t off) -> uint8_t { return off < len ? file[off] : 0; };

    PatternLoadInfo li;
    li.structVersion = byteAt(8);
    li.sizeField = uint16_t(byteAt(0x0C) | (byteAt(0x0D) << 8));
    li.storedCrc = uint32_t(byteAt(mtp::kCrcOff)) | (uint32_t(byteAt(mtp::kCrcOff + 1)) << 8) |
                   (uint32_t(byteAt(mtp::kCrcOff + 2)) << 16) | (uint32_t(byteAt(mtp::kCrcOff + 3)) << 24);
    {
        uint32_t covered = len < mtp::kCrcOff ? uint32_t(len) : mtp::kCrcOff;
        uint32_t st = crc32Update(crc32Init(), file, covered);
        static const uint8_t zeros[64] = {};
        for (uint32_t rem = mtp::kCrcOff - covered; rem;) {
            uint32_t c = rem < sizeof(zeros) ? rem : uint32_t(sizeof(zeros));
            st = crc32Update(st, zeros, c);
            rem -= c;
        }
        li.computedCrc = crc32Final(st);
    }

    for (int t = 0; t < kTrackCount; ++t) {
        uint32_t tr = mtp::kTracksOff + t * mtp::kTrackSize;
        Track& track = p.tracks[t];
        track.lastStep = byteAt(tr);
        for (int s = 0; s < kStepCount; ++s) {
            uint32_t st = tr + 1 + s * mtp::kStepSize;
            Step& step = track.steps[s];
            step.note = int8_t(byteAt(st));
            step.instrument = byteAt(st + 1);
            step.fx[1].type = byteAt(st + 2);
            step.fx[1].value = byteAt(st + 3);
            step.fx[0].type = byteAt(st + 4);
            step.fx[0].value = byteAt(st + 5);
        }
    }

    // spec 10 §2.2 steps a-d for structure < 5 (tracks 0..7, scan rule).
    const uint8_t v = li.structVersion;
    if (v < mtp::kStructVersion) {
        legacy::PatternUpgradeParams prm;
        if (ctx) {
            prm.instrumentPlayModes = ctx->instrumentPlayModes;
            prm.beatSliceReference = ctx->beatSliceReference;
            prm.curveV2 = ctx->volumeCurveV2;
            prm.curveV4 = ctx->volumeCurveV4;
        }
        li.upgraded = legacy::upgradePattern(p, v, prm);
        li.upperTracksReset = true;
    }

    // spec 02 §4.4 step 3: size field != 0x242C resets tracks 8..11.
    if (li.sizeField != mtp::kFileSize) {
        resetUpperTracks(p);
        li.upperTracksReset = true;
    }

    if (info) *info = li;
}

}  // namespace ot
