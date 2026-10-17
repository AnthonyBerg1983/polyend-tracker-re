// Manual-audit fixes, batch B (docs/MANUAL_AUDIT.md): Sample Playback play-mode
// list, Song paste at the grid position, mixer value formats, export naming,
// Performance label colours, File and Games labels.
#include <cmath>
#include <cstring>
#include <string>

#include "test.h"
#include "ui_fakes.h"
#include "nvstore_fake.h"

#include "app/perf_presets.h"

#include "app/project_fields.h"
#include "formats/wav.h"
#include "ui/screens.h"

using namespace ot;

namespace {
SongScreen& song(Harness& h) { return *static_cast<SongScreen*>(h.app->ui.screen(kModSong)); }
MixerScreen& mixer(Harness& h) { return *static_cast<MixerScreen*>(h.app->ui.screen(kModMaster)); }
const BarLabel& lab(Harness& h, int i) { return h.app->ui.bar.label[i]; }

void loadSaw(Harness& h) {
    std::vector<int16_t> s(8000);
    for (size_t i = 0; i < s.size(); ++i) s[i] = int16_t((int(i) % 200 - 100) * 250);
    std::vector<uint8_t> w(s.size() * 2 + 64);
    w.resize(wavEncode(s.data(), uint32_t(s.size()), 1, w.data(), w.size()));
    h.fs.files["saw.wav"] = w;
    CHECK(h.app->importSample("saw.wav", 0));
    h.app->setCurrentInstrument(0);
}
}  // namespace

// Audit 6.1: the 8-item play-mode list is drawn, with the manual's names.
TEST(playback_draws_play_mode_list) {
    Harness h;
    loadSaw(h);
    h.tap(btn::SamplePlayback);
    h.app->ui.dirty();
    h.run(40);
    for (const char* n : {"1-Shot", "Forward Loop", "Backward Loop", "Pingpong Loop", "Slice", "Beat Slice",
                          "Wavetable", "Granular"})
        CHECK(h.disp.lastHasText(n));
    CHECK_STR(lab(h, 6).bottom, "1-Shot");
}

// Audit 9.1: the paste goes to the grid selection's top-left, track offset
// included, also within the same pattern.
TEST(song_paste_uses_grid_position) {
    Harness h;
    App& a = *h.app;
    a.project.playlist[0] = 1;
    a.project.playlist[1] = 2;
    a.project.playlist[2] = 3;
    a.project.playlist[3] = 0;
    a.selectPattern(1);
    a.pattern().tracks[0].steps[0].note = 60;
    a.stored(1) = a.pattern();
    h.tap(btn::Song);
    SongScreen& s = song(h);
    h.tap(btn::Rec);
    h.tap(btn::Left);   // list -> grid (row = cursor 0, last track)
    CHECK_EQ(s.focus, 0);
    for (int i = 0; i < kTrackCount; ++i) h.tap(btn::Left);
    CHECK_EQ(s.selTrack0, 0);
    CHECK_EQ(s.selRow0, 0);
    h.tap(btn::Copy);   // copy slot 0, track 0
    h.tap(btn::Down);
    h.tap(btn::Right);
    h.tap(btn::Right);
    CHECK_EQ(s.selRow0, 1);
    CHECK_EQ(s.selTrack0, 2);
    h.down(btn::Shift);
    h.tap(btn::Copy);   // paste -> pattern 2, track 2
    h.up(btn::Shift);
    h.run(40);
    CHECK_EQ(a.stored(2).tracks[2].steps[0].note, 60);
    CHECK(a.stored(2).tracks[0].steps[0].note < 0);
    CHECK(a.stored(3).tracks[2].steps[0].note < 0);   // not the list cursor's slot + 1
    // Same pattern, other track.
    h.tap(btn::Up);
    h.tap(btn::Left);
    CHECK_EQ(s.selRow0, 0);
    CHECK_EQ(s.selTrack0, 1);
    h.down(btn::Shift);
    h.tap(btn::Copy);
    h.up(btn::Shift);
    h.run(40);
    CHECK_EQ(a.stored(1).tracks[1].steps[0].note, 60);
    CHECK_EQ(a.stored(1).tracks[0].steps[0].note, 60);
}

