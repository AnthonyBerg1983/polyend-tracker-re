// pattern_NN.mtp codec tests (spec 02 §4).
#include "test.h"

#include "formats/bytes.h"
#include "formats/crc32.h"
#include "formats/pattern_mtp.h"
#include "model/fx.h"
#include "model/instrument.h"

using namespace ot;

static uint8_t g_buf[mtp::kFileSize];
static uint8_t g_buf2[mtp::kFileSize];
static Pattern g_p, g_q;

static uint32_t stepOff(int t, int s) { return 0x1C + t * 0x301 + 1 + s * 6; }

static void fillRandom(Pattern& p, uint32_t seed) {
    for (int t = 0; t < kTrackCount; ++t) {
        p.tracks[t].lastStep = uint8_t((seed >> 3) % 128);
        for (int s = 0; s < kStepCount; ++s) {
            seed = seed * 1664525u + 1013904223u;
            Step& st = p.tracks[t].steps[s];
            st.note = int8_t(int(seed >> 24) % 132 - 4);
            st.instrument = uint8_t((seed >> 8) % 64);
            st.fx[0] = {uint8_t((seed >> 12) % kFxCount), uint8_t(seed >> 16)};
            st.fx[1] = {uint8_t((seed >> 4) % kFxCount), uint8_t(seed >> 20)};
        }
    }
}

TEST(sizes) {
    CHECK_EQ(mtp::kFileSize, 9260u);
    // Header 0x10 + reserved 12 = 0x1C bytes before the tracks. (spec 02 §4.1
    // states "32 + 769*T + 4", which does not add up; the stated totals 9260 /
    // 6184 and the 0x1C track offset imply 28.)
    CHECK_EQ(28u + 769u * 12u + 4u, mtp::kFileSize);
    CHECK_EQ(28u + 769u * 8u + 4u, mtp::kLegacyFileSize);
}

TEST(default_golden) {
    g_p.setDefault();
    encodePattern(g_p, g_buf);
    CHECK_BYTES(g_buf, 'K', 'S', 2, 0, 1, 9, 2, 1, 5, 5, 5, 5, 0x2C, 0x24, 0, 0);
    for (int i = 0x10; i < 0x1C; ++i) CHECK_EQ(g_buf[i], 0);
    for (int t = 0; t < kTrackCount; ++t) CHECK_EQ(g_buf[0x1C + t * 0x301], 31);
    CHECK_BYTES(g_buf + stepOff(0, 0), 0xFF, 0, 0, 0, 0, 0);
    CHECK_BYTES(g_buf + stepOff(11, 127), 0xFF, 0, 0, 0, 0, 0);
    CHECK_EQ(stepOff(11, 127) + 6, 0x2428u);
    CHECK_EQ(rdU32(g_buf + 0x2428), crc32(g_buf, 0x2428));
}

TEST(step_layout_fx_order) {
    g_p.setDefault();
    Step& s = g_p.tracks[2].steps[5];
    s.note = 60;
    s.instrument = 49;
    s.fx[0] = {uint8_t(FxId::Volume), 80};    // slot 0 -> bytes +4/+5
    s.fx[1] = {uint8_t(FxId::Panning), 25};   // slot 1 -> bytes +2/+3
    g_p.tracks[2].lastStep = 63;
    g_p.tracks[4].steps[0].note = kNoteOff;
    g_p.tracks[4].steps[1].note = kNoteCut;
    g_p.tracks[4].steps[2].note = kNoteFade;
    encodePattern(g_p, g_buf);
    CHECK_EQ(g_buf[0x1C + 2 * 0x301], 63);
    CHECK_BYTES(g_buf + stepOff(2, 5), 60, 49, 31, 25, 18, 80);
    CHECK_BYTES(g_buf + stepOff(4, 0), 0xFC, 0, 0, 0, 0, 0, 0xFD, 0, 0, 0, 0, 0, 0xFE);
}

TEST(round_trip_random) {
    for (uint32_t seed : {1u, 77u, 0xDEADBEEFu}) {
        fillRandom(g_p, seed);
        encodePattern(g_p, g_buf);
        PatternLoadInfo info;
        decodePattern(g_buf, sizeof(g_buf), g_q, nullptr, &info);
        CHECK(!info.upgraded);
        CHECK(!info.upperTracksReset);
        CHECK_EQ(info.structVersion, 5);
        CHECK_EQ(info.storedCrc, info.computedCrc);
        bool same = true;
        for (int t = 0; t < kTrackCount; ++t) {
            same = same && g_p.tracks[t].lastStep == g_q.tracks[t].lastStep;
            for (int s = 0; s < kStepCount; ++s) same = same && g_p.tracks[t].steps[s] == g_q.tracks[t].steps[s];
        }
        CHECK(same);
        encodePattern(g_q, g_buf2);
        CHECK(std::memcmp(g_buf, g_buf2, sizeof(g_buf)) == 0);
    }
}

TEST(ids_type_crc_not_checked) {
    fillRandom(g_p, 5);
    encodePattern(g_p, g_buf);
    g_buf[0] = 'P';
    g_buf[1] = 'M';
    wrU16(g_buf + 2, 0);
    wrU32(g_buf + 0x2428, 0);
    PatternLoadInfo info;
    decodePattern(g_buf, sizeof(g_buf), g_q, nullptr, &info);
    CHECK(!info.upperTracksReset);
    CHECK(g_q.tracks[11].steps[7] == g_p.tracks[11].steps[7]);
    CHECK(info.storedCrc != info.computedCrc);
}

