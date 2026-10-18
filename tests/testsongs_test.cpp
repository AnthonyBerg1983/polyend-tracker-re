// 2.x test songs (tools/make_testsongs, TESTSONGS.md): every song this build
// supports is generated on a fake card, checked byte by byte against the
// stock format (docs/specs/02), opened through the app's project_io (Open
// job: no load errors, no popup), re-encoded from the loaded model (every
// pattern, instrument and the project marked dirty, autosaved, saved back)
// and compared byte-exact with what the generator wrote; then the song is
// played and must make sound and advance. Every song is a Tracker Mini
// project (docs/specs/21): tracks 13-16 are read back from the raw 0x3030
// pattern files, their names and mutes from project.mt, and the open must
// bring them back.
#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "audio/engines/perc.h"
#include "test.h"
#include "ui_fakes.h"

#include "../tools/make_testsongs/stock_check.cpp"
#include "../tools/make_testsongs/testsongs.cpp"
#include "formats/instrument_pti.h"
#include "ui/gfx.h"

namespace {

using Files = std::map<std::string, std::vector<uint8_t>>;

Files snapshot(const FakeStorage& fs, const std::string& prefix) {
    Files out;
    for (const auto& f : fs.files)
        if (f.first.compare(0, prefix.size(), prefix) == 0) out[f.first] = f.second;
    return out;
}

// Device configuration: custom fonts bound (the default Harness display
// reports ROM fonts only, whose fallback layout is not the device's frame).
std::unique_ptr<Harness> deviceHarness() {
    std::unique_ptr<Harness> h(new Harness(false, ot::kSampleBankBytes / 2));
    h->disp.customFonts = true;
    h->app->boot();
    return h;
}

uint32_t u32le(const std::vector<uint8_t>& b, size_t o) {
    return uint32_t(b[o]) | (uint32_t(b[o + 1]) << 8) | (uint32_t(b[o + 2]) << 16) | (uint32_t(b[o + 3]) << 24);
}

// Every Ui::notify() text since the last clear (load notices).
std::vector<std::string>& notices() {
    static std::vector<std::string> n;
    return n;
}
void onNotice(const char* t) { notices().push_back(t); }

// Tracks 13-16 of every pattern as the generator left them in the store.
using ExtTracks = std::map<int, std::vector<ot::Track>>;
bool emptyTrack(const ot::Track& t) {
    for (const ot::Step& s : t.steps)
        if (s.note != ot::kNoteEmpty || s.instrument || s.fx[0].type || s.fx[1].type) return false;
    return true;
}
ExtTracks extTracks(ot::App& a) {
    ExtTracks out;
    for (int n = 1; n <= ot::kPatternCount; ++n) {
        if (!a.patternPresent(n)) continue;
        std::vector<ot::Track> v(4);
        bool any = false;
        for (int i = 0; i < 4; ++i) {
            a.readStoredTrack(n, ot::kStockTrackCount + i, v[i]);
            any = any || !emptyTrack(v[i]);
        }
        if (any) out[n] = v;
    }
    return out;
}
// Track image (pattern file track: last step, then note, instrument, FX1
// type/value, FX0 type/value per step) == track?
bool blobMatches(const uint8_t* b, const ot::Track& t) {
    if (b[0] != t.lastStep) return false;
    for (int s = 0; s < ot::kStepCount; ++s) {
        const uint8_t* p = b + 1 + 6 * s;
        const ot::Step& st = t.steps[s];
        if (int8_t(p[0]) != st.note || p[1] != st.instrument || p[2] != st.fx[1].type || p[4] != st.fx[0].type)
            return false;
        if ((st.fx[1].type && p[3] != st.fx[1].value) || (st.fx[0].type && p[5] != st.fx[0].value)) return false;
    }
    return true;
}
bool blobEmpty(const uint8_t* b) {
    for (int s = 0; s < ot::kStepCount; ++s) {
        const uint8_t* p = b + 1 + 6 * s;
        if (p[0] != 0xFF || p[1] || p[2] || p[4]) return false;
    }
    return true;
}

// Tracker Mini files on disk (spec 21 §4.1): every pattern file is 0x3030
// with tracks 13-16 at 0x1C + 0x301 * (12..15) holding exactly the generated
// tracks (empty where the generator wrote none); no ot2/ folder.
void checkMiniFiles(const Files& gen, const std::string& dir, const ExtTracks& want) {
    int patterns = 0;
    for (const auto& f : gen) {
        CHECK(f.first.find("/ot2") == std::string::npos);
        int n = 0;
        if (std::sscanf(f.first.c_str() + dir.size(), "/patterns/pattern_%d.mtp", &n) != 1) continue;
        ++patterns;
        CHECK_EQ(f.second.size(), size_t(0x3030));
        if (f.second.size() != 0x3030) continue;
        const auto w = want.find(n);
        for (int i = 0; i < 4; ++i) {
            const uint8_t* b = f.second.data() + 0x1C + 0x301 * size_t(12 + i);
            if (w == want.end()) CHECK(blobEmpty(b));
            else CHECK(blobMatches(b, w->second[i]));
        }
    }
    CHECK(patterns > 0);
}

// Generates `id`, then checks stock format, open, round trip and playback.
void checkSong(ts::SongId id, bool force) {
    const ts::SongInfo* info = ts::findSong(id);
    CHECK(info != nullptr);
    if (!info) return;
    std::printf("  song %d \"%s\"%s\n", int(id), info->name, force ? " (forced)" : "");
    std::fflush(stdout);
    std::unique_ptr<Harness> hp = deviceHarness();
    Harness& h = *hp;
    CHECK(customFontsBound());
    ts::GenOptions opt;
    opt.force = force;
    std::string err;
    const bool made = ts::generateSong(*h.app, h.fs, id, opt, err);
    if (!made) std::printf("    generate: %s\n", err.c_str());
    CHECK(made);
    if (!made) return;

    const std::string dir = std::string(ot::kProjectsDir) + "/" + info->name;
    const Files gen = snapshot(h.fs, dir + "/");
    CHECK(gen.count(dir + "/project.mt") == 1);
    const ExtTracks ext = extTracks(*h.app);
    checkMiniFiles(gen, dir, ext);
    const std::vector<uint8_t>& mt = gen.at(dir + "/project.mt");
    if (id == ts::SongId::SixteenTracks) {
        CHECK(!ext.empty());
        CHECK_EQ(mt[0x742 + 7], 1);   // track 16 starts muted (0x742[7], U13)
        CHECK(std::strcmp(reinterpret_cast<const char*>(&mt[0x623]), "Sub") == 0);    // name 13 (0x603 slot 4)
        CHECK(std::strcmp(reinterpret_cast<const char*>(&mt[0x63B]), "Perc2") == 0);  // name 16
    }
    if (id == ts::SongId::TwelveTracks) CHECK(ext.empty());
    for (int k = 0; k < 3; ++k) CHECK(gen.count(dir + "/" + std::to_string(k) + ".patch") == 1);

    // Tracker Mini format (docs/specs/21), every song strictly.
    const std::string mini = ts::checkStockProject(h.fs, dir.c_str(), true);
    if (!mini.empty()) std::printf("    Mini format check:\n%s", mini.c_str());
    CHECK(mini.empty());

    // Open through project_io from a fresh project: no errors, no notices.
    CHECK(h.app->newProject());
    notices().clear();
    h.app->ui.platform.notified = &onNotice;
    const bool opened = h.app->openProject(ot::kProjectsDir, info->name);
    for (const std::string& n : notices()) std::printf("    load notice: %s\n", n.c_str());
    CHECK(notices().empty());
    CHECK_EQ(h.app->extPoolFullEvents(), 0u);
    CHECK(opened);
    // Tracks 13-16, their names and mutes come back.
    {
        const ExtTracks back = extTracks(*h.app);
        CHECK(back.size() == ext.size());
        for (const auto& e : ext) {
            const auto b = back.find(e.first);
            CHECK(b != back.end());
            if (b == back.end()) continue;
            for (int i = 0; i < 4; ++i)
                for (int st = 0; st < ot::kStepCount; ++st) CHECK(b->second[i].steps[st] == e.second[i].steps[st]);
        }
        for (int t = 12; t < 16; ++t) CHECK_EQ(h.app->trackMuted(t), mt[0x742 + t - 8] != 0);
        char nm[16];
        h.app->project.trackName(12, nm, sizeof nm);
        CHECK(std::strcmp(nm, reinterpret_cast<const char*>(&mt[0x623])) == 0);
        for (int k = 0; k < 3; ++k) CHECK(h.app->synthPatchSource(k) == ot::App::PatchSource::File);
        // No dead slot: every song's Synth 1-3 has an engine in this build
        // (songs 1-4 take the New Project slots: Synth 1 = ACD in 2.0).
        for (int k = 0; k < 3; ++k) CHECK(h.app->engine.synth().slotAvailable(k));
    }
    if (id == ts::SongId::Synths) {   // VAP / ACD / PERC slots with the generator's own patches
        const ot::SynthModel models[3] = {ot::SynthModel::VAP, ot::SynthModel::ACD, ot::SynthModel::PERC};
        const int32_t engines[3] = {ot::kSynthEngineVap, ot::kSynthEngineAcd, ot::kSynthEnginePerc};
        const char* names[3] = {"OT Pad", "OT Acid Bass", "OT Kit"};
        const uint32_t poly[3] = {4, 1, 1};
        for (int k = 0; k < 3; ++k) {
            const ot::SynthSlotRecord r = h.app->synthSlot(k);
            CHECK_EQ(r.model, uint32_t(models[k]));
            CHECK_EQ(r.polyphony, poly[k]);
            CHECK(std::strcmp(r.patchName, names[k]) == 0);
            CHECK_EQ(h.app->synthPatch(k).engine, engines[k]);
            CHECK(h.app->engine.synth().slotAvailable(k));
        }
        // PERC has one tom channel: never two toms on the same tick (tracks 13-16).
        int toms = 0;
        for (const auto& e : ext)
            for (int st = 0; st < ot::kStepCount; ++st) {
                int n = 0;
                for (int i = 0; i < 4; ++i) {
                    const ot::Step& x = e.second[i].steps[st];
                    if (x.note >= 0 && x.instrument == 64 + 2 &&
                        ot::audio::perc::drumForNote(uint8_t(x.note)) == ot::audio::perc::kTom)
                        ++n;
                }
                toms += n;
                CHECK(n <= 1);
            }
        CHECK(toms > 0);
    }
    const bool bg = int(id) >= int(ts::SongId::BgTitle);
    if (bg) {
        // BLAST GRID remixes (bgsongs.inc): Synth 1 VAP, 2 ACD, 3 PERC; the
        // notes per track match the source parser's counts (bg_data.h); the
        // synth parts fit the default voice budget (Config Synth voices 4).
        const ts::bg::Song& src = *ts::bgr::variant(id)->src;
        const ot::SynthModel models[3] = {ot::SynthModel::VAP, ot::SynthModel::ACD, ot::SynthModel::PERC};
        uint32_t polySum = 0;
        for (int k = 0; k < 3; ++k) {
            const ot::SynthSlotRecord r = h.app->synthSlot(k);
            CHECK_EQ(r.model, uint32_t(models[k]));
            polySum += r.polyphony;
        }
        CHECK(polySum <= ot::Config::kDefaultSynthVoices);
        const std::string counts = ts::bgr::checkNoteCounts(*h.app, id);
        if (!counts.empty()) std::printf("    note counts differ:\n%s", counts.c_str());
        CHECK(counts.empty());
        const int held = ts::bgr::peakSynthVoices(*h.app);
        std::printf("    tempo %.1f, swing %d, peak synth voices held %d (budget %d)\n", double(h.app->project.tempo()),
                    ts::bgr::swingFx(src), held, int(ot::Config::kDefaultSynthVoices));
        CHECK(held <= int(ot::Config::kDefaultSynthVoices));
        CHECK(std::fabs(h.app->project.tempo() - ts::bgr::bpm(src)) < 0.01f);
    }
    CHECK_EQ(h.app->lastJobErrors(), 0);
    CHECK(!h.app->ui.askOpen());
    CHECK(std::strcmp(h.app->project.name, info->name) == 0);
    int ptiFiles = 0, present = 0;
    for (const auto& f : gen)
        if (f.first.size() > 4 && f.first.compare(f.first.size() - 4, 4, ".pti") == 0) ++ptiFiles;
    for (int i = 0; i < ot::kSampleInstrumentCount; ++i) present += h.app->instrPresent[i] ? 1 : 0;
    CHECK_EQ(present, ptiFiles);

    // Re-encode everything from the loaded model, then Save.
    h.app->markProjectDirty();
    h.app->markNamesDirty();
    for (int n = 1; n <= ot::kPatternCount; ++n)
        if (h.app->patternPresent(n)) h.app->markPatternDirty(n);
    for (int i = 0; i < ot::kSampleInstrumentCount; ++i) {
        if (!h.app->instrPresent[i]) continue;
        // Stereo .pti files load as left-only before M2 (spec 02 §6.3), so
        // their sample payload is not rewritten from memory.
        // TODO(M2): rewrite stereo payloads too once stereo stays in memory.
        bool monoFile = true;
        for (const auto& f : gen) {
            char pre[16];
            std::snprintf(pre, sizeof(pre), "/%d ", i + 1);
            if (f.first.find(dir + "/instruments" + pre) == 0)
                monoFile = f.second.size() == ot::ptiFileSize(u32le(f.second, 0x10 + 0x2C), false);
        }
        h.app->markInstrumentDirty(i, monoFile);
    }
    h.app->autosave();
    CHECK(h.app->saveProject());
    const Files saved = snapshot(h.fs, dir + "/");
    CHECK_EQ(saved.size(), gen.size());
    int diffs = 0;
    for (const auto& f : gen) {
        auto it = saved.find(f.first);
        if (it == saved.end() || it->second != f.second) {
            if (++diffs <= 5) std::printf("    round trip differs: %s\n", f.first.c_str());
        }
    }
    CHECK_EQ(diffs, 0);

    // Play the song: sound within the first bars, and it moves on to the
    // second song slot after the first 64-step pattern (~7.7 s at 124 BPM;
    // 128-step patterns ~15.5 s).
    h.app->seq.playSong(0);
    CHECK(h.app->seq.isPlaying());
    uint32_t acc = 0;
    int peak = 0, voices = 0;
    const uint32_t ms = id == ts::SongId::LoadStress ? 16500 : 8500;
    for (uint32_t t = 0; t < ms; t += 10) {
        acc += 10u * 44100u;
        int blocks = 0;
        while (acc >= 128000u) {
            acc -= 128000u;
            ++blocks;
        }
        const int p = h.audio.pump(blocks);
        peak = p > peak ? p : peak;
        const int sv = h.app->engine.synth().sounding();
        voices = sv > voices ? sv : voices;
        h.run(10);
    }
    CHECK(peak > 2000);
    if (bg) {
        std::printf("    synth voices sounding at most %d, steals %u\n", voices, unsigned(h.app->engine.synth().steals()));
        CHECK(voices <= int(ot::Config::kDefaultSynthVoices));
        CHECK(voices >= 2);
        // The groove: title 7+6 ticks, world 4 8+5 -> Swing FX 58 / 73.
        const int sw = ts::bgr::swingFx(*ts::bgr::variant(id)->src);
        CHECK_EQ(h.app->seq.swingActive(), sw != 0);
        if (sw) CHECK_EQ(int(h.app->seq.swingValue()), sw);
    }
    if (id == ts::SongId::Synths) {
        // The chords ask for more voices than the pool has: voices were stolen.
        std::printf("    synth steals %u\n", unsigned(h.app->engine.synth().steals()));
        CHECK(h.app->engine.synth().steals() > 0);
    }
    CHECK(h.app->seq.isPlaying());
    CHECK(h.app->seq.songPosition() >= 1);
}

}  // namespace