// Audit 10.1 / 10.2: delay sync rate and sidechain shown as names.
TEST(mixer_delay_rate_and_sidechain_names) {
    Harness h;
    App& a = *h.app;
    a.project.setU8(pf::kDelayFlags, 0x40 | 5);
    a.project.setU8(pf::kSidechain, 9);
    h.tap(btn::Master);
    MixerScreen& m = mixer(h);
    CHECK_EQ(m.page, 0);
    h.tap(btn::F3);
    CHECK_EQ(m.sub, 2);
    CHECK_STR(lab(h, 2).bottom, "3/2");
    a.project.setU8(pf::kDelayFlags, 0x40 | 18);
    h.tap(btn::Right);
    CHECK_STR(lab(h, 2).bottom, "1/32");
    h.tap(btn::F8);
    h.tap(btn::F4);
    CHECK_EQ(m.sub, 3);
    CHECK_STR(lab(h, 4).bottom, "Line in L");
    a.project.setU8(pf::kSidechain, 0);
    h.tap(btn::Right);
    CHECK_STR(lab(h, 4).bottom, "Disable");
    a.project.setU8(pf::kSidechain, 3);
    a.project.setTrackName(2, "Kick");
    h.tap(btn::Right);
    CHECK_STR(lab(h, 4).bottom, "Kick");   // the track's current name
}

// Spec 16 §B.6: master volume v/100*63.5 - 51.5 dB, 81 = exactly 0 dB.
TEST(mixer_master_volume_in_db) {
    Harness h;
    App& a = *h.app;
    h.tap(btn::Master);
    a.cfg.masterVolume = 81;
    h.tap(btn::Right);
    CHECK_STR(lab(h, 0).bottom, "0.00 dB");
    a.cfg.masterVolume = 0;
    h.tap(btn::Right);
    CHECK_STR(lab(h, 0).bottom, "-51.50 dB");
    a.cfg.masterVolume = 100;
    h.tap(btn::Right);
    CHECK_STR(lab(h, 0).bottom, "12.00 dB");
    a.cfg.masterVolume = 50;
    h.tap(btn::Right);
    CHECK_STR(lab(h, 0).bottom, "-19.75 dB");
}



// Spec 16 §A.12: a pattern export writes <proj>_P<n>_NNNN/master.wav; a
// cancelled one leaves no folder behind.
TEST(export_pattern_folder_with_master_wav) {
    Harness h;
    App& a = *h.app;
    char p[160];
    a.selectPattern(3);
    CHECK(a.exportName(App::ExportKind::Pattern, p, sizeof(p)));
    CHECK_STR(p, "Export/New Project/New Project_P3_0001/master.wav");
    CHECK(a.exportAudio(App::ExportKind::Pattern, p));
    CHECK(h.fs.files.count(p));
    CHECK(h.fs.dirs.count("Export/New Project/New Project_P3_0001"));
    CHECK(a.exportName(App::ExportKind::Pattern, p, sizeof(p)));
    CHECK_STR(p, "Export/New Project/New Project_P3_0002/master.wav");
    h.input.q.push_back(InputEvent{});
    h.input.q.back().type = InputEvent::kButtonDown;
    h.input.q.back().id = btn::F8;
    CHECK(!a.exportAudio(App::ExportKind::Pattern, p));
    CHECK(!h.fs.files.count(p));
    CHECK(!h.fs.dirs.count("Export/New Project/New Project_P3_0002"));
}

namespace {
struct PerfRig {
    FakeNvStore nv;
    std::unique_ptr<PerfPatternMemory> mem{new PerfPatternMemory};
    Harness h{false};
    PerfRig() {
        h.app->setNvStore(&nv);
        h.app->seq.setPerfPatternMemory(mem.get());
        h.app->boot();
    }
    PerformanceScreen& p() { return *static_cast<PerformanceScreen*>(h.app->ui.screen(kModPerformance)); }
};
}  // namespace

