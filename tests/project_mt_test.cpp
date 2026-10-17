// project.mt codec tests (spec 02 §3).
#include "test.h"

#include <cstdlib>
#include <string>
#include <vector>

#include "formats/bytes.h"
#include "formats/crc32.h"
#include "formats/project_mt.h"

using namespace ot;

static std::vector<uint8_t> loadFixture(const char* name) {
    const char* dir = std::getenv("OT_FIXTURES");
    std::string path = std::string(dir ? dir : "fixtures") + "/" + name;
    std::vector<uint8_t> v;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return v;
    int c;
    while ((c = std::fgetc(f)) != EOF) v.push_back(uint8_t(c));
    std::fclose(f);
    return v;
}

static uint8_t g_buf[mt::kFileSize];
static uint8_t g_buf2[mt::kFileSize];

TEST(default_header_golden) {
    Project p;
    encodeProject(p, g_buf);
    // "MT", type 0, fw 1.9.2.255, structure 17x4, size 0x914, pad 0.
    CHECK_BYTES(g_buf, 'M', 'T', 0x00, 0x00, 1, 9, 2, 255, 17, 17, 17, 17, 0x14, 0x09, 0x00, 0x00);
}

TEST(default_fields_golden) {
    Project p;
    encodeProject(p, g_buf);
    CHECK_EQ(g_buf[0x010], 1);  // playlist slot 0
    CHECK_EQ(g_buf[0x011], 0);
    CHECK_EQ(g_buf[0x10F], 0);  // playlist position
    CHECK_EQ(g_buf[0x114], 1);  // current pattern
    CHECK_BYTES(g_buf + 0x115, 0, 11, 24, 8);
    CHECK_BYTES(g_buf + 0x1C0, 0x00, 0x00, 0x02, 0x43, 0x20, 0x00);  // 130.0f, 32
    CHECK_BYTES(g_buf + 0x142, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0, 1, 0);
    CHECK_BYTES(g_buf + 0x152, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12);
    CHECK_BYTES(g_buf + 0x428, 'T', 'r', 'a', 'c', 'k', ' ', '1', 0);
    CHECK_BYTES(g_buf + 0x428 + 21, 'T', 'r', 'a', 'c', 'k', ' ', '2', 0);
    CHECK_BYTES(g_buf + 0x4FD, 0, 127, 1, 5, 7, 11, 64, 128, 0, 0, 0, 0, 0, 0, 0, 0, 1, 127);
    CHECK_BYTES(g_buf + 0x5FE, 0x20, 0x4E);  // 20000
    CHECK_BYTES(g_buf + 0x603, 'M', 'i', 'd', 'i', ' ', '9', 0, 0, 'M', 'i', 'd', 'i', ' ', '1', '0', 0);
    CHECK_BYTES(g_buf + 0x810, 'N', 'e', 'w', ' ', 'P', 'r', 'o', 'j', 'e', 'c', 't', 0);
    CHECK_EQ(g_buf[0x831], 0);
    CHECK_EQ(rdU32(g_buf + 0x910), crc32(g_buf, 0x910));
}

TEST(round_trip) {
    static Project p, q;
    p.setTempo(97.25f);
    p.setCurrentPattern(17);
    for (int i = 0; i < 20; ++i) p.playlist[i] = uint8_t(i + 3);
    p.playlistPos = 4;
    p.setTrackName(2, "Kick");
    p.setTrackName(12, "Synth");
    p.setName("My Song");
    p.setFolder("Projects");
    p.setU8(0x4DD, 77);
    encodeProject(p, g_buf);
    ProjectLoadInfo info;
    CHECK(decodeProject(g_buf, sizeof(g_buf), q, &info) == FmtStatus::Ok);
    CHECK(!info.sizeMismatch);
    CHECK(!info.upgraded);
    CHECK_EQ(info.structVersion, 17);
    CHECK_EQ(info.storedCrc, info.computedCrc);
    CHECK(std::memcmp(p.values, q.values, sizeof(p.values)) == 0);
    CHECK(std::memcmp(p.playlist, q.playlist, sizeof(p.playlist)) == 0);
    CHECK_EQ(q.playlistPos, 4);
    CHECK_STR(q.name, "My Song");
    CHECK_STR(q.folder, "Projects");
    encodeProject(q, g_buf2);
    CHECK(std::memcmp(g_buf, g_buf2, sizeof(g_buf)) == 0);
}

TEST(bad_crc_accepted) {
    Project p;
    encodeProject(p, g_buf);
    wrU32(g_buf + 0x910, 0);
    Project q;
    q.setTempo(1.0f);
    CHECK(decodeProject(g_buf, sizeof(g_buf), q) == FmtStatus::Ok);
    CHECK_NEAR(q.tempo(), 130.0, 0);
}