TEST(testsongs_table) {
    // Songs that need no unbuilt milestone are always offered.
    CHECK(ts::findSong(ts::SongId::TwelveTracks)->available);
    CHECK(ts::findSong(ts::SongId::LoadStress)->available);
    CHECK_EQ(ts::findSong(ts::SongId::SixteenTracks)->available, ts::kHas16Tracks);
    CHECK_EQ(ts::findSong(ts::SongId::Stereo)->available, ts::kHasStereo);
    CHECK_EQ(ts::findSong(ts::SongId::Synths)->available, ts::kHasSynths && ts::kHas16Tracks);
    CHECK(ts::kHasSynths);
    for (int i = 5; i < ts::kSongCount; ++i) CHECK_EQ(ts::songInfo(i).available, ts::kHasSynths && ts::kHas16Tracks);
    for (int i = 0; i < ts::kSongCount; ++i) CHECK(std::strlen(ts::songInfo(i).name) <= ot::mt::kNameMax);
}

TEST(testsongs_available_songs_open_round_trip_and_play) {
    for (int i = 0; i < ts::kSongCount; ++i)
        if (ts::songInfo(i).available) checkSong(ts::songInfo(i).id, false);
}

// The stereo song's files are stock-format already (planar stereo .pti,
// spec 02 §6.3), so it is exercised before M2 too: it must open cleanly
// (stock plays the left channel) and keep its stereo payloads on save.
TEST(testsongs_stereo_song_files_before_m2) {
    if (ts::kHasStereo) return;   // covered above
    checkSong(ts::SongId::Stereo, true);
    std::unique_ptr<Harness> hp = deviceHarness();
    Harness& h = *hp;
    ts::GenOptions opt;
    opt.force = true;
    std::string err;
    CHECK(ts::generateSong(*h.app, h.fs, ts::SongId::Stereo, opt, err));
    int stereo = 0, mono = 0;
    for (const auto& f : h.fs.files) {
        if (f.first.find("Projects/OT Test 04 - Stereo/instruments/") != 0) continue;
        const uint32_t len = u32le(f.second, 0x10 + 0x2C);
        if (f.second.size() == ot::ptiFileSize(len, true)) ++stereo;
        else if (f.second.size() == ot::ptiFileSize(len, false)) ++mono;
    }
    CHECK_EQ(stereo, 6);
    CHECK_EQ(mono, 6);
}