// Spec 16 §A.15: Performance stays live on Instrument Params and Master;
// stopped, any other screen reverts at once.
TEST(perf_stays_live_on_instr_params_and_master) {
    PerfRig r;
    App& a = *r.h.app;
    r.h.tap(btn::Performance);
    PerformanceScreen& p = r.p();
    r.h.pad(24 + 1);
    const int row = p.src.row(1);
    CHECK(row != 0);
    r.h.tap(btn::F2);   // FX off for track 2
    CHECK(!p.fxOn[1]);
    r.h.tap(btn::InstrParams);
    CHECK(a.ui.active() == kModInstrument);
    CHECK(a.seq.performanceMode());
    CHECK(p.keepAlive() == PerformanceScreen::KeepAlive::Always);
    r.h.tap(btn::Performance);
    CHECK_EQ(p.src.row(1), row);   // resumed, rows kept
    CHECK(!p.fxOn[1]);
    r.h.tap(btn::Master);
    CHECK(a.seq.performanceMode());
    r.h.tap(btn::Performance);
    CHECK_EQ(p.src.row(1), row);
    r.h.tap(btn::Pattern);   // stopped: immediate revert
    CHECK(!a.seq.performanceMode());
    CHECK(p.keepAlive() == PerformanceScreen::KeepAlive::None);
    r.h.tap(btn::Performance);
    CHECK_EQ(p.src.row(1), 0);   // fresh entry
}

// Spec 16 §A.15: playing, another screen keeps the FX until the pattern ends.
TEST(perf_other_screen_while_playing_reverts_at_pattern_end) {
    PerfRig r;
    App& a = *r.h.app;
    r.h.tap(btn::Performance);
    r.h.tap(btn::Play);
    CHECK(a.seq.isPlaying());
    r.h.tap(btn::Song);
    CHECK(a.ui.active() == kModSong);
    CHECK(a.seq.performanceMode());
    CHECK(r.p().keepAlive() == PerformanceScreen::KeepAlive::UntilPatternEnd);
    const uint32_t e0 = a.patternEndCount();
    for (int i = 0; i < 20000 && a.patternEndCount() == e0; ++i) r.h.audio.pump(1);
    CHECK(a.patternEndCount() != e0);
    r.h.run(20);
    CHECK(!a.seq.performanceMode());
    CHECK(r.p().keepAlive() == PerformanceScreen::KeepAlive::None);
    a.seq.stop();
}

// Audit 12.1 / spec 16 §A.14: highlighted label = FX on, dim text = muted;
// the live cell is drawn on top.
TEST(perf_label_colours_and_live_cell_on_top) {
    PerfRig r;
    App& a = *r.h.app;
    r.h.tap(btn::Performance);
    PerformanceScreen& p = r.p();
    CHECK(a.ui.bar.label[0].highlight);
    CHECK(a.ui.bar.label[0].enabled);
    r.h.tap(btn::F1);   // FX off for track 1
    CHECK(!p.fxOn[0]);
    CHECK(!a.ui.bar.label[0].highlight);
    a.setTrackMute(1, true);
    r.h.tap(btn::Right);
    a.ui.dirty();
    r.h.run(200);
    CHECK(!a.ui.bar.label[1].enabled);
    CHECK(a.ui.bar.label[1].highlight);
    a.setTrackMute(1, false);
    // Live value cell of column 0: the title band (text centre y 53) above
    // the four rows [S snap 021].
    p.liveValid[0] = true;
    p.live[0] = 25;
    a.ui.dirty();
    r.h.run(40);
    bool top = false;
    for (auto& l : r.h.disp.last)
        if (l.find("TEXT") == 0 && l.find("\"+25\"") != std::string::npos && l.find(" 53 ") != std::string::npos) top = true;
    CHECK(top);
}