TEST(type_must_be_zero) {
    Project p;
    encodeProject(p, g_buf);
    wrU16(g_buf + 2, 1);  // tracker-lib createProject writes 1 (spec 02 §11)
    Project q;
    q.setName("Untouched");
    CHECK(decodeProject(g_buf, sizeof(g_buf), q) == FmtStatus::WrongType);
    CHECK_STR(q.name, "Untouched");
}

TEST(size_mismatch_unsaved) {
    Project p;
    p.setName("Named");
    p.setFolder("Projects");
    encodeProject(p, g_buf);
    wrU16(g_buf + 0x0C, 0x900);
    Project q;
    ProjectLoadInfo info;
    CHECK(decodeProject(g_buf, sizeof(g_buf), q, &info) == FmtStatus::Ok);
    CHECK(info.sizeMismatch);
    CHECK_STR(q.name, "Unsaved Project");
    CHECK_STR(q.folder, "");
    CHECK_EQ(q.u8(0x140), 3);
}

TEST(short_file_reads_zero) {
    Project p;
    p.setTempo(150.0f);
    encodeProject(p, g_buf);
    // Truncate before the tempo field: the tail reads 0 (spec 10 §0.2).
    Project q;
    q.setTempo(50.0f);
    CHECK(decodeProject(g_buf, 0x100, q) == FmtStatus::Ok);
    CHECK_NEAR(q.tempo(), 0.0, 0);
    CHECK_STR(q.name, "");  // size field read from file = 0x914, name bytes 0
}

TEST(name_nul_forced) {
    Project p;
    encodeProject(p, g_buf);
    std::memset(g_buf + 0x810, 'A', 0x100);  // fill name + start of folder
    std::memset(g_buf + 0x831, 'B', 0x910 - 0x831);
    Project q;
    CHECK(decodeProject(g_buf, sizeof(g_buf), q) == FmtStatus::Ok);
    CHECK_EQ(std::strlen(q.name), 32u);
    CHECK_EQ(std::strlen(q.folder), 221u);
    CHECK_EQ(q.folder[0], 'B');
    encodeProject(q, g_buf2);
    CHECK_EQ(g_buf2[0x830], 0);
    CHECK_EQ(g_buf2[0x90E], 0);
}