TEST(size_field_wrong_resets_upper_tracks) {
    // tracker-lib writes size 0 (spec 02 §11): tracks 9..12 get wiped.
    fillRandom(g_p, 9);
    g_p.tracks[0].lastStep = 15;
    encodePattern(g_p, g_buf);
    wrU16(g_buf + 0x0C, 0);
    PatternLoadInfo info;
    decodePattern(g_buf, sizeof(g_buf), g_q, nullptr, &info);
    CHECK(info.upperTracksReset);
    CHECK(!info.upgraded);
    for (int t = 0; t < 8; ++t) CHECK(g_q.tracks[t].steps[3] == g_p.tracks[t].steps[3]);
    for (int t = 8; t < 12; ++t) {
        CHECK_EQ(g_q.tracks[t].lastStep, 15);
        for (int s = 0; s < kStepCount; ++s) {
            const Step& st = g_q.tracks[t].steps[s];
            CHECK(st.note == kNoteEmpty && st.instrument == 0 && st.fx[0].type == 0 && st.fx[1].type == 0);
            // Values untouched by the firmware clear routine.
            CHECK_EQ(st.fx[0].value, g_p.tracks[t].steps[s].fx[0].value);
        }
    }
}

TEST(legacy_8_track_file) {
    // A 6184-byte 8-track file: structure 5 but size field 6184.
    fillRandom(g_p, 3);
    g_p.tracks[0].lastStep = 63;
    encodePattern(g_p, g_buf);
    wrU16(g_buf + 0x0C, uint16_t(mtp::kLegacyFileSize));
    PatternLoadInfo info;
    decodePattern(g_buf, mtp::kLegacyFileSize, g_q, nullptr, &info);
    CHECK(info.upperTracksReset);
    for (int t = 0; t < 8; ++t) CHECK(g_q.tracks[t].steps[100] == g_p.tracks[t].steps[100]);
    for (int t = 8; t < 12; ++t) {
        CHECK_EQ(g_q.tracks[t].lastStep, 63);
        CHECK_EQ(g_q.tracks[t].steps[0].note, kNoteEmpty);
    }
}

static uint8_t plusOne(uint8_t v) { return uint8_t(v + 1); }
static uint8_t timesTwo(uint8_t v) { return uint8_t(v * 2); }

TEST(upgrade_chain) {
    g_p.setDefault();
    // Track 0: beat-slice instrument 3 note, normal instrument 4 note, volume FX.
    g_p.tracks[0].steps[0] = Step{40, 3, {{18, 10}, {0, 0}}};
    g_p.tracks[0].steps[1] = Step{40, 4, {{0, 0}, {18, 20}}};
    g_p.tracks[0].steps[2] = Step{118, 3, {}};
    g_p.tracks[0].steps[3] = Step{kNoteOff, 3, {}};
    g_p.tracks[0].lastStep = 47;
    // Track 9 data must be wiped by v<5; FX 18 there must not be scanned.
    g_p.tracks[9].steps[0] = Step{50, 3, {{18, 10}, {0, 0}}};
    encodePattern(g_p, g_buf);
    std::memset(g_buf + 8, 1, 4);  // structure 1.1.1.1

    uint8_t modes[kSampleInstrumentCount] = {};
    modes[3] = uint8_t(PlayMode::BeatSlice);
    PatternUpgradeContext ctx;
    ctx.instrumentPlayModes = modes;
    ctx.beatSliceReference = 7;  // transpose by 12 - 7 = +5
    ctx.volumeCurveV2 = &plusOne;
    ctx.volumeCurveV4 = &timesTwo;
    PatternLoadInfo info;
    decodePattern(g_buf, sizeof(g_buf), g_q, &ctx, &info);
    CHECK(info.upgraded);
    CHECK(info.upperTracksReset);
    CHECK_EQ(info.structVersion, 1);
    CHECK_EQ(g_q.tracks[0].steps[0].note, 45);
    CHECK_EQ(g_q.tracks[0].steps[1].note, 40);
    CHECK_EQ(g_q.tracks[0].steps[2].note, 120);  // clamped
    CHECK_EQ(g_q.tracks[0].steps[3].note, kNoteOff);
    CHECK_EQ(g_q.tracks[0].steps[0].fx[0].value, (10 + 1) * 2);  // v2 then v4 curve
    CHECK_EQ(g_q.tracks[0].steps[1].fx[1].value, (20 + 1) * 2);
    CHECK_EQ(g_q.tracks[9].steps[0].note, kNoteEmpty);
    CHECK_EQ(g_q.tracks[9].lastStep, 47);

    // Structure 3: only the v<4 curve and the track reset apply.
    std::memset(g_buf + 8, 3, 4);
    decodePattern(g_buf, sizeof(g_buf), g_q, &ctx, &info);
    CHECK_EQ(g_q.tracks[0].steps[0].note, 40);
    CHECK_EQ(g_q.tracks[0].steps[0].fx[0].value, 20);

    // Structure 4 without a context: only the track reset.
    std::memset(g_buf + 8, 4, 4);
    decodePattern(g_buf, sizeof(g_buf), g_q, nullptr, &info);
    CHECK(info.upgraded);
    CHECK_EQ(g_q.tracks[0].steps[0].fx[0].value, 10);
    CHECK_EQ(g_q.tracks[9].steps[0].note, kNoteEmpty);
}

TEST(empty_file) {
    PatternLoadInfo info;
    decodePattern(nullptr, 0, g_q, nullptr, &info);
    CHECK(info.upgraded);  // structure byte reads 0
    CHECK_EQ(g_q.tracks[0].lastStep, 0);
    CHECK_EQ(g_q.tracks[0].steps[0].note, 0);
    CHECK_EQ(g_q.tracks[8].steps[0].note, kNoteEmpty);
}
