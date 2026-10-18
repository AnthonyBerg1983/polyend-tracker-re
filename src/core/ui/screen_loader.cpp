// Sample Loader (spec 06 §5).
#include <cmath>
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "formats/wav.h"
#include "ui/screens.h"

namespace ot {

namespace {
int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

bool hasExt(const char* s, const char* ext) {
    const size_t a = std::strlen(s), b = std::strlen(ext);
    if (b > a) return false;
    for (size_t i = 0; i < b; ++i) {
        char x = s[a - b + i];
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (x != ext[i]) return false;
    }
    return true;
}

struct ListCtx {
    LoaderScreen* self;
    int count;
};
bool listFn(const IStorage::DirEntry& e, void* ctx) {
    auto* c = static_cast<ListCtx*>(ctx);
    if (e.name[0] == '.' || c->count >= LoaderScreen::kMaxEntries) return c->count < LoaderScreen::kMaxEntries;
    if (!e.isDir && !hasExt(e.name, ".wav") && !hasExt(e.name, ".pti")) return true;
    if (std::strlen(e.name) >= sizeof(c->self->entries[0])) return true;
    std::snprintf(c->self->entries[c->count], sizeof(c->self->entries[0]), "%s", e.name);
    c->self->isDir[c->count] = e.isDir;
    ++c->count;
    return true;
}

// spec 06 §5.2: a .wav is listed only when its header passes the WAV check
// (RIFF/WAVE, 1-2 channels, PCM 8/16/24/32 or float 32; stock 0x70bb0).
bool wavHeaderOk(IStorage& fs, const char* path) {
    IStorage::Handle h = fs.open(path, IStorage::kRead);
    if (h < 0) return false;
    StorageReader r(fs, h);
    WavInfo info;
    const bool ok = wavParseHeader(r, info) && wavIsSupported(info);
    fs.close(h);
    return ok;
}

int cmpNoCase(const char* a, const char* b) {
    for (;; ++a, ++b) {
        char x = *a, y = *b;
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
        if (x != y || !x) return x - y;
    }
}
}  // namespace

void LoaderScreen::entryPath(int i, char* out, int cap) const {
    if (dir[0]) std::snprintf(out, size_t(cap), "%s/%s", dir, entries[i]);
    else std::snprintf(out, size_t(cap), "%s", entries[i]);
}

void LoaderScreen::list() {
    // Folders first, then files, alphabetical; at most 101 names (spec 06 §5.2).
    ListCtx c{this, 0};
    if (depth_ > 0) {
        std::snprintf(entries[0], sizeof(entries[0]), "..");
        isDir[0] = true;
        c.count = 1;
    }
    const int first = c.count;
    app_.fs().listDir(dir, listFn, &c);
    count = c.count;
    // Insertion sort (small lists).
    for (int i = first + 1; i < count; ++i) {
        char tmp[40];
        bool td = isDir[i];
        std::memcpy(tmp, entries[i], sizeof(tmp));
        int j = i - 1;
        while (j >= first && ((isDir[j] == td && cmpNoCase(entries[j], tmp) > 0) || (!isDir[j] && td))) {
            std::memcpy(entries[j + 1], entries[j], sizeof(tmp));
            isDir[j + 1] = isDir[j];
            --j;
        }
        std::memcpy(entries[j + 1], tmp, sizeof(tmp));
        isDir[j + 1] = td;
    }
    // Drop .wav files whose header fails the check.
    int w = first;
    char path[256];
    for (int i = first; i < count; ++i) {
        if (!isDir[i] && hasExt(entries[i], ".wav")) {
            entryPath(i, path, sizeof(path));
            if (!wavHeaderOk(app_.fs(), path)) continue;
        }
        if (w != i) {
            std::memcpy(entries[w], entries[i], sizeof(entries[0]));
            isDir[w] = isDir[i];
        }
        ++w;
    }
    count = w;
    sdCursor = clampi(sdCursor, 0, count > 0 ? count - 1 : 0);
    std::memset(marked_, 0, sizeof(marked_));
    memoryProjection();
}

void LoaderScreen::start(int opt) {
    trim = false;
    if (opt > 0) {
        insCursor = clampi(opt - 1, 0, 47);
        focus = 1;
        sdCursor = 0;
    } else {
        const int ci = app_.currentInstrument();
        insCursor = ci > 47 ? 0 : ci;
    }
    list();
    refreshBar();
}

void LoaderScreen::stop() {
    trimPlaying_ = false;
    app_.previewAllOff();
    previewing_ = false;
    trim = false;
}

void LoaderScreen::selectionRange(int* first, int* last) const { listRange(focus != 1, first, last); }

void LoaderScreen::listRange(bool sd, int* first, int* last) const {
    const uint8_t* m = sd ? marked_ : insMarked_;
    const int n = sd ? count : 48;
    int f = -1, l = -1;
    for (int i = 0; i < n; ++i)
        if (m[i]) {
            if (f < 0) f = i;
            l = i;
        }
    if (f < 0) f = l = sd ? sdCursor : insCursor;
    *first = f;
    *last = l;
}

void LoaderScreen::memoryProjection() {
    // spec 06 §5.4 memory bar.
    App& a = app_;
    uint64_t used = a.bank.usedBytes();
    over_ = false;
    projected_ = uint32_t(used);
    // The projection always concerns the SD selection loaded at the
    // instrument cursor, whichever list has the focus.
    if (count == 0 || isDir[sdCursor]) return;
    int f, l;
    listRange(true, &f, &l);
    char path[256];
    for (int i = f; i <= l; ++i) {
        if (isDir[i]) continue;
        entryPath(i, path, sizeof(path));
        used += a.sampleBytesNeeded(path);
        const int slot = insCursor + (i - f);
        if (slot < kSampleInstrumentCount) used -= a.bank.samples(slot) * 2;
    }
    projected_ = uint32_t(used);
    over_ = used > a.bank.capacityBytes();
}

void LoaderScreen::refreshBar() {
    BottomBar& b = ui_.bar;
    App& a = app_;
    b.clear();
    char v[24];
    if (trim) {
        if (trimPlaying_) std::snprintf(v, sizeof(v), "%.3fs", double(trimElapsed_) / 1000.0);
        else std::snprintf(v, sizeof(v), "-");
        b.set(0, "Preview", v);
        std::snprintf(v, sizeof(v), "%.3fs", double(trimStart) / 65535.0 * double(trimLen_ / 44100.0));
        b.set(1, "Start", v);
        std::snprintf(v, sizeof(v), "%.3fs", double(trimEnd) / 65535.0 * double(trimLen_ / 44100.0));
        b.set(2, "End", v);
        b.set(5, "Cancel");
        b.set(6, "Import", "low quality", importAllowed(true));
        b.set(7, "Import", "Normal", importAllowed(false));
        b.highlight(trimSel ? 2 : 1, true);
        return;
    }
    const bool onDir = focus == 0 && count > 0 && isDir[sdCursor];
    const bool emptySlot = focus == 1 && !a.instrPresent[insCursor];
    b.set(0, onDir ? "Enter" : "Add", nullptr, focus <= 1);
    b.set(1, "Add next", nullptr, !onDir);
    b.set(2, "Preview", nullptr, !onDir && !emptySlot);
    b.set(3, "Rename", nullptr, focus == 1 && !emptySlot);
    int f, l;
    listRange(true, &f, &l);
    b.set(4, "Import", nullptr, focus <= 1 && count > 0 && f == l && !isDir[sdCursor] && hasExt(entries[sdCursor], ".wav"));
    b.set(5, "Delete", "unused", focus == 1);
    const float g = audio::trkGain(a.cfg.previewVolume);
    if (g <= 0) std::snprintf(v, sizeof(v), "-inf dB");
    else std::snprintf(v, sizeof(v), "%.2f dB", double(20.0f * std::log10(g)));
    b.set(6, "Preview Vol", v);
    std::snprintf(v, sizeof(v), "%u%%", unsigned(uint64_t(projected_) * 100 / (a.bank.capacityBytes() ? a.bank.capacityBytes() : 1)));
    b.set(7, "Memory", v);
    b.highlight(6, focus == 2);
}

bool LoaderScreen::importAllowed(bool lowQuality) const {
    // spec 06 §5.5 trim gating (bytes): n = selected samples, free = capacity
    // - used + 2 * samples of the slot being replaced.
    const App& a = app_;
    const int64_t n = int64_t(uint32_t(trimEnd - trimStart)) * int64_t(trimLen_) / 65535;
    const int64_t fr = int64_t(a.bank.capacityBytes()) - int64_t(a.bank.usedBytes()) +
                       2 * int64_t(a.bank.samples(insCursor));
    return lowQuality ? n < fr - 10000 : 2 * n < fr - 10000;
}

void LoaderScreen::openImport() {
    // §5.5 step 1: F5 with a single non-.pti file; an occupied target slot
    // asks first (F7 Cancel, F8 Yes).
    int f, l;
    listRange(true, &f, &l);
    if (focus > 1 || count == 0 || f != l || isDir[sdCursor] || !hasExt(entries[sdCursor], ".wav")) return;
    if (app_.instrPresent[insCursor]) {
        ui_.ask("Replace instrument?", "The instrument will be replaced", nullptr, "Cancel", "Yes",
                [](void* ctx, int key) {
                    auto* self = static_cast<LoaderScreen*>(ctx);
                    if (key == btn::F8) self->startTrim();
                    self->refreshBar();
                },
                this);
        return;
    }
    startTrim();
}

void LoaderScreen::startTrim() {
    // §5.5 step 2: read the whole file (here: decoded into the free tail of
    // the sample bank), then the trim state.
    App& a = app_;
    char path[256];
    entryPath(sdCursor, path, sizeof(path));
    WavImportResult res;
    WavImportOptions opt;
    if (wavImportFile(a.fs(), path, opt, a.bank.tail(), uint32_t(a.bank.freeSamples()), res) != FmtStatus::Ok) {
        ui_.notify("Cannot read file");
        return;
    }
    trimLen_ = res.frames;
    trimStart = 0;
    trimEnd = 65535;
    trimSel = 0;
    trimPlaying_ = false;
    trim = true;
    refreshBar();
}

void LoaderScreen::enter() {
    if (count == 0 || !isDir[sdCursor]) return;
    if (std::strcmp(entries[sdCursor], "..") == 0) {
        char* s = std::strrchr(dir, '/');
        if (s) *s = 0;
        else dir[0] = 0;
        if (depth_ > 0) --depth_;
        list();
        sdCursor = clampi(cursorHist_[depth_], 0, count - 1);
    } else if (depth_ < kMaxDepth - 1) {
        cursorHist_[depth_++] = sdCursor;
        char nd[200];
        entryPath(sdCursor, nd, sizeof(nd));
        std::snprintf(dir, sizeof(dir), "%s", nd);
        sdCursor = 0;
        list();
    }
}

void LoaderScreen::doAdd(bool next) {
    App& a = app_;
    int f, l;
    listRange(true, &f, &l);
    int n = 0;
    char path[256];
    for (int i = f; i <= l; ++i) {
        if (isDir[i]) continue;
        entryPath(i, path, sizeof(path));
        const int slot = insCursor + n;
        if (slot >= kSampleInstrumentCount) break;
        ui_.progress("Loading", (i - f) * 100 / (l - f + 1));
        if (!a.importSample(path, slot)) ui_.notify("Import failed");
        ++n;
    }
    ui_.progressDone();
    if (next) insCursor = clampi(insCursor + n, 0, 47);
    a.setCurrentInstrument(insCursor);
    memoryProjection();
}

void LoaderScreen::add(bool next) {
    // spec 06 §5.4 Add / Add next.
    App& a = app_;
    if (focus == 0 && count > 0 && isDir[sdCursor]) {
        enter();
        return;
    }
    // Add works from the SD list and from the instrument list (F1 is enabled
    // with either focus); it always loads the SD selection.
    if (focus > 1 || count == 0) return;
    int f, l;
    listRange(true, &f, &l);
    int files = 0;
    for (int i = f; i <= l; ++i) files += isDir[i] ? 0 : 1;
    if (files == 0) return;
    if (insCursor + files - 1 > 47) {
        ui_.notify("Too many samples selected");
        return;
    }
    memoryProjection();
    if (over_) return;
    bool occupied = false;
    for (int i = 0; i < files; ++i) occupied = occupied || a.instrPresent[insCursor + i];
    addNext_ = next;
    if (occupied) {
        ui_.ask("Replace instrument(s)?", "Existing instruments will be replaced", nullptr, "Cancel", "Yes",
                [](void* ctx, int key) {
                    auto* self = static_cast<LoaderScreen*>(ctx);
                    if (key == btn::F8) self->doAdd(self->addNext_);
                    self->refreshBar();
                },
                this);
        return;
    }
    doAdd(next);
}

void LoaderScreen::moveCursor(int d) {
    const bool sh = ui_.shift();
    if (focus == 0) {
        if (count == 0) return;
        const int old = sdCursor;
        int nc = clampi(sdCursor + d, 0, count - 1);
        if (sh) {
            // Multi-select: mark rows passed, folders stop the range (spec 06 §5.3).
            int marks = 0;
            for (int i = 0; i < count; ++i) marks += marked_[i];
            if (!marks && !isDir[old]) marked_[old] = 1;
            const int step = nc > old ? 1 : -1;
            for (int i = old + step; nc != old && i != nc + step; i += step) {
                if (isDir[i]) {
                    nc = i - step;
                    break;
                }
                if (marked_[i] && marked_[i - step]) marked_[i - step] = 0;   // moving back un-marks
                else if (marks < 48) marked_[i] = 1;
            }
        } else {
            std::memset(marked_, 0, sizeof(marked_));
        }
        sdCursor = nc;
        pendingProjAt_ = ui_.now() + 300;
    } else if (focus == 1) {
        const int old = insCursor;
        insCursor = clampi(insCursor + d, 0, 47);
        if (sh) {
            int marks = 0;
            for (int i = 0; i < 48; ++i) marks += insMarked_[i];
            if (!marks) insMarked_[old] = 1;
            const int step = insCursor > old ? 1 : -1;
            for (int i = old + step; insCursor != old && i != insCursor + step; i += step) {
                if (insMarked_[i] && insMarked_[i - step]) insMarked_[i - step] = 0;   // moving back un-marks
                else if (marks < 48) insMarked_[i] = 1;
            }
        } else {
            std::memset(insMarked_, 0, sizeof(insMarked_));
        }
        app_.setCurrentInstrument(insCursor);
        if (previewing_ && app_.instrPresent[insCursor]) app_.previewOn(App::kAuditionLoader, uint8_t(insCursor), 60, 127);
    } else {
        app_.cfg.previewVolume = uint8_t(clampi(app_.cfg.previewVolume + d, 0, 100));
        app_.markConfigDirty();
    }
}

void LoaderScreen::onButton(uint8_t id, uint8_t state) {
    App& a = app_;
    if (trim) {
        if (id == btn::F1 || id == btn::EncPush) {
            if (state == kPress && !trimPlaying_) {
                const uint32_t s0 = uint32_t(uint64_t(trimStart) * trimLen_ / 65535);
                const uint32_t s1 = uint32_t(uint64_t(trimEnd) * trimLen_ / 65535);
                a.previewFile(a.bank.tail() + s0, s1 > s0 ? s1 - s0 : 0, false);
                trimPlaying_ = true;
                trimPlayAt_ = now();
                trimElapsed_ = 0;
            } else if (state == kRelease) {
                a.previewFile(nullptr, 0, false);
                trimPlaying_ = false;
            }
        } else if (state == kPress) {
            switch (id) {
                case btn::F2: trimSel = 0; break;
                case btn::F3: trimSel = 1; break;
                case btn::F6:
                    trim = false;
                    trimPlaying_ = false;
                    a.previewAllOff();
                    break;
                case btn::F7:
                case btn::F8: {
                    if (!importAllowed(id == btn::F7)) break;   // disabled key does nothing
                    a.previewAllOff();
                    trimPlaying_ = false;
                    char path[256];
                    entryPath(sdCursor, path, sizeof(path));
                    trim = false;
                    ui_.progress("Importing", 50);
                    if (!a.importSample(path, insCursor, trimStart, trimEnd, id == btn::F7)) ui_.notify("Import failed");
                    ui_.progressDone();
                    memoryProjection();
                    break;
                }
                default: break;
            }
        }
        refreshBar();
        return;
    }
    if (ui_.kb.isOpen()) return;
    switch (id) {
        case btn::F1:
        case btn::Insert:
        case btn::EncPush:
            if (state == kPress) add(false);
            break;
        case btn::F2:
            if (state == kPress) add(true);
            break;
        case btn::F3:
            if (state == kPress) {
                const int fcs = focus == 2 ? prevFocus_ : focus;
                if (fcs == 1 && a.instrPresent[insCursor]) {
                    // One track voice, fixed id 0x60 and note (spec 07 Q7).
                    a.previewOn(App::kAuditionLoader, uint8_t(insCursor), 60, 127);
                    previewing_ = true;
                } else if (fcs == 0 && count > 0 && !isDir[sdCursor]) {
                    // Decode into the free tail of the sample bank and play it.
                    char path[256];
                    entryPath(sdCursor, path, sizeof(path));
                    WavImportResult res;
                    WavImportOptions opt;
                    opt.stereo = true;   // the SD streamer plays stereo files in stereo (spec 07 Q1)
                    if (hasExt(path, ".wav") &&
                        wavImportFile(a.fs(), path, opt, a.bank.tail(), uint32_t(a.bank.freeSamples()), res) == FmtStatus::Ok)
                        a.previewFile(a.bank.tail(), res.frames, res.stereo);
                    previewing_ = true;
                }
            } else if (state == kRelease) {
                a.previewAllOff();
                previewing_ = false;
            }
            break;
        case btn::F4:
            if (state == kPress && focus == 1 && a.instrPresent[insCursor]) {
                ui_.kb.open("Rename instrument", a.instruments[insCursor].name, kInstrumentNameLen, "Rename",
                            [](void* ctx, bool ok, const char* text) {
                                auto* self = static_cast<LoaderScreen*>(ctx);
                                bool blank = true;
                                for (const char* p = text; *p; ++p) blank = blank && *p == ' ';
                                if (!ok) return;
                                if (blank) {
                                    self->ui_.notify("Name cannot be empty");
                                    return;
                                }
                                self->app_.renameInstrument(self->insCursor, text);
                            },
                            this);
            }
            break;
        case btn::F5:
            if (state == kPress) openImport();
            break;
        case btn::F6:
            if (state == kPress && focus == 1) {
                ui_.ask("Delete unused instruments?", "Instruments not used in any pattern", nullptr, "Cancel", "Yes",
                        [](void* ctx, int key) {
                            if (key != btn::F8) return;
                            auto* self = static_cast<LoaderScreen*>(ctx);
                            App& ap = self->app_;
                            bool used[kSampleInstrumentCount] = {};
                            ap.commitCurrentPattern();
                            for (int n = 1; n <= kPatternCount; ++n) {
                                if (!ap.patternPresent(n)) continue;
                                // All 16 tracks (2.x): tracks 12..15 only when they hold a pool block.
                                for (int t = 0; t < kTrackCount; ++t) {
                                    const Track* tr = ap.findStoredTrack(n, t);
                                    if (!tr) continue;
                                    for (const auto& s : tr->steps)
                                        if (s.note >= 0 && s.instrument < kSampleInstrumentCount) used[s.instrument] = true;
                                }
                            }
                            int removed = 0;
                            for (int i = 0; i < kSampleInstrumentCount; ++i)
                                if (ap.instrPresent[i] && !used[i]) {
                                    ap.deleteInstrument(i);
                                    ++removed;
                                }
                            char msg[32];
                            std::snprintf(msg, sizeof(msg), "%d deleted", removed);
                            self->ui_.notify(msg);
                            self->refreshBar();
                        },
                        this);
            }
            break;
        case btn::F7:
            // §5.3: press focuses the volume bar; after a hold tick the release
            // returns to the SD list (momentary use).
            if (state == kPress) {
                if (focus != 2) prevFocus_ = focus;
                focus = 2;
                f7Hold_ = false;
            } else if (state == kHold) {
                f7Hold_ = true;
            } else if (state == kRelease) {
                if (f7Hold_ && prevFocus_ == 0) focus = 0;
                f7Hold_ = false;
            }
            break;
        case btn::Delete:
            if (state != kPress) break;
            if (focus == 2) {
                a.cfg.previewVolume = 86;
                a.markConfigDirty();
            } else if (focus == 1 && [&] {
                           int f, l, n = 0;
                           listRange(false, &f, &l);
                           for (int i = f; i <= l; ++i) n += a.instrPresent[i] ? 1 : 0;
                           return n > 0;
                       }()) {
                ui_.ask("Delete instrument(s)?", "Selected instruments will be deleted", nullptr, "Cancel", "Yes",
                        [](void* ctx, int key) {
                            if (key != btn::F8) return;
                            auto* self = static_cast<LoaderScreen*>(ctx);
                            int f, l;
                            self->selectionRange(&f, &l);
                            for (int i = f; i <= l; ++i) self->app_.deleteInstrument(i);
                            self->memoryProjection();
                            self->refreshBar();
                        },
                        this);
            }
            break;
        case btn::Copy:
            if (state != kPress || focus != 1) break;
            if (!ui_.shift()) {
                int f, l;
                selectionRange(&f, &l);
                copySrc_ = f;
                copyCount_ = l - f + 1;
                ui_.notify("Copied");
            } else if (copySrc_ >= 0) {
                int ok = 0;
                for (int i = 0; i < copyCount_ && insCursor + i < kSampleInstrumentCount; ++i)
                    if (a.instrPresent[copySrc_ + i] && a.copyInstrument(copySrc_ + i, insCursor + i)) ++ok;
                if (ok < copyCount_) ui_.notify("Not all instruments copied");
            }
            break;
        case btn::Left:
            if (state != kPress) break;
            a.previewAllOff();
            if (focus == 1) {
                focus = 0;
                std::memset(insMarked_, 0, sizeof(insMarked_));
            } else if (focus == 0) {
                sdCursor = 0;
                std::memset(marked_, 0, sizeof(marked_));
            } else {
                focus = 1;
            }
            memoryProjection();
            break;
        case btn::Right:
            if (state != kPress) break;
            if (focus < 2) {
                prevFocus_ = focus;
                ++focus;
            }
            memoryProjection();
            break;
        case btn::Up:
        case btn::Down:
            // Lists: Up = cursor - 1; volume bar: Up = volume + 1 (§5.3).
            if (state == kPress) moveCursor((id == btn::Up) == (focus == 2) ? 1 : -1);
            break;
        case btn::Shift:
            break;
        default:
            Screen::onButton(id, state);
            break;
    }
    refreshBar();
}

void LoaderScreen::onPad(uint8_t pad, uint8_t state) {
    (void)pad;
    (void)state;   // pads are only used by the keyboard here (spec 06 §5.4)
}

void LoaderScreen::onEncoder(int d) {
    if (trim) {
        // 109 units per detent; Start <= End - 2 (spec 06 §5.5).
        if (trimSel == 0) trimStart = uint16_t(clampi(int(trimStart) + d * 109, 0, int(trimEnd) - 2));
        else trimEnd = uint16_t(clampi(int(trimEnd) + d * 109, int(trimStart) + 2, 65535));
        refreshBar();
        return;
    }
    if (focus == 0) d = clampi(d, -5, 5);
    moveCursor(d);
    refreshBar();
}

void LoaderScreen::update(uint32_t now) {
    if (trim && trimPlaying_ && now - trimPlayAt_ >= trimElapsed_ + 11) {
        // §5.5: the playhead advances every >= 11 ms; playback stops at End.
        trimElapsed_ = now - trimPlayAt_;
        const uint32_t s0 = uint32_t(uint64_t(trimStart) * trimLen_ / 65535);
        const uint32_t s1 = uint32_t(uint64_t(trimEnd) * trimLen_ / 65535);
        const uint32_t durMs = uint32_t(uint64_t(s1 > s0 ? s1 - s0 : 0) * 1000 / 44100);
        if (trimElapsed_ >= durMs) {
            trimElapsed_ = durMs;
            app_.previewFile(nullptr, 0, false);
            trimPlaying_ = false;
        }
        refreshBar();
        ui_.dirty();
    }
    if (pendingProjAt_ && int32_t(now - pendingProjAt_) >= 0) {
        pendingProjAt_ = 0;
        memoryProjection();
        refreshBar();
        ui_.dirty();
    }
}

namespace {
// Column header label (spec 20 §4.5): 080808 band x 3..297 (+ 300 k),
// y 32..58 with 1-px soft edges, bold font-4 caption centred [S snap 009].
void headerLabel(Gfx& g, const Theme& th, int x, const char* text) {
    const uint32_t c = 0x080808;
    wg::panel(g, th, x + 2, 32, 294, 27, c, false);
    wg::blendFill(g, x + 2, 31, 295, 1, c, wg::kEdgeA);
    wg::blendFill(g, x + 2, 59, 295, 1, c, wg::kEdgeA);
    g.text(x + 150, 45, kFont4, th.slot(slot::kList + 4), text, IDisplay::kOptCenter);   // ink y 40..51 [S]
}
// List box (x, 61, 297, 362) under a header, with its soft top row.
void listBox(Gfx& g, const Theme& th, int x) {
    const uint32_t c = th.slot(slot::kListRed + 3);
    wg::panel(g, th, x, 61, 297, 362, c);
    wg::blendFill(g, x, 60, 298, 1, c, wg::kEdgeA);
}
int gTop[2] = {0, 0};
}  // namespace

void LoaderScreen::draw(Gfx& g) {
    App& a = app_;
    const Theme& th = ui_.th();
    ui_.drawTitle(g, "Sample Loader");   // no path [S snap 009]
    if (trim) {
        // Waveform of the decoded file (min/max per pixel column).
        g.fill(100, 116, 600, 218, th.panelDark);
        const int16_t* d = a.bank.tail();
        if (trimLen_ > 0) {
            g.rectsBegin(th.slot(slot::kWave));   // white wave (spec 20 §3.12)
            for (int x = 0; x < 600; x += 2) {
                const uint32_t s0 = uint32_t(uint64_t(x) * trimLen_ / 600), s1 = uint32_t(uint64_t(x + 2) * trimLen_ / 600);
                int mn = 0, mx = 0;
                const uint32_t stride = (s1 - s0) / 32 + 1;
                for (uint32_t s = s0; s < s1 && s < trimLen_; s += stride) {
                    if (d[s] < mn) mn = d[s];
                    if (d[s] > mx) mx = d[s];
                }
                g.rectsAdd(100 + x, 225 - mx * 100 / 32768, 1, (mx - mn) * 100 / 32768 + 1);
            }
            g.rectsEnd();
        }
        const int xs = 100 + int(uint32_t(trimStart) * 600 / 65536), xe = 100 + int(uint32_t(trimEnd) * 600 / 65536);
        g.vline(xs, 116, 218, trimSel == 0 ? th.accent : th.text);
        g.vline(xe, 116, 218, trimSel == 1 ? th.accent : th.text);
        if (trimPlaying_ && trimLen_) {
            const uint64_t f0 = uint64_t(trimStart) * trimLen_ / 65535 + uint64_t(trimElapsed_) * 44100 / 1000;
            g.vline(100 + int(f0 * 600 / trimLen_), 116, 218, th.good);
        }
        return;
    }
    // spec 20 §4.5: header labels (font 4, bg 080808, y 32), lists
    // (1 / 301, 61, 297, 362); browser selection red, instrument selection
    // white; folders "/name" white; value bars (601 / 701, 29) with white
    // fills [S snap 009].
    headerLabel(g, th, 1, "SD Card");
    headerLabel(g, th, 301, "Instruments");
    for (int l = 0; l < 2; ++l) {
        const int x = l ? 301 : 1;
        listBox(g, th, x);
        wg::ListView v;
        v.x = x;
        v.y = 61;
        v.w = 297;
        v.h = 362;
        v.background = false;
        v.set = slot::kListRed;   // marked (multi-selection) text green
        v.count = l ? 48 : count;
        v.sel = l ? insCursor : sdCursor;
        v.selStyle = l ? wg::ListSel::kWhite : wg::ListSel::kRed;
        gTop[l] = wg::listFollow(v.sel, v.count, wg::listVisibleRows(v.h), gTop[l]);
        v.top = gTop[l];
        v.ctx = this;
        if (l) {
            v.item = [](void* c, int i, char* out, int cap) {
                const App& ap = static_cast<LoaderScreen*>(c)->app_;
                if (ap.instrPresent[i]) std::snprintf(out, size_t(cap), "%d.%s", i + 1, ap.instruments[i].name);   // [S snap 009]
                else std::snprintf(out, size_t(cap), "%d.", i + 1);
            };
            v.marked = [](void* c, int i) { return static_cast<LoaderScreen*>(c)->insMarked_[i] != 0; };
        } else {
            v.item = [](void* c, int i, char* out, int cap) {
                const LoaderScreen& ls = *static_cast<LoaderScreen*>(c);
                std::snprintf(out, size_t(cap), "%s%s", ls.isDir[i] ? "/" : "", ls.entries[i]);
            };
            v.marked = [](void* c, int i) { return static_cast<LoaderScreen*>(c)->marked_[i] != 0; };
        }
        wg::list(g, th, v);
    }
    if (focus <= 1) wg::panelFocus(g, th, focus ? 301 : 1, 297);
    // Preview volume and memory bars (white fills; the memory bar shows the
    // projected use above the current one in the "above reference" colour,
    // red when it does not fit [I]).
    wg::valueBar(g, th, 601, 29, 97, 394, a.cfg.previewVolume / 100.0f);
    if (focus == 2) wg::panelFocus(g, th, 601, 97);
    const float cap = float(a.bank.capacityBytes() ? a.bank.capacityBytes() : 1);
    const float cur = float(a.bank.usedBytes()) / cap;
    const float proj = float(projected_) / cap;
    wg::ValueBarStyle ms;
    if (over_) {
        ms.fill = th.slot(slot::kFocusFrame);
        wg::valueBar(g, th, 701, 29, 97, 394, 1.0f, ms);
    } else {
        if (proj > cur) ms.ref = cur;
        wg::valueBar(g, th, 701, 29, 97, 394, proj > cur ? proj : cur, ms);
    }
}

}  // namespace ot