TEST(sanitiser) {
    Project p;
    encodeProject(p, g_buf);
    g_buf[0x110] = 0xF0;        // <0 -> 48
    g_buf[0x111] = 64;          // >63 -> 0
    g_buf[0x113] = 50;          // >49 -> 0
    g_buf[0x12C + 3] = 2;       // >1 -> 0
    g_buf[0x134] = 4;           // >3 -> 0
    wrU16(g_buf + 0x142, 0);    // not 1..255 -> 1
    wrU16(g_buf + 0x144, 256);
    wrU16(g_buf + 0x146, 255);  // valid
    wrU16(g_buf + 0x15E, 7);    // [0] forced 0
    wrU16(g_buf + 0x160, uint16_t(int16_t(-255)));  // valid
    wrU16(g_buf + 0x162, uint16_t(int16_t(-256)));  // -> 0
    wrU16(g_buf + 0x164, 300);                      // -> 0
    wrF32(g_buf + 0x1C0, 1000.5f);                  // -> 130
    wrU16(g_buf + 0x1C4, 256);                      // -> 32
    g_buf[0x2F6 + 11] = 4;                          // -> 0
    g_buf[0x2F6 + 10] = 3;                          // valid
    g_buf[0x4FD + 16 * 2] = 17;                     // channel > 16 -> record index 2
    g_buf[0x4FD + 16 * 6] = 16;                     // valid
    g_buf[0x4FD + 16 * 3 + 1] = 0x80;               // velocity bit7 -> 127
    g_buf[0x4FD + 16 * 4 + 2] = 131;                // CC > 130 -> 0
    g_buf[0x4FD + 16 * 4 + 3] = 130;                // valid
    wrU16(g_buf + 0x5FE, 99);                       // -> 20000
    g_buf[0x600] = 2;
    g_buf[0x601] = 5;
    g_buf[0x602] = 2;
    g_buf[0x742 + 7] = 9;
    g_buf[0x80C] = 101;
    Project q;
    CHECK(decodeProject(g_buf, sizeof(g_buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.u8(0x110), 48);
    CHECK_EQ(q.u8(0x111), 0);
    CHECK_EQ(q.u8(0x113), 0);
    CHECK_EQ(q.u8(0x12F), 0);
    CHECK_EQ(q.u8(0x134), 0);
    CHECK_EQ(q.u16(0x142), 1);
    CHECK_EQ(q.u16(0x144), 1);
    CHECK_EQ(q.u16(0x146), 255);
    CHECK_EQ(q.u16(0x15E), 0);
    CHECK_EQ(int16_t(q.u16(0x160)), -255);
    CHECK_EQ(q.u16(0x162), 0);
    CHECK_EQ(q.u16(0x164), 0);
    CHECK_NEAR(q.tempo(), 130.0, 0);
    CHECK_EQ(q.u16(0x1C4), 32);
    CHECK_EQ(q.u8(0x2F6 + 11), 0);
    CHECK_EQ(q.u8(0x2F6 + 10), 3);
    CHECK_EQ(q.midiInstrument(2).channel, 2);
    CHECK_EQ(q.midiInstrument(6).channel, 16);
    CHECK_EQ(q.midiInstrument(3).velocity, 127);
    CHECK_EQ(q.midiInstrument(4).cc[0], 0);
    CHECK_EQ(q.midiInstrument(4).cc[1], 130);
    CHECK_EQ(q.u16(0x5FE), 20000);
    CHECK_EQ(q.u8(0x600), 0);
    CHECK_EQ(q.u8(0x601), 0);
    CHECK_EQ(q.u8(0x602), 0);
    CHECK_EQ(q.u8(0x749), 0);
    CHECK_EQ(q.u8(0x80C), 0);

    // Track-index check (0x152) only with a runtime limit.
    encodeProject(p, g_buf);
    g_buf[0x152] = 40;
    CHECK(decodeProject(g_buf, sizeof(g_buf), q) == FmtStatus::Ok);
    CHECK_EQ(q.u8(0x152), 40);
    CHECK(decodeProject(g_buf, sizeof(g_buf), q, nullptr, 13) == FmtStatus::Ok);
    CHECK_EQ(q.u8(0x152), 1);
}

static int g_upgradeFrom = -1;
static uint32_t g_upgradeLen = 0;
static void upgradeHook(uint8_t* block, uint32_t len, uint8_t from) {
    g_upgradeFrom = from;
    g_upgradeLen = len;
    block[0x114 - 0x10] = 9;  // pretend the upgrade changed the current pattern
}

TEST(upgrade_hook) {
    Project p;
    encodeProject(p, g_buf);
    g_buf[8] = 16;
    setProjectUpgradeHook(&upgradeHook);
    Project q;
    ProjectLoadInfo info;
    CHECK(decodeProject(g_buf, sizeof(g_buf), q, &info) == FmtStatus::Ok);
    CHECK(info.upgraded);
    CHECK_EQ(g_upgradeFrom, 16);
    CHECK_EQ(g_upgradeLen, 0x800u);
    CHECK_EQ(q.currentPattern(), 9);
    g_upgradeFrom = -1;
    g_buf[8] = 17;
    CHECK(decodeProject(g_buf, sizeof(g_buf), q, &info) == FmtStatus::Ok);
    CHECK_EQ(g_upgradeFrom, -1);
    setProjectUpgradeHook(nullptr);
}

TEST(tracker_lib_template) {
    std::vector<uint8_t> tpl = loadFixture("tl_project_template.mt");
    CHECK_EQ(tpl.size(), size_t(mt::kFileSize));
    if (tpl.size() != mt::kFileSize) return;
    static Project q;
    ProjectLoadInfo info;
    CHECK(decodeProject(tpl.data(), tpl.size(), q, &info) == FmtStatus::Ok);
    CHECK_EQ(info.structVersion, 17);
    CHECK_EQ(info.storedCrc, 0u);  // tracker-lib writes 0 (spec 02 §11)
    CHECK_NEAR(q.tempo(), 130.0, 0);
    CHECK_EQ(q.defaultPatternLength(), 32);
    CHECK_EQ(q.currentPattern(), 1);
    CHECK_EQ(q.playlist[0], 1);
    CHECK_STR(q.name, "New Project");
    CHECK_STR(q.folder, "Projects");
    char buf[32];
    q.trackName(0, buf, sizeof(buf));
    CHECK_STR(buf, "Track 1");
    q.trackName(15, buf, sizeof(buf));
    CHECK_STR(buf, "Midi 16");
    CHECK_EQ(q.midiInstrument(15).channel, 15);
    CHECK_NEAR(q.f32(mt::kReverbDiffusion), 0.68, 1e-6);
    // Song + values blocks survive a re-encode byte-for-byte.
    encodeProject(q, g_buf);
    CHECK(std::memcmp(g_buf + 0x10, tpl.data() + 0x10, 0x800) == 0);
    CHECK(std::memcmp(g_buf, tpl.data(), 0x0E) == 0);
}