// ---- levels and arpeggios (rendered audio) ------------------------------------

namespace {

// int16 samples of a WAV file in the fake card (the "data" chunk).
std::vector<int16_t> wavSamples(const std::vector<uint8_t>& f) {
    std::vector<int16_t> out;
    for (size_t o = 12; o + 8 <= f.size();) {
        const uint32_t len = u32le(f, o + 4);
        if (!std::memcmp(&f[o], "data", 4)) {
            const size_t n = std::min<size_t>(len, f.size() - o - 8) / 2;
            out.resize(n);
            std::memcpy(out.data(), &f[o + 8], n * 2);
            return out;
        }
        o += 8 + len + (len & 1);
    }
    return out;
}

}  // namespace

// Songs 1 and 5 and two BG remixes rendered through the
// App's own stem export (spec 04 §6.1): no stem clips (sample tracks at the
// voice AMP clamp, synth slot buses at int16), and the sum of the stems --
// what reaches the master sum before its clamp and the limiter (spec 07
// §5.4) -- stays under full scale but for isolated transients.
TEST(testsongs_rendered_levels_do_not_clip) {
    const ts::SongId ids[] = {ts::SongId::TwelveTracks, ts::SongId::Synths, ts::SongId::BgTitle,
                              ts::SongId::BgFinalBoss};
    for (ts::SongId id : ids) {
        const ts::SongInfo* info = ts::findSong(id);
        if (!info || !info->available) continue;
        std::unique_ptr<Harness> hp = deviceHarness();
        Harness& h = *hp;
        ts::GenOptions opt;
        std::string err;
        CHECK(ts::generateSong(*h.app, h.fs, id, opt, err));
        h.fs.mkdir("Export");
        CHECK(h.app->exportAudio(ot::App::ExportKind::Song, "Export/levels", nullptr, true));
        const char* const names[] = {"track1", "track2", "track3", "track4",  "track5",  "track6", "track7",
                                     "track8", "synth_1", "synth_2", "synth_3", "reverb", "delay"};
        std::vector<int32_t> sum;
        long stemClips = 0;
        int found = 0;
        for (const char* n : names) {
            const auto f = h.fs.files.find(std::string("Export/levels/") + n + ".wav");
            if (f == h.fs.files.end()) continue;
            ++found;
            const std::vector<int16_t> s = wavSamples(f->second);
            if (sum.size() < s.size()) sum.resize(s.size(), 0);
            long c = 0;
            for (size_t i = 0; i < s.size(); ++i) {
                sum[i] += s[i];
                c += s[i] >= 32767 || s[i] <= -32767;
            }
            if (c) std::printf("    %s: stem %s clips %ld samples\n", info->name, n, c);
            stemClips += c;
        }
        long over = 0;
        int32_t peak = 0;
        for (int32_t v : sum) {
            over += v > 32767 || v < -32767;
            peak = std::max(peak, v < 0 ? -v : v);
        }
        std::printf("    %s: stem sum peak %+.1f dBFS, %ld of %zu samples over full scale\n", info->name,
                    20.0 * std::log10(double(peak) / 32768.0), over, sum.size());
        CHECK_EQ(found, 13);
        CHECK_EQ(stemClips, 0L);
        CHECK(sum.size() > 44100u * 2u * 10u);   // the whole song was rendered
        CHECK(double(over) < 1e-4 * double(sum.size()));
    }
}

