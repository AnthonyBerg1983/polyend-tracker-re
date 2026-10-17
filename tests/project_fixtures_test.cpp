// Regression test over every project on the user's SD-card backup
// (fixtures/sdcard-backup-2026-10-15/Projects, read in place, never copied
// into the repo; skipped when absent). Field bug: "Legowelt - Tracker Demo"
// opened with empty patterns and the open blocked the main loop for 8 s.
//
// For each project: open it through the background job (spec 05 appendix E3)
// one main-loop pass at a time, check that every pass does a bounded amount
// of card I/O, that every pattern has the same number of non-empty steps as
// a direct read of its file, that the reloaded Workspace (boot path) agrees,
// and that playing the current pattern makes sound when it should.
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test.h"
#include "ui_fakes.h"

#include "app/project_io.h"
#include "formats/file_io.h"
#include "formats/pattern_mtp.h"

using namespace ot;
namespace sfs = std::filesystem;

namespace {

std::string backupRoot() {
    if (const char* e = std::getenv("OT_LEGACY_FIXTURES")) return std::string(e) + "/sdcard-backup-2026-10-15";
    if (const char* e = std::getenv("OT_FIXTURES")) return std::string(e) + "/../../../fixtures/sdcard-backup-2026-10-15";
    return "../../fixtures/sdcard-backup-2026-10-15";
}

// Storage that counts the bytes moved since the last reset.
struct MeteredStorage : FakeStorage {
    uint64_t io = 0;
    int32_t read(Handle h, void* d, uint32_t n) override {
        const int32_t r = FakeStorage::read(h, d, n);
        if (r > 0) io += uint32_t(r);
        return r;
    }
    int32_t write(Handle h, const void* s, uint32_t n) override {
        const int32_t r = FakeStorage::write(h, s, n);
        if (r > 0) io += uint32_t(r);
        return r;
    }
};

std::vector<uint8_t> readHostFile(const sfs::path& p) {
    std::ifstream f(p, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

// Non-empty steps of a pattern file read straight from its bytes (spec 02
// §4.2: note 0xFF and both FX types 0 = empty; legacy 6184-byte files hold 8
// tracks, the upgrade adds 4 empty ones).
int rawNonEmpty(const std::vector<uint8_t>& f) {
    const int tracks = f.size() >= mtp::kFileSize ? kTrackCount : kLegacyTrackCount;
    int n = 0;
    for (int t = 0; t < tracks; ++t)
        for (int s = 0; s < kStepCount; ++s) {
            const size_t o = mtp::kTracksOff + size_t(t) * mtp::kTrackSize + 1 + size_t(s) * mtp::kStepSize;
            if (o + 5 >= f.size()) continue;
            if (f[o] != 0xFF || f[o + 2] != 0 || f[o + 4] != 0) ++n;
        }
    return n;
}

int modelNonEmpty(const Pattern& p) {
    int n = 0;
    for (const Track& t : p.tracks)
        for (const Step& s : t.steps)
            if (s.note != -1 || s.fx[0].type != 0 || s.fx[1].type != 0) ++n;
    return n;
}

// A sample note on an unmuted track with a loaded instrument (the pattern
// should then make sound).
bool expectsAudio(App& a, const Pattern& p) {
    for (int t = 0; t < 8; ++t) {
        if (a.trackMuted(t)) continue;
        const Track& tr = p.tracks[t];
        for (int s = 0; s <= tr.lastStep && s < kStepCount; ++s) {
            const Step& st = tr.steps[s];
            if (st.note >= 0 && st.instrument < kSampleInstrumentCount && a.instrPresent[st.instrument]) return true;
        }
    }
    return false;
}

}  // namespace

TEST(fixture_backup_projects_open_incrementally) {
    const sfs::path projects = sfs::path(backupRoot()) / "Projects";
    std::error_code ec;
    if (!sfs::is_directory(projects, ec)) {
        std::printf("  SKIP: %s absent\n", projects.string().c_str());
        return;
    }
    std::vector<sfs::path> dirs;
    for (auto it = sfs::recursive_directory_iterator(projects, ec); it != sfs::recursive_directory_iterator(); it.increment(ec))
        if (it->is_regular_file(ec) && it->path().filename() == "project.mt") dirs.push_back(it->path().parent_path());
    std::sort(dirs.begin(), dirs.end());
    CHECK(!dirs.empty());
    int opened = 0, audible = 0;
    for (const sfs::path& dir : dirs) {
        const std::string rel = "Projects/" + sfs::relative(dir, projects).generic_string();
        Harness h(false, 0xB40000 / 2);   // device sample pool (spec 02 §8.2)
        MeteredStorage ms;
        HalRefs hr = h.app->hal();
        hr.storage = &ms;
        App& a = *h.app;
        CHECK(a.init(hr, a.mem()));
        CHECK(a.boot());
        // Card image: the project folder (and its parents).
        for (size_t p = rel.find('/'); p != std::string::npos; p = rel.find('/', p + 1)) ms.dirs.insert(rel.substr(0, p));
        ms.dirs.insert(rel);
        int expected[kPatternCount + 1] = {};
        for (auto it = sfs::recursive_directory_iterator(dir, ec); it != sfs::recursive_directory_iterator(); it.increment(ec)) {
            const std::string r = rel + "/" + sfs::relative(it->path(), dir).generic_string();
            if (it->is_directory()) {
                ms.dirs.insert(r);
                continue;
            }
            ms.files[r] = readHostFile(it->path());
            const std::string fn = it->path().filename().string();
            int num = 0;
            if (it->path().parent_path().filename() == "patterns" && std::sscanf(fn.c_str(), "pattern_%d.mtp", &num) == 1 &&
                num >= 1 && num <= kPatternCount)
                expected[num] = rawNonEmpty(ms.files[r]);
        }
        const std::string name = dir.filename().string();
        const std::string folder = rel.substr(0, rel.size() - name.size() - 1);
        if (!projectFitsMemory(a, folder.c_str(), name.c_str())) {
            std::printf("  %-40s too big for the sample pool, skipped\n", name.c_str());
            continue;
        }
        if (name.size() > mt::kNameMax) {
            // spec 05 appendix E3 OPEN (0x1d2e0): names over 32 chars are refused.
            CHECK(!a.startOpenProject(folder.c_str(), name.c_str()));
            std::printf("  %-40s name > 32 chars: refused like stock\n", name.c_str());
            continue;
        }
        // Background open, one main-loop pass at a time.
        h.tap(btn::Pattern);
        CHECK(a.startOpenProject(folder.c_str(), name.c_str()));
        int passes = 0;
        uint64_t maxIo = 0;
        bool popup = false;
        while (a.jobBusy() && passes < 200000) {
            ms.io = 0;
            h.clock.ms += 1;
            a.loop();
            ++passes;
            if (ms.io > maxIo) maxIo = ms.io;
            popup = popup || h.disp.lastHasText("Opening project");
        }
        CHECK(!a.jobBusy());
        CHECK(a.lastJobOk());
        CHECK(a.lastJobErrors() == 0);
        CHECK(std::strcmp(a.project.name, name.c_str()) == 0);
        // spec 05 §1.3: bounded work per pass (a unit may finish one chunk
        // past the budget: copy = read + write).
        CHECK(maxIo <= 4u * kJobPassBytes);
        CHECK(passes == 1 || popup);
        int bad = 0, steps = 0;
        for (int n = 1; n <= kPatternCount; ++n) {
            const int got = modelNonEmpty(a.stored(n));
            if (got != expected[n]) {
                if (!bad) std::printf("  %s pattern %d: %d non-empty steps, file has %d\n", name.c_str(), n, got, expected[n]);
                ++bad;
            }
            steps += got;
        }
        CHECK(bad == 0);
        CHECK(modelNonEmpty(a.pattern()) == expected[a.patternNumber()]);
        // Boot path: the same Workspace loads to the same patterns.
        CHECK(a.loadWorkspace());
        CHECK(a.lastJobErrors() == 0);
        int bad2 = 0;
        for (int n = 1; n <= kPatternCount; ++n) bad2 += modelNonEmpty(a.stored(n)) != expected[n];
        CHECK(bad2 == 0);
        // Audio: play the current pattern.
        int peak = 0;
        const bool want = expectsAudio(a, a.pattern());
        if (want) {
            h.tap(btn::Play);
            CHECK(a.seq.isPlaying());
            for (int k = 0; k < 6 && peak <= 100; ++k) {
                peak = std::max(peak, h.audio.pump(500));
                h.run(10);
            }
            h.tap(btn::Play);
            CHECK(peak > 100);
            if (peak > 100) ++audible;
        }
        int instr = 0;
        for (int i = 0; i < kSampleInstrumentCount; ++i) instr += a.instrPresent[i];
        std::printf("  %-40s %3d passes, max %6llu B/pass, %5d steps, %2d instr, peak %d\n", name.c_str(), passes,
                    static_cast<unsigned long long>(maxIo), steps, instr, peak);
        ++opened;
    }
    std::printf("  %d projects opened, %d audible\n", opened, audible);
    CHECK(opened > 0);
}

// Field bug #1 regression on a synthetic card: a pattern file that exists
// but cannot be read is reported (job error) instead of silently becoming an
// empty pattern, and the readable ones still load.
namespace {
struct FlakyStorage : FakeStorage {
    std::string failPath;
    int failReads = 0;
    std::map<Handle, std::string> paths;
    Handle open(const char* path, OpenMode m) override {
        const Handle h = FakeStorage::open(path, m);
        if (h >= 0) paths[h] = path;
        return h;
    }
    int32_t read(Handle h, void* d, uint32_t n) override {
        if (failReads > 0 && paths[h] == failPath) {
            --failReads;
            return -1;
        }
        return FakeStorage::read(h, d, n);
    }
};
}  // namespace

TEST(unreadable_pattern_is_reported_not_silently_empty) {
    Harness h(false);
    FlakyStorage fsx;
    HalRefs hr = h.app->hal();
    hr.storage = &fsx;
    App& a = *h.app;
    CHECK(a.init(hr, a.mem()));
    CHECK(a.boot());
    // Two patterns with a note each in the Workspace.
    for (int n = 1; n <= 2; ++n) {
        a.selectPattern(n);
        a.pattern().tracks[0].steps[n].note = 60;
        a.patternEdited();
    }
    a.autosave();
    // A transient read error is retried.
    fsx.failPath = "Workspace/patterns/pattern_02.mtp";
    fsx.failReads = 1;
    CHECK(a.loadWorkspace());
    CHECK(a.lastJobErrors() == 0);
    CHECK(a.stored(2).tracks[0].steps[2].note == 60);
    // A persistent one is reported.
    fsx.failReads = 100;
    CHECK(a.loadWorkspace());
    CHECK(a.lastJobErrors() == 1);
    CHECK(a.stored(1).tracks[0].steps[1].note == 60);
    fsx.failReads = 0;
}

// The periodic autosave runs in the background without a popup and leaves
// the UI responsive.
TEST(autosave_runs_in_background) {
    Harness h;
    App& a = *h.app;
    a.pattern().tracks[0].steps[0].note = 61;
    a.patternEdited();
    h.clock.ms += 10001;
    a.loop();
    CHECK(a.jobBusy());
    for (int i = 0; i < 50 && a.jobBusy(); ++i) {
        CHECK(!h.disp.lastHasText("Saving project"));
        h.clock.ms += 1;
        a.loop();
    }
    CHECK(!a.jobBusy());
    Pattern p;
    CHECK(loadPatternFile(h.fs, "Workspace/patterns/pattern_01.mtp", p) == FmtStatus::Ok);
    CHECK(p.tracks[0].steps[0].note == 61);
}