// The BG arpeggios play chip-style (pitch steps inside one sustained note):
// each (chord, root) is its own sample at the game's arp rate, the step a
// plain note. No sample track carries the Arp FX (which re-triggers the
// sample on every arp note, spec 03 §3.4: the "machine gun"), and every
// arpeggio instrument is tuned so its root note plays the sample at its own
// rate.
TEST(testsongs_bg_arpeggios_are_sustained_samples) {
    for (int i = 0; i < ts::kSongCount; ++i) {
        const ts::SongInfo& info = ts::songInfo(i);
        if (int(info.id) < int(ts::SongId::BgTitle) || !info.available) continue;
        std::unique_ptr<Harness> hp = deviceHarness();
        Harness& h = *hp;
        ts::GenOptions opt;
        std::string err;
        CHECK(ts::generateSong(*h.app, h.fs, info.id, opt, err));
        int arpFx = 0, arpNotes = 0;
        for (int n = 1; n <= ot::kPatternCount; ++n) {
            if (!h.app->patternPresent(n)) continue;
            for (int t = 0; t < ot::kStockTrackCount; ++t) {
                ot::Track tr;
                h.app->readStoredTrack(n, t, tr);
                for (const ot::Step& st : tr.steps) {
                    for (const auto& fx : st.fx) arpFx += fx.type == uint8_t(ot::FxId::Arp);
                    if (st.note < 0 || st.instrument >= ot::kSampleInstrumentCount) continue;
                    const ot::Instrument& in = h.app->instruments[st.instrument];
                    if (std::strstr(in.name, " arp ") || std::strstr(in.name, " stab ")) {
                        ++arpNotes;
                        CHECK_EQ(int(st.note) + int(in.tune), 60);
                    }
                }
            }
        }
        CHECK_EQ(arpFx, 0);
        if (info.id != ts::SongId::BgWorld4) CHECK(arpNotes > 0);   // World 4 has no arpeggio
    }
}
