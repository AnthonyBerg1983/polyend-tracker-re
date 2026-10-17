// Workspace / project management (spec 02 §1, §1.1, §7, §8.2; spec 05 §5.5
// and appendix E; spec 10 §1.5, §3.3, §4 legacy projects) and instrument
// import/delete (spec 06 §5.4).
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "app/project_fields.h"
#include "app/project_io.h"
#include "formats/bytes.h"
#include "formats/file_io.h"
#include "formats/pattern_mtp.h"
#include "formats/instrument_pti.h"
#include "formats/legacy_upgrade.h"
#include "formats/wav.h"
#include "ui/gfx.h"

namespace ot {

namespace {
struct FirstEntry {
    char name[96];
    bool isDir;
    bool found;
};
bool firstEntryFn(const IStorage::DirEntry& e, void* ctx) {
    auto* f = static_cast<FirstEntry*>(ctx);
    if (!std::strcmp(e.name, ".") || !std::strcmp(e.name, "..")) return true;
    std::snprintf(f->name, sizeof(f->name), "%s", e.name);
    f->isDir = e.isDir;
    f->found = true;
    return false;
}

bool endsWithNoCase(const char* s, const char* suf) {
    size_t a = std::strlen(s), b = std::strlen(suf);
    if (b > a) return false;
    for (size_t i = 0; i < b; ++i) {
        char x = s[a - b + i], y = suf[i];
        if (x >= 'A' && x <= 'Z') x = char(x - 'A' + 'a');
        if (y >= 'A' && y <= 'Z') y = char(y - 'A' + 'a');
        if (x != y) return false;
    }
    return true;
}

const char* baseName(const char* path) {
    const char* s = std::strrchr(path, '/');
    return s ? s + 1 : path;
}

struct NameList {
    char (*names)[64];
    int cap;
    int count;
    bool dirs;   // collect directories instead of files
};
bool collectFn(const IStorage::DirEntry& e, void* ctx) {
    auto* l = static_cast<NameList*>(ctx);
    if (e.isDir != l->dirs || e.name[0] == '.') return true;
    if (l->count >= l->cap) return false;
    std::snprintf(l->names[l->count++], 64, "%s", e.name);
    return true;
}

// Saved instrument "<N> <name>.pti" / "%02d %s.pti" -> N (1..48), else 0.
// SPEC GAP (spec 10 §7 item 1): the stock scan is unresolved; a leading
// number 1..48 followed by a space and a ".pti" suffix is accepted.
int savedPtiSlot(const char* n) {
    if (!endsWithNoCase(n, ".pti")) return 0;
    int num = 0, digits = 0;
    while (*n >= '0' && *n <= '9' && digits < 3) {
        num = num * 10 + (*n++ - '0');
        ++digits;
    }
    if (!digits || *n != ' ' || num < 1 || num > kSampleInstrumentCount) return 0;
    return num;
}

// "instrument_NN.mti" -> NN (1..48), else 0 (spec 02 §1 legacy layout).
int legacyMtiSlot(const char* n) {
    if (std::strncmp(n, "instrument_", 11) != 0 || !endsWithNoCase(n, ".mti")) return 0;
    if (std::strlen(n) != 17) return 0;
    const char a = n[11], b = n[12];
    if (a < '0' || a > '9' || b < '0' || b > '9') return 0;
    const int num = (a - '0') * 10 + (b - '0');
    return num >= 1 && num <= kSampleInstrumentCount ? num : 0;
}

// "instrNN.wav" -> NN (1..48), else 0.
int legacyWavSlot(const char* n) {
    if (std::strncmp(n, "instr", 5) != 0 || std::strlen(n) != 11 || !endsWithNoCase(n, ".wav")) return 0;
    const char a = n[5], b = n[6];
    if (a < '0' || a > '9' || b < '0' || b > '9') return 0;
    const int num = (a - '0') * 10 + (b - '0');
    return num >= 1 && num <= kSampleInstrumentCount ? num : 0;
}

void mtiPath(char* out, size_t cap, const char* base, int n) {
    std::snprintf(out, cap, "%s/instruments/instrument_%02d.mti", base, n);
}
void legacyWavPath(char* out, size_t cap, const char* base, int n) {
    std::snprintf(out, cap, "%s/samples/instr%02d.wav", base, n);
}

// Per-slot scan of a saved project's instruments/ and samples/ folders.
struct SlotScan {
    uint32_t ptiPayload[kSampleInstrumentCount];
    uint32_t wavBytes[kSampleInstrumentCount];
    bool hasPti[kSampleInstrumentCount];
    bool hasMti[kSampleInstrumentCount];
    bool hasWav[kSampleInstrumentCount];
};
bool scanInstrFn(const IStorage::DirEntry& e, void* ctx) {
    auto* s = static_cast<SlotScan*>(ctx);
    if (e.isDir) return true;
    if (int n = savedPtiSlot(e.name)) {
        s->hasPti[n - 1] = true;
        s->ptiPayload[n - 1] = e.size > pti::kDataOff ? e.size - pti::kDataOff : 0;
    } else if (int m = legacyMtiSlot(e.name)) {
        s->hasMti[m - 1] = true;
    }
    return true;
}
bool scanWavFn(const IStorage::DirEntry& e, void* ctx) {
    auto* s = static_cast<SlotScan*>(ctx);
    if (e.isDir) return true;
    if (int n = legacyWavSlot(e.name)) {
        s->hasWav[n - 1] = true;
        s->wavBytes[n - 1] = e.size > wav::kHeaderSize ? e.size - wav::kHeaderSize : 0;
    }
    return true;
}
void scanSlots(IStorage& f, const char* dir, SlotScan& s) {
    std::memset(&s, 0, sizeof(s));
    char p[300];
    std::snprintf(p, sizeof(p), "%s/instruments", dir);
    f.listDir(p, scanInstrFn, &s);
    std::snprintf(p, sizeof(p), "%s/samples", dir);
    f.listDir(p, scanWavFn, &s);
}

bool stopAtFirst(const IStorage::DirEntry&, void*) { return false; }
}  // namespace

bool App::removeTree(const char* path) {
    IStorage& f = fs();
    if (!f.exists(path)) return true;
    char child[300];
    for (int guard = 0; guard < 4096; ++guard) {
        FirstEntry e{};
        if (f.listDir(path, firstEntryFn, &e) < 0 || !e.found) break;
        std::snprintf(child, sizeof(child), "%s/%s", path, e.name);
        if (e.isDir) {
            if (!removeTree(child)) return false;
        } else if (!f.remove(child)) {
            return false;
        }
    }
    return f.remove(path);
}

bool App::copyFile(const char* from, const char* to) {
    IStorage& f = fs();
    IStorage::Handle src = f.open(from, IStorage::kRead);
    if (src < 0) return false;
    if (f.exists(to)) f.remove(to);
    IStorage::Handle dst = f.open(to, IStorage::kWrite);
    if (dst < 0) {
        f.close(src);
        return false;
    }
    bool ok = true;
    uint8_t* buf = mem_.scratch;
    const uint32_t cap = mem_.scratchBytes > kIoChunk ? kIoChunk : mem_.scratchBytes;
    for (;;) {
        int32_t n = f.read(src, buf, cap);
        if (n < 0) {
            ok = false;
            break;
        }
        if (n == 0) break;
        if (f.write(dst, buf, uint32_t(n)) != n) {
            ok = false;
            break;
        }
    }
    f.close(src);
    f.close(dst);
    return ok;
}

bool App::projectExists(const char* folder, const char* name) {
    char p[280];
    std::snprintf(p, sizeof(p), "%.221s/%.32s/project.mt", folder && folder[0] ? folder : kProjectsDir, name);
    return fs().exists(p);
}

bool App::newProject() {
    // spec 05 appendix E3: flush a running job first.
    finishJob();
    // spec 05 appendix E3 NEW PROJECT: update recents with the current project.
    recentsLeaveCurrent(*this);
    // spec 02 §1.1 "New project".
    seq.stop();
    allVoicesOff();
    IStorage& f = fs();
    removeTree(kWorkspace);
    if (!f.mkdir(kWorkspace)) return false;
    f.mkdir("Workspace/instruments");
    f.mkdir("Workspace/patterns");
    f.mkdir("Workspace/samples");
    project.setDefault();
    project.setName("New Project");
    project.setFolder("");
    project.setCurrentPattern(1);
    names.clear();
    engineDirty_ = true;
    bank.clear();
    for (int i = 0; i < kSampleInstrumentCount; ++i) {
        instruments[i].setDefault();
        instrPresent[i] = false;
        instrHeaderDirty_[i] = instrSampleDirty_[i] = false;
    }
    std::memset(present_, 0, sizeof(present_));
    std::memset(patDirty_, 0, sizeof(patDirty_));
    for (int n = 1; n <= kPatternCount; ++n) defaultPattern(stored(n));
    setPresent(1, true);
    pattern() = stored(1);
    attachSequencer();
    char path[96];
    patternFilePath(path, sizeof(path), kWorkspace, 1);
    bool ok = savePatternFile(f, path, stored(1)) == FmtStatus::Ok;
    projectFilePath(path, sizeof(path), kWorkspace);
    ok = saveProjectFile(f, path, project) == FmtStatus::Ok && ok;
    undo.clear();
    clipValid_ = false;
    projectDirty_ = namesDirty_ = false;
    unsaved_ = false;
    neverSaved_ = true;
    refreshPadNotes();
    ui.dirty();
    return ok;
}

// ---------------------------------------------------------------------------
// Background project operations (spec 05 appendix E3/E4 state machine
// 0x1d5e0; spec 10 §4.2/§4.3 open/save of legacy projects).
//
// A job is a list of stages; each call of jobUnit() does one unit of work:
// one small file (project.mt, one pattern, one header, ...), one existence
// probe, or one kJobChunk piece of a large file (instrument copy, sample
// payload, WAV import). serviceProjectJob() runs units until kJobPassBytes of
// work or kJobPassUs of time are used, so the main loop keeps its rate
// (field: opening a 48-pattern / 47-instrument legacy project blocked the loop
// for 8 s).
//
// Load order (spec 10 §2.2 ordering dependency): project.mt -> instrument
// headers (the pattern upgrade needs their play modes) -> patterns -> sample
// payloads. The headers carry the play modes, so the patterns are decoded
// before the (large) sample data instead of after it as in stock; the result
// is the same.

namespace {
enum Stage : uint8_t {
    kLProject = 1, kLHeaders, kLNames, kLPatterns, kLSamples, kLFinish,
    kOWipe, kODirs, kOPatterns, kOScan, kOInstr, kOLegacy, kONames,
    kABegin, kAPatterns, kANames, kAInstr, kAProject, kAFinish,
    kSDirs, kSPatterns, kSNames, kSCleanList, kSClean, kSInstr, kSFinish,
};

// Instrument file names of the project being opened / saved.
char g_list[kSampleInstrumentCount + 16][64];
SlotScan g_scan;

struct DirSum {
    uint64_t bytes = 0;
    uint32_t files = 0;
};
bool sumFn(const IStorage::DirEntry& e, void* ctx) {
    auto* s = static_cast<DirSum*>(ctx);
    if (!e.isDir) {
        s->bytes += e.size;
        ++s->files;
    }
    return true;
}
DirSum dirSum(IStorage& f, const char* base, const char* sub) {
    char p[340];
    std::snprintf(p, sizeof(p), "%s/%s", base, sub);
    DirSum s;
    f.listDir(p, sumFn, &s);
    return s;
}
}  // namespace

int App::jobPercent() const {
    if (pj_.op == JobOp::None) return 100;
    if (pj_.op == JobOp::External) {
        const int p = pj_.extPercent ? pj_.extPercent(pj_.extCtx) : 0;
        return p < 0 ? 0 : (p > 99 ? 99 : p);
    }
    const uint64_t t = pj_.total ? pj_.total : 1;
    const uint64_t p = pj_.done * 100 / t;
    return p > 99 ? 99 : int(p);
}

void App::estimateJobTotal() {
    IStorage& f = fs();
    uint64_t t = 0;
    auto add = [&](const DirSum& s, uint64_t mul) { t += s.bytes * mul + uint64_t(s.files) * kJobSmallCost; };
    switch (pj_.op) {
        case JobOp::Open:
            add(dirSum(f, pj_.dir, "patterns"), 2);      // copy, then load
            add(dirSum(f, pj_.dir, "instruments"), 2);   // copy, then load
            add(dirSum(f, pj_.dir, "samples"), 2);       // legacy import + .pti write
            break;
        case JobOp::LoadWorkspace:
            add(dirSum(f, kWorkspace, "patterns"), 1);
            add(dirSum(f, kWorkspace, "instruments"), 1);
            add(dirSum(f, kWorkspace, "samples"), 2);
            break;
        case JobOp::Save:
            add(dirSum(f, kWorkspace, "patterns"), 1);
            add(dirSum(f, kWorkspace, "instruments"), 1);
            break;
        default:
            break;
    }
    pj_.total = t ? t : 1;
}

void App::jobProgress() {
    if (!pj_.popup || pj_.op == JobOp::None) return;
    ui.setProgress(pj_.title, jobPercent());
}

void App::jobCloseFiles() {
    IStorage& f = fs();
    if (pj_.in >= 0) f.close(pj_.in);
    if (pj_.out >= 0) f.close(pj_.out);
    pj_.in = pj_.out = -1;
}

void App::jobEnd() {
    jobCloseFiles();
    const bool ok = pj_.ok;
    const bool popup = pj_.popup;
    JobDoneFn fn = pj_.onDone;
    void* ctx = pj_.ctx;
    lastJobOk_ = ok;
    lastJobErrors_ = pj_.loadErrors;
    pj_ = ProjectJob{};
    if (popup) ui.progressDone();
    if (fn) fn(ctx, ok);
}

void App::jobFail() {
    pj_.ok = false;
    jobEnd();
}

void App::abortJob() {
    if (pj_.op != JobOp::None) jobFail();
}

bool App::serviceProjectJob() {
    if (pj_.op == JobOp::None) return false;
    const uint32_t t0 = hal_.clock ? hal_.clock->micros() : 0;
    uint32_t cost = 0;
    while (pj_.op != JobOp::None) {
        if (!jobUnit(cost)) break;
        if (cost >= kJobPassBytes) break;
        if (hal_.clock && hal_.clock->micros() - t0 >= kJobPassUs) break;
    }
    jobProgress();
    return pj_.op != JobOp::None;
}

void App::finishJob() {
    while (serviceProjectJob()) {
        // Blocking callers (boot, power-off): keep the popup moving
        // (spec 05 §1.3: progress refresh every > 50 ms).
        if (pj_.popup && hal_.display && millis() - jobProgressMs_ > 50) {
            jobProgressMs_ = millis();
            ui.render();
        }
    }
}

// One chunk of a file copy. Returns true when the copy is finished (or
// failed: *failed set).
bool App::jobCopyUnit(const char* from, const char* to, uint32_t& cost, bool* failed) {
    IStorage& f = fs();
    if (pj_.phase == 0) {
        pj_.in = f.open(from, IStorage::kRead);
        if (pj_.in < 0) {
            *failed = true;
            cost += kJobSmallCost;
            return true;
        }
        if (f.exists(to)) f.remove(to);
        pj_.out = f.open(to, IStorage::kWrite);
        if (pj_.out < 0) {
            jobCloseFiles();
            *failed = true;
            cost += kJobSmallCost;
            return true;
        }
        const int32_t sz = f.size(pj_.in);
        pj_.len = sz > 0 ? uint32_t(sz) : 0;
        pj_.pos = 0;
        pj_.phase = 1;
        cost += kJobSmallCost;
        pj_.done += kJobSmallCost;
    }
    const uint32_t cap = mem_.scratchBytes < kJobChunk ? mem_.scratchBytes : kJobChunk;
    int32_t n = pj_.pos < pj_.len ? f.read(pj_.in, mem_.scratch, cap) : 0;
    if (n < 0 && f.seek(pj_.in, pj_.pos)) n = f.read(pj_.in, mem_.scratch, cap);   // one retry
    bool bad = n < 0;
    if (n > 0 && f.write(pj_.out, mem_.scratch, uint32_t(n)) != n) bad = true;
    if (n > 0) {
        pj_.pos += uint32_t(n);
        cost += uint32_t(n);
        pj_.done += uint32_t(n);
    }
    if (bad || n <= 0 || pj_.pos >= pj_.len) {
        jobCloseFiles();
        pj_.phase = 0;
        if (bad) *failed = true;
        return true;
    }
    return false;
}

// One chunk of writing instrument `slot` from the sample bank as a full
// .pti (header + payload, spec 02 §6). Returns true when finished. A bank
// change while writing (sample edit, import) abandons the file and marks the
// slot dirty again so the next autosave rewrites it.
bool App::jobPtiWriteUnit(int slot, const char* path, uint32_t& cost, bool* failed) {
    IStorage& f = fs();
    if (pj_.wphase == 0) {
        Instrument ins = instruments[slot];
        ins.lengthFrames = bank.frames(slot);
        pj_.src = bank.left(slot);
        pj_.srcFrames = ins.lengthFrames;
        pj_.len = ins.lengthFrames * (ins.stereo ? 2u : 1u);
        pj_.pos = 0;
        if (f.exists(path) && !f.remove(path)) {
            *failed = true;
            return true;
        }
        pj_.out = f.open(path, IStorage::kWrite);
        cost += kJobSmallCost;
        if (pj_.out < 0) {
            *failed = true;
            return true;
        }
        uint8_t hdr[pti::kHeaderBlockSize];
        encodePtiHeader(ins, hdr);
        if (f.write(pj_.out, hdr, sizeof(hdr)) != int32_t(sizeof(hdr))) {
            jobCloseFiles();
            *failed = true;
            return true;
        }
        pj_.wphase = 1;
        if (!pj_.src) pj_.len = 0;
    }
    if (bank.left(slot) != pj_.src || bank.frames(slot) != pj_.srcFrames) {
        jobCloseFiles();
        pj_.wphase = 0;
        instrSampleDirty_[slot] = true;
        return true;
    }
    if (pj_.pos < pj_.len) {
        const uint32_t cap = (mem_.scratchBytes < kJobChunk ? mem_.scratchBytes : kJobChunk) / 2;
        const uint32_t n = pj_.len - pj_.pos < cap ? pj_.len - pj_.pos : cap;
        uint8_t* buf = mem_.scratch;
        for (uint32_t k = 0; k < n; ++k) wrU16(buf + 2 * k, uint16_t(pj_.src[pj_.pos + k]));
        if (f.write(pj_.out, buf, 2 * n) != int32_t(2 * n)) {
            jobCloseFiles();
            pj_.wphase = 0;
            *failed = true;
            return true;
        }
        pj_.pos += n;
        cost += 2 * n;
        pj_.done += 2 * n;
    }
    if (pj_.pos >= pj_.len) {
        jobCloseFiles();
        pj_.wphase = 0;
        return true;
    }
    return false;
}

// Sample data of Workspace instrument `slot` (spec 10 §3.3): a .pti payload
// read straight into the bank, or (legacy, payload-less header) the WAV
// imported with the normal importer and then written as a v9 .pti.
// Returns true when the slot is finished.
bool App::jobSamplesUnit(int slot, uint32_t& cost) {
    IStorage& f = fs();
    char path[340];
    workspaceInstrumentPath(path, sizeof(path), kWorkspace, slot + 1);
    auto clearSlot = [&](bool error) {
        jobCloseFiles();
        instruments[slot].setDefault();
        instrPresent[slot] = false;
        if (error) ++pj_.loadErrors;
        pj_.phase = pj_.wphase = 0;
        return true;
    };
    if (pj_.kind[slot] == 1) {
        if (pj_.phase == 0) {
            pj_.in = f.open(path, IStorage::kRead);
            cost += kJobSmallCost;
            if (pj_.in < 0) return clearSlot(true);
            const uint32_t payload = pj_.fileSize[slot] > pj_.dataOff[slot] ? pj_.fileSize[slot] - pj_.dataOff[slot] : 0;
            const PtiSampleLayout lay = resolvePtiSample(instruments[slot].lengthFrames, payload, false);
            const uint32_t count = lay.frames * (lay.useStereo ? 2u : 1u);
            if (count > bank.freeSamples()) return clearSlot(true);   // checked before opening (spec 02 §8.2)
            if (count && !f.seek(pj_.in, pj_.dataOff[slot])) return clearSlot(true);
            pj_.srcFrames = lay.frames;
            pj_.srcStereo = lay.useStereo;
            pj_.len = count * 2;
            pj_.pos = 0;
            pj_.phase = 1;
        }
        uint8_t* raw = reinterpret_cast<uint8_t*>(bank.tail());
        if (pj_.pos < pj_.len) {
            const uint32_t want = pj_.len - pj_.pos < kJobChunk ? pj_.len - pj_.pos : kJobChunk;
            int32_t r = f.read(pj_.in, raw + pj_.pos, want);
            if (r < 0 && f.seek(pj_.in, pj_.dataOff[slot] + pj_.pos)) r = f.read(pj_.in, raw + pj_.pos, want);
            if (r < 0) ++pj_.loadErrors;
            const uint32_t got = r > 0 ? uint32_t(r) & ~1u : 0;
            // Little-endian payload -> native int16 in place (spec 02 §6.3).
            int16_t* s = bank.tail() + pj_.pos / 2;
            for (uint32_t k = 0; k < got / 2; ++k) s[k] = rdI16(raw + pj_.pos + 2 * k);
            pj_.pos += got;
            cost += got;
            pj_.done += got;
            if (got < want) {
                // Short file: the rest reads as zero (as loadInstrumentFile).
                std::memset(raw + pj_.pos, 0, pj_.len - pj_.pos);
                pj_.pos = pj_.len;
            }
        }
        if (pj_.pos < pj_.len) return false;
        jobCloseFiles();
        Instrument ins = instruments[slot];
        ins.lengthFrames = pj_.srcFrames;
        ins.stereo = pj_.srcStereo;
        bank.commitTail(slot, ins.lengthFrames, ins.stereo);
        instruments[slot] = ins;
        instrPresent[slot] = true;
        pj_.phase = 0;
        return true;
    }
    // Legacy slot: import <wavBase>/samples/instrNN.wav (spec 10 §3.3 steps 3-6).
    if (pj_.phase == 0) {
        char wavPath[340];
        legacyWavPath(wavPath, sizeof(wavPath), pj_.wavBase, slot + 1);
        cost += kJobSmallCost;
        pj_.in = f.open(wavPath, IStorage::kRead);
        // spec 10 §7 unknown 3: a missing WAV clears the slot.
        if (pj_.in < 0) return clearSlot(f.exists(wavPath));
        WavInfo info;
        StorageReader r(f, pj_.in);
        if (!wavParseHeader(r, info)) return clearSlot(false);
        WavImportOptions opt;
        if (pj_.wav.begin(info, opt, bank.tail(), bank.freeSamples()) != FmtStatus::Ok) return clearSlot(true);
        pj_.len = info.dataSize;
        pj_.pos = 0;
        pj_.phase = 1;
    }
    if (pj_.phase == 1) {
        const uint32_t cap = mem_.scratchBytes < kJobChunk ? mem_.scratchBytes : kJobChunk;
        const uint32_t want = pj_.len - pj_.pos < cap ? pj_.len - pj_.pos : cap;
        const int32_t got = want ? f.read(pj_.in, mem_.scratch, want) : 0;
        if (got > 0) {
            pj_.wav.feed(mem_.scratch, size_t(got));
            pj_.pos += uint32_t(got);
            cost += uint32_t(got);
            pj_.done += uint32_t(got);
        }
        if (got > 0 && pj_.pos < pj_.len) return false;
        // End of data (a missing tail is zero-filled by the importer).
        jobCloseFiles();
        const uint32_t frames = pj_.wav.finish();
        Instrument ins = instruments[slot];
        legacy::applyImportedLength(ins, frames, false);
        bank.commitTail(slot, frames, false);
        ins.lengthFrames = bank.frames(slot);
        instruments[slot] = ins;
        instrPresent[slot] = true;
        pj_.phase = 2;
        pj_.wphase = 0;
        return false;
    }
    // SPEC GAP: stock writes the payload now and the v9 header at the next
    // autosave (dirty flag); both are written here at once.
    bool failed = false;
    if (!jobPtiWriteUnit(slot, path, cost, &failed)) return false;
    if (failed) ++pj_.loadErrors;
    pj_.phase = 0;
    return true;
}

// One step of the recursive Workspace delete. 0 = done.
int App::jobWipeUnit(uint32_t& cost) {
    IStorage& f = fs();
    if (!f.exists(pj_.wipe)) return 0;
    FirstEntry e{};
    cost += kJobProbeCost;
    if (f.listDir(pj_.wipe, firstEntryFn, &e) < 0 || !e.found) {
        cost += kJobSmallCost;
        const bool removed = f.remove(pj_.wipe);
        if (!removed || std::strcmp(pj_.wipe, kWorkspace) == 0) return 0;
        *std::strrchr(pj_.wipe, '/') = 0;
        return 1;
    }
    char child[sizeof(pj_.wipe)];
    if (std::snprintf(child, sizeof(child), "%s/%s", pj_.wipe, e.name) >= int(sizeof(child))) return 0;
    if (e.isDir) {
        std::memcpy(pj_.wipe, child, sizeof(child));
        return 1;
    }
    cost += kJobSmallCost;
    // SPEC GAP: a file that cannot be deleted stops the wipe; the copies
    // below overwrite what they need.
    return f.remove(child) ? 1 : 0;
}

void App::jobBeginLoad() {
    pj_.stage = kLProject;
    pj_.idx = 0;
    pj_.phase = pj_.wphase = 0;
}

// One unit of the current job. Returns false when the job has ended.
bool App::jobUnit(uint32_t& cost) {
    IStorage& f = fs();
    char src[340], dst[340];
    if (pj_.op == JobOp::External) {
        const int r = pj_.extUnit(pj_.extCtx, cost);
        if (r > 0) return true;
        if (r < 0) jobFail();
        else jobEnd();
        return false;
    }
    switch (pj_.stage) {
        // ---- workspace load (op 8 / tail of op 9; spec 05 E3 stage 1) ----
        case kLProject: {
            seq.stop();
            allVoicesOff();
            projectFilePath(src, sizeof(src), kWorkspace);
            Project p;
            ProjectLoadInfo pinfo;
            // spec 10 §1: structure 1..16 upgraded in RAM; type != 0 / version 0 fail.
            if (loadProjectFile(f, src, p, &pinfo) != FmtStatus::Ok) {
                jobFail();
                return false;
            }
            project = p;
            pj_.sizeMismatch = pinfo.sizeMismatch;
            engineDirty_ = true;
            bank.clear();
            for (int i = 0; i < kSampleInstrumentCount; ++i) {
                instruments[i].setDefault();
                instrPresent[i] = false;
                pj_.kind[i] = 0;
            }
            std::memset(present_, 0, sizeof(present_));
            cost += kJobSmallCost;
            pj_.done += kJobSmallCost;
            pj_.stage = kLHeaders;
            pj_.idx = 0;
            return true;
        }
        case kLHeaders: {
            const int i = pj_.idx++;
            if (i >= kSampleInstrumentCount) {
                pj_.stage = kLNames;
                return true;
            }
            workspaceInstrumentPath(src, sizeof(src), kWorkspace, i + 1);
            // spec 10 §3.3 step 1: instrument_NN.mti -> .pti when no .pti exists.
            if (!f.exists(src)) {
                mtiPath(dst, sizeof(dst), kWorkspace, i + 1);
                cost += kJobProbeCost;
                if (!f.exists(dst)) return true;
                if (!f.rename(dst, src)) {
                    ++pj_.loadErrors;
                    return true;
                }
            }
            // spec 10 §3.3 step 2: zero-filled 0x188-byte header, chain, data offset.
            cost += kJobSmallCost;
            pj_.done += kJobSmallCost;
            IStorage::Handle h = f.open(src, IStorage::kRead);
            if (h < 0) {
                ++pj_.loadErrors;
                return true;
            }
            const int32_t size = f.size(h);
            uint8_t hdr[pti::kHeaderBlockSize] = {};
            const int32_t got = size > 0 ? f.read(h, hdr, sizeof(hdr)) : 0;
            f.close(h);
            if (size < 0 || got < 0) {
                ++pj_.loadErrors;
                return true;
            }
            Instrument ins;
            PtiLoadInfo li;
            if (decodePtiHeader(hdr, size_t(got), ins, &li) != FmtStatus::Ok) return true;
            if (!legacy::ptiSlotAccepted(size_t(size), li.dataOffset)) return true;   // slot cleared
            pj_.kind[i] = legacy::ptiNeedsWav(size_t(size), li.dataOffset) ? 2 : 1;
            pj_.fileSize[i] = uint32_t(size);
            pj_.dataOff[i] = li.dataOffset;
            instruments[i] = ins;   // header only; present once its samples are in
            return true;
        }
        case kLNames:
            patternNamesPath(src, sizeof(src), kWorkspace);
            loadPatternNamesFile(f, src, names);
            cost += kJobSmallCost;
            for (int i = 0; i < kSampleInstrumentCount; ++i) pj_.modes[i] = instruments[i].playMode;
            pj_.stage = kLPatterns;
            pj_.idx = 1;
            return true;
        case kLPatterns: {
            const int n = pj_.idx++;
            if (n > kPatternCount) {
                pj_.stage = kLSamples;
                pj_.idx = 0;
                return true;
            }
            patternFilePath(src, sizeof(src), kWorkspace, n);
            if (!f.exists(src)) {
                cost += kJobProbeCost;
                defaultPattern(stored(n));
                return true;
            }
            PatternUpgradeContext ctx;
            ctx.instrumentPlayModes = pj_.modes;
            ctx.beatSliceReference = project.beatSliceReference();
            // spec 10 §2.2/§2.3: legacy patterns are upgraded on every load and
            // not marked dirty (they stay legacy on the card until edited).
            FmtStatus st = loadPatternFile(f, src, stored(n), &ctx);
            if (st == FmtStatus::IoError) st = loadPatternFile(f, src, stored(n), &ctx);   // one retry
            if (st == FmtStatus::Ok) {
                setPresent(n, true);
            } else {
                // The file is there but unreadable: report it instead of
                // silently showing an empty pattern (field bug #1).
                defaultPattern(stored(n));
                ++pj_.loadErrors;
            }
            cost += kJobSmallCost + mtp::kFileSize;
            pj_.done += kJobSmallCost + mtp::kLegacyFileSize;
            return true;
        }
        case kLSamples: {
            const int i = pj_.idx;
            if (i >= kSampleInstrumentCount) {
                pj_.stage = kLFinish;
                return true;
            }
            if (pj_.kind[i] == 0 || jobSamplesUnit(i, cost)) ++pj_.idx;
            return true;
        }
        case kLFinish: {
            uint8_t cur = project.currentPattern();
            if (cur < 1) cur = 1;
            project.setCurrentPattern(cur);
            loadPattern(cur, pattern(), true);
            attachSequencer();
            undo.clear();
            std::memset(patDirty_, 0, sizeof(patDirty_));
            projectDirty_ = namesDirty_ = false;
            for (int i = 0; i < kSampleInstrumentCount; ++i) instrHeaderDirty_[i] = instrSampleDirty_[i] = false;
            unsaved_ = false;
            // spec 05 appendix E2: an empty folder marks a never-saved (new) project.
            neverSaved_ = !project.name[0] || !project.folder[0] || !projectExists(project.folder, project.name);
            if (pj_.sizeMismatch) {
                // spec 10 §1.3 / §4.4: a legacy Workspace/project.mt loads as
                // "Unsaved Project" with status flags 3 (never saved + unsaved).
                neverSaved_ = true;
                unsaved_ = true;
            }
            if (pj_.opened) {
                // spec 05 appendix E2: opening sets the status flags to 0.
                neverSaved_ = false;
                unsaved_ = false;
            }
            refreshPadNotes();
            ui.dirty();
            jobEnd();
            return false;
        }

        // ---- open project (op 9 stage 3; spec 10 §4.2) -------------------
        case kOWipe:
            // Step 0: wipe and recreate the Workspace.
            if (jobWipeUnit(cost) == 0) pj_.stage = kODirs;
            return true;
        case kODirs:
            f.mkdir(kWorkspace);
            f.mkdir("Workspace/instruments");
            f.mkdir("Workspace/patterns");
            f.mkdir("Workspace/samples");
            // spec 10 §1.5 / §4.2 step 2: Workspace/project.mt as v17 with the
            // selected name and parent folder (also for legacy files).
            projectFilePath(dst, sizeof(dst), kWorkspace);
            if (saveProjectFile(f, dst, pj_.proj) != FmtStatus::Ok) {
                jobFail();
                return false;
            }
            cost += 4 * kJobSmallCost;
            pj_.stage = kOPatterns;
            pj_.idx = 1;
            return true;
        case kOPatterns: {
            // Step 3: patterns raw (spec 10 §2.3).
            const int n = pj_.idx;
            if (n > kPatternCount) {
                pj_.stage = kOScan;
                return true;
            }
            patternFilePath(src, sizeof(src), pj_.dir, n);
            if (pj_.phase == 0 && !f.exists(src)) {
                cost += kJobProbeCost;
                ++pj_.idx;
                return true;
            }
            patternFilePath(dst, sizeof(dst), kWorkspace, n);
            bool failed = false;
            if (jobCopyUnit(src, dst, cost, &failed)) {
                if (failed) ++pj_.loadErrors;
                ++pj_.idx;
            }
            return true;
        }
        case kOScan: {
            scanSlots(f, pj_.dir, g_scan);
            NameList nl{g_list, int(sizeof(g_list) / sizeof(g_list[0])), 0, false};
            std::snprintf(src, sizeof(src), "%s/instruments", pj_.dir);
            f.listDir(src, collectFn, &nl);
            pj_.listCount = nl.count;
            cost += kJobSmallCost;
            pj_.stage = kOInstr;
            pj_.idx = 0;
            return true;
        }
        case kOInstr: {
            // Step 4: a saved "<N> <name>.pti" per slot -> instrument_NN.mti
            // (renamed to .pti by the load).
            const int k = pj_.idx;
            if (k >= pj_.listCount) {
                pj_.stage = kOLegacy;
                pj_.idx = 0;
                return true;
            }
            const int num = savedPtiSlot(g_list[k]);
            if (!num || pj_.copied[num - 1]) {
                ++pj_.idx;
                return true;
            }
            std::snprintf(src, sizeof(src), "%s/instruments/%.63s", pj_.dir, g_list[k]);
            mtiPath(dst, sizeof(dst), kWorkspace, num);
            bool failed = false;
            if (jobCopyUnit(src, dst, cost, &failed)) {
                if (failed) ++pj_.loadErrors;
                else pj_.copied[num - 1] = true;
                ++pj_.idx;
            }
            return true;
        }
        case kOLegacy: {
            // Step 4 (legacy): instrument_NN.mti. Step 5: OpenTracker imports
            // samples/instrNN.wav straight from the project folder during the
            // load (same importer, same result) instead of copying it into
            // Workspace/samples first; that halves the card traffic.
            const int n = pj_.idx;
            if (n >= kSampleInstrumentCount) {
                pj_.stage = kONames;
                return true;
            }
            if (pj_.copied[n] || !g_scan.hasMti[n]) {
                ++pj_.idx;
                return true;
            }
            mtiPath(src, sizeof(src), pj_.dir, n + 1);
            mtiPath(dst, sizeof(dst), kWorkspace, n + 1);
            bool failed = false;
            if (jobCopyUnit(src, dst, cost, &failed)) {
                if (failed) ++pj_.loadErrors;
                ++pj_.idx;
            }
            return true;
        }
        case kONames:
            // Step 6: copy patternsMetadata, or create a zeroed one (spec 10 §2.3).
            patternNamesPath(src, sizeof(src), pj_.dir);
            patternNamesPath(dst, sizeof(dst), kWorkspace);
            if (pj_.phase == 0 && !f.exists(src)) {
                names.clear();
                savePatternNamesFile(f, dst, names);
                cost += kJobSmallCost;
                jobBeginLoad();
                return true;
            }
            {
                bool failed = false;
                if (jobCopyUnit(src, dst, cost, &failed)) {
                    if (failed) ++pj_.loadErrors;
                    jobBeginLoad();
                }
            }
            return true;

        // ---- save to Workspace (op 0x0c autosave / stage 2 of op 0x0d) ----
        case kABegin:
            lastAutosave_ = millis();
            stored(patternNumber()) = pattern();
            pj_.stage = kAPatterns;
            pj_.idx = 1;
            return true;
        case kAPatterns: {
            int n = pj_.idx;
            while (n <= kPatternCount && !((patDirty_[n >> 3] >> (n & 7)) & 1)) ++n;
            if (n > kPatternCount) {
                pj_.stage = kANames;
                return true;
            }
            pj_.idx = n + 1;
            patDirty_[n >> 3] &= uint8_t(~(1u << (n & 7)));
            patternFilePath(dst, sizeof(dst), kWorkspace, n);
            if (savePatternFile(f, dst, stored(n)) != FmtStatus::Ok) {
                patDirty_[n >> 3] |= uint8_t(1u << (n & 7));
                ++pj_.loadErrors;
            }
            cost += kJobSmallCost + mtp::kFileSize;
            return true;
        }
        case kANames:
            if (namesDirty_) {
                patternNamesPath(dst, sizeof(dst), kWorkspace);
                namesDirty_ = false;
                if (savePatternNamesFile(f, dst, names) != FmtStatus::Ok) {
                    namesDirty_ = true;
                    ++pj_.loadErrors;
                }
                cost += kJobSmallCost;
            }
            pj_.stage = kAInstr;
            pj_.idx = 0;
            return true;
        case kAInstr: {
            int i = pj_.idx;
            if (pj_.wphase == 0) {
                while (i < kSampleInstrumentCount && !instrHeaderDirty_[i] && !instrSampleDirty_[i]) ++i;
                pj_.idx = i;
                if (i >= kSampleInstrumentCount) {
                    pj_.stage = kAProject;
                    return true;
                }
                pj_.sampleDirty = instrSampleDirty_[i];
                instrHeaderDirty_[i] = instrSampleDirty_[i] = false;
            }
            workspaceInstrumentPath(dst, sizeof(dst), kWorkspace, i + 1);
            if (pj_.wphase == 0 && !instrPresent[i]) {
                if (f.exists(dst)) f.remove(dst);
                cost += kJobSmallCost;
                ++pj_.idx;
                return true;
            }
            if (pj_.wphase == 0 && !pj_.sampleDirty && f.exists(dst)) {
                if (saveInstrumentHeader(f, dst, instruments[i]) != FmtStatus::Ok) {
                    instrHeaderDirty_[i] = true;
                    ++pj_.loadErrors;
                }
                cost += kJobSmallCost;
                ++pj_.idx;
                return true;
            }
            bool failed = false;
            if (jobPtiWriteUnit(i, dst, cost, &failed)) {
                if (failed) {
                    instrSampleDirty_[i] = true;
                    ++pj_.loadErrors;
                }
                ++pj_.idx;
            }
            return true;
        }
        case kAProject:
            if (projectDirty_) {
                project.setCurrentPattern(patternNumber());
                projectFilePath(dst, sizeof(dst), kWorkspace);
                projectDirty_ = false;
                if (saveProjectFile(f, dst, project) != FmtStatus::Ok) {
                    projectDirty_ = true;
                    ++pj_.loadErrors;
                }
                cost += kJobSmallCost;
            }
            pj_.stage = kAFinish;
            return true;
        case kAFinish:
            if (pj_.op == JobOp::Save) {
                // spec 05 appendix E3: op 0x0d continues with stage 4.
                if (pj_.loadErrors) {
                    jobFail();
                    return false;
                }
                pj_.stage = kSDirs;
                return true;
            }
            jobEnd();
            return false;

        // ---- copy Workspace -> "<folder>/<name>" (stage 4; spec 10 §4.3) ----
        case kSDirs: {
            // Create every level of the folder path.
            char folder[sizeof(pj_.dir)];
            std::snprintf(folder, sizeof(folder), "%s", pj_.dir);
            for (char* p = folder; *p; ++p) {
                if (*p != '/' || p == folder) continue;
                *p = 0;
                f.mkdir(folder);
                *p = '/';
            }
            if (!f.mkdir(pj_.dir)) {
                jobFail();
                return false;
            }
            // spec 10 §4.3 step 0: patterns/ and instruments/. OpenTracker
            // deviation (README): the project's samples/ folder is NOT deleted.
            std::snprintf(dst, sizeof(dst), "%s/patterns", pj_.dir);
            f.mkdir(dst);
            std::snprintf(dst, sizeof(dst), "%s/instruments", pj_.dir);
            f.mkdir(dst);
            projectFilePath(dst, sizeof(dst), pj_.dir);
            if (saveProjectFile(f, dst, project) != FmtStatus::Ok) ++pj_.loadErrors;
            cost += 4 * kJobSmallCost;
            pj_.stage = kSPatterns;
            pj_.idx = 1;
            return true;
        }
        case kSPatterns: {
            // spec 10 §2.3: patterns are copied raw (unedited legacy ones stay legacy).
            const int n = pj_.idx;
            if (n > kPatternCount) {
                pj_.stage = kSNames;
                return true;
            }
            patternFilePath(src, sizeof(src), kWorkspace, n);
            patternFilePath(dst, sizeof(dst), pj_.dir, n);
            if (pj_.phase == 0 && !(patternPresent(n) && f.exists(src))) {
                cost += kJobProbeCost;
                if (f.exists(dst)) {
                    f.remove(dst);
                    cost += kJobSmallCost;
                }
                ++pj_.idx;
                return true;
            }
            bool failed = false;
            if (jobCopyUnit(src, dst, cost, &failed)) {
                if (failed) ++pj_.loadErrors;
                ++pj_.idx;
            }
            return true;
        }
        case kSNames: {
            patternNamesPath(src, sizeof(src), kWorkspace);
            patternNamesPath(dst, sizeof(dst), pj_.dir);
            if (pj_.phase == 0 && !f.exists(src)) {
                if (f.exists(dst)) f.remove(dst);
                cost += kJobSmallCost;
                pj_.stage = kSCleanList;
                return true;
            }
            bool failed = false;
            if (jobCopyUnit(src, dst, cost, &failed)) {
                if (failed) ++pj_.loadErrors;
                pj_.stage = kSCleanList;
            }
            return true;
        }
        case kSCleanList: {
            // spec 10 §4.3 step 0 "empty instruments/": stale "NN <old name>.pti"
            // files (renamed or deleted instruments) and legacy instrument_NN.mti
            // headers are removed. SPEC GAP: other files there are kept.
            NameList nl{g_list, int(sizeof(g_list) / sizeof(g_list[0])), 0, false};
            std::snprintf(dst, sizeof(dst), "%s/instruments", pj_.dir);
            f.listDir(dst, collectFn, &nl);
            pj_.listCount = nl.count;
            cost += kJobSmallCost;
            pj_.stage = kSClean;
            pj_.idx = 0;
            return true;
        }
        case kSClean: {
            const int k = pj_.idx++;
            if (k >= pj_.listCount) {
                pj_.stage = kSInstr;
                pj_.idx = 0;
                return true;
            }
            if (!endsWithNoCase(g_list[k], ".pti") && !endsWithNoCase(g_list[k], ".mti")) return true;
            std::snprintf(dst, sizeof(dst), "%s/instruments/%.63s", pj_.dir, g_list[k]);
            f.remove(dst);
            cost += kJobSmallCost;
            return true;
        }
        case kSInstr: {
            const int i = pj_.idx;
            if (i >= kSampleInstrumentCount) {
                pj_.stage = kSFinish;
                return true;
            }
            if (!instrPresent[i]) {
                ++pj_.idx;
                return true;
            }
            workspaceInstrumentPath(src, sizeof(src), kWorkspace, i + 1);
            savedInstrumentPath(dst, sizeof(dst), pj_.dir, i + 1, instruments[i].name[0] ? instruments[i].name : "instrument");
            bool failed = false;
            if (jobCopyUnit(src, dst, cost, &failed)) {
                if (failed) ++pj_.loadErrors;
                ++pj_.idx;
            }
            return true;
        }
        case kSFinish:
            if (pj_.loadErrors) {
                jobFail();
                return false;
            }
            // spec 05 appendix E2: a completed save sets the status flags to 0.
            unsaved_ = false;
            neverSaved_ = false;
            jobEnd();
            return false;
        default:
            jobFail();
            return false;
    }
}

// ---- job starters ----------------------------------------------------------

bool App::startLoadWorkspace(JobDoneFn done, void* ctx) {
    finishJob();
    if (!fs().exists("Workspace/project.mt")) return false;
    pj_ = ProjectJob{};
    pj_.op = JobOp::LoadWorkspace;
    pj_.popup = true;
    pj_.title = "Opening last project";
    std::snprintf(pj_.wavBase, sizeof(pj_.wavBase), "%s", kWorkspace);
    pj_.onDone = done;
    pj_.ctx = ctx;
    estimateJobTotal();
    jobBeginLoad();
    seq.stop();
    allVoicesOff();
    jobProgress();
    return true;
}

bool App::startExternalJob(const char* title, JobUnitFn unit, JobPercentFn percent, void* unitCtx, JobDoneFn done,
                           void* ctx) {
    if (!unit) return false;
    finishJob();
    pj_ = ProjectJob{};
    pj_.op = JobOp::External;
    pj_.popup = true;
    pj_.title = title;
    pj_.extUnit = unit;
    pj_.extPercent = percent;
    pj_.extCtx = unitCtx;
    pj_.onDone = done;
    pj_.ctx = ctx;
    jobProgress();
    return true;
}

bool App::loadWorkspace() {
    if (!startLoadWorkspace()) return false;
    finishJob();
    return lastJobOk_;
}

bool App::startOpenProject(const char* folder, const char* name, JobDoneFn done, void* ctx) {
    // spec 05 appendix E3 OPEN (0x1d2e0): name <= 32 and project.mt must exist.
    if (!name || !name[0] || std::strlen(name) > mt::kNameMax) return false;
    const char* fold = folder && folder[0] ? folder : kProjectsDir;
    if (std::strlen(fold) > mt::kFolderMax) return false;
    finishJob();
    IStorage& f = fs();
    char src[340], dir[sizeof(pj_.dir)];
    std::snprintf(dir, sizeof(dir), "%s/%s", fold, name);
    projectFilePath(src, sizeof(src), dir);
    if (!f.exists(src)) return false;
    // spec 10 §4.2 step 1: load + upgrade the project first. SPEC GAP: done
    // before the Workspace is wiped, so a rejected file (type != 0, version
    // 0) or a project too big for the sample bank leaves the current one.
    Project p;
    if (loadProjectFile(f, src, p) != FmtStatus::Ok) return false;
    if (!projectFitsMemory(*this, fold, name)) return false;
    recentsLeaveCurrent(*this);
    seq.stop();
    allVoicesOff();
    pj_ = ProjectJob{};
    pj_.op = JobOp::Open;
    pj_.popup = true;
    pj_.opened = true;
    pj_.title = "Opening project";
    std::memcpy(pj_.dir, dir, sizeof(dir));
    std::memcpy(pj_.wavBase, dir, sizeof(dir));
    std::snprintf(pj_.wipe, sizeof(pj_.wipe), "%s", kWorkspace);
    pj_.proj = p;
    pj_.proj.setName(name);
    pj_.proj.setFolder(fold);
    pj_.onDone = done;
    pj_.ctx = ctx;
    pj_.stage = kOWipe;
    estimateJobTotal();
    jobProgress();
    return true;
}

bool App::openProject(const char* folder, const char* name) {
    if (!startOpenProject(folder, name)) return false;
    finishJob();
    return lastJobOk_ && lastJobErrors_ == 0;
}

bool App::startAutosave() {
    if (pj_.op != JobOp::None) return false;
    lastAutosave_ = millis();
    bool any = projectDirty_ || namesDirty_;
    for (int i = 0; i < 32 && !any; ++i) any = patDirty_[i] != 0;
    for (int i = 0; i < kSampleInstrumentCount && !any; ++i) any = instrHeaderDirty_[i] || instrSampleDirty_[i];
    // The edited pattern may differ from the store without a dirty mark yet.
    if (!any) {
        stored(patternNumber()) = pattern();
        return false;
    }
    pj_ = ProjectJob{};
    pj_.op = JobOp::Autosave;
    pj_.stage = kABegin;
    return true;
}

void App::autosave() {
    // Blocking flush (power-off, Config, tests): finish whatever runs, then
    // write every dirty item now.
    finishJob();
    if (startAutosave()) finishJob();
    lastAutosave_ = millis();
}

bool App::startSaveProjectAs(const char* nameOrPath, JobDoneFn done, void* ctx) {
    // A plain name saves into the browser root "Projects" (spec 05 appendix
    // E1.2); "<folder>/<name>" (saveProjectInto: the Save browser's current
    // folder; keyboard names cannot contain '/') saves into that folder.
    if (!nameOrPath || !nameOrPath[0]) return false;
    char folder[mt::kFolderMax + 1];
    const char* name = nameOrPath;
    if (const char* s = std::strrchr(nameOrPath, '/')) {
        const size_t fl = size_t(s - nameOrPath);
        if (fl == 0 || fl > mt::kFolderMax) return false;
        std::memcpy(folder, nameOrPath, fl);
        folder[fl] = 0;
        name = s + 1;
    } else {
        std::snprintf(folder, sizeof(folder), "%s", kProjectsDir);
    }
    if (!name[0] || std::strlen(name) > mt::kNameMax) return false;
    finishJob();
    // spec 05 appendix E3: update recents (the project being left), set
    // folder + name, mark the project dirty, flush the Workspace (stage 2),
    // then copy Workspace -> "<folder>/<name>" (stage 4).
    recentsLeaveCurrent(*this);
    project.setName(name);
    project.setFolder(folder);
    projectDirty_ = true;
    pj_ = ProjectJob{};
    pj_.op = JobOp::Save;
    pj_.popup = true;
    pj_.title = "Saving project";
    std::snprintf(pj_.dir, sizeof(pj_.dir), "%s/%s", folder, name);
    pj_.onDone = done;
    pj_.ctx = ctx;
    pj_.stage = kABegin;
    estimateJobTotal();
    jobProgress();
    return true;
}

bool App::saveProjectAs(const char* nameOrPath) {
    if (!startSaveProjectAs(nameOrPath)) return false;
    finishJob();
    return lastJobOk_;
}

bool App::startSaveProject(JobDoneFn done, void* ctx) {
    // spec 05 appendix E1.3 / E3: Save goes to the project's own "<folder>/<name>"
    // (the folder stored in project.mt or the one it was opened from).
    if (neverSaved_ || !project.name[0]) return false;
    return startSaveProjectInto(*this, project.folder[0] ? project.folder : kProjectsDir, project.name, done, ctx);
}

bool App::saveProject() {
    if (!startSaveProject()) return false;
    finishJob();
    return lastJobOk_;
}

bool canDirectSave(App& a) {
    // spec 05 appendix E1.3 + E2: a brand-new project has an empty folder.
    return !a.neverSaved() && a.project.name[0] && a.project.folder[0] &&
           a.projectExists(a.project.folder, a.project.name);
}

bool startSaveProjectInto(App& a, const char* folder, const char* name, void (*done)(void*, bool), void* ctx) {
    if (!name || !name[0] || std::strchr(name, '/')) return false;
    if (!folder || !folder[0]) return a.startSaveProjectAs(name, done, ctx);
    if (std::strlen(folder) > mt::kFolderMax) return false;
    char path[mt::kFolderMax + mt::kNameMax + 2];
    std::snprintf(path, sizeof(path), "%s/%s", folder, name);
    return a.startSaveProjectAs(path, done, ctx);
}

bool saveProjectInto(App& a, const char* folder, const char* name) {
    if (!startSaveProjectInto(a, folder, name, nullptr, nullptr)) return false;
    a.finishJob();
    return a.lastJobOk();
}

// ---------------------------------------------------------------------------
// Memory pre-check, recents, card removal

uint64_t projectSampleBytes(App& a, const char* dir) {
    // spec 02 §8.2 / spec 10 §4.2 step 0: sum the sample data sizes.
    // SPEC GAP: file headers (0x188 / 44 bytes) are not counted.
    static SlotScan s;
    scanSlots(a.fs(), dir, s);
    uint64_t sum = 0;
    for (int i = 0; i < kSampleInstrumentCount; ++i) {
        if (s.hasPti[i]) sum += s.ptiPayload[i];
        else if (s.hasMti[i] && s.hasWav[i]) sum += s.wavBytes[i];
    }
    return sum;
}

bool projectFitsMemory(App& a, const char* folder, const char* name) {
    char dir[mt::kFolderMax + mt::kNameMax + 2];
    std::snprintf(dir, sizeof(dir), "%s/%s", folder && folder[0] ? folder : kProjectsDir, name);
    // SPEC GAP: the stock reaction to an over-budget project is not
    // specified; OpenTracker refuses to open it.
    return projectSampleBytes(a, dir) <= a.bank.capacityBytes();
}

int loadRecents(App& a, Recents& r) {
    loadRecentsFile(a.fs(), kRecentsPath, r);
    int n = 0;
    char p[Recents::kSlotSize + 16];
    for (int i = 0; i < Recents::kSlots; ++i) {
        r.paths[i][Recents::kSlotSize - 1] = 0;
        if (!r.paths[i][0]) continue;
        std::snprintf(p, sizeof(p), "%s/project.mt", r.paths[i]);
        if (!a.fs().exists(p)) continue;   // stale entry removed (0x236e0)
        if (n != i) std::memcpy(r.paths[n], r.paths[i], Recents::kSlotSize);
        ++n;
    }
    for (int i = n; i < Recents::kSlots; ++i) std::memset(r.paths[i], 0, Recents::kSlotSize);
    return n;
}

void recentsLeaveCurrent(App& a) {
    if (!canDirectSave(a)) return;   // only a project that exists on the card
    Recents r;
    loadRecents(a, r);
    char p[mt::kFolderMax + mt::kNameMax + 2];
    std::snprintf(p, sizeof(p), "%s/%s", a.project.folder, a.project.name);
    r.touch(p);   // MRU, de-duplicated, 10 entries
    saveRecentsFile(a.fs(), kRecentsPath, r);
}

bool splitProjectPath(const char* path, char* folder, int folderCap, char* name, int nameCap) {
    if (!path || !path[0]) return false;
    const char* s = std::strrchr(path, '/');
    if (!s) {
        std::snprintf(folder, size_t(folderCap), "%s", "");
        std::snprintf(name, size_t(nameCap), "%s", path);
    } else {
        std::snprintf(folder, size_t(folderCap), "%.*s", int(s - path), path);
        std::snprintf(name, size_t(nameCap), "%s", s + 1);
    }
    return name[0] != 0;
}

bool openRecent(App& a, int index) {
    Recents r;
    const int n = loadRecents(a, r);
    if (index < 0 || index >= n) return false;
    char folder[Recents::kSlotSize], name[Recents::kSlotSize];
    if (!splitProjectPath(r.paths[index], folder, sizeof(folder), name, sizeof(name))) return false;
    return a.openProject(folder, name);
}

bool startOpenRecent(App& a, int index, void (*done)(void*, bool), void* ctx) {
    Recents r;
    const int n = loadRecents(a, r);
    if (index < 0 || index >= n) return false;
    char folder[Recents::kSlotSize], name[Recents::kSlotSize];
    if (!splitProjectPath(r.paths[index], folder, sizeof(folder), name, sizeof(name))) return false;
    return a.startOpenProject(folder, name, done, ctx);
}

namespace {
bool g_cardMissing = false;
bool g_cardProbed = false;
uint32_t g_cardProbeMs = 0;
constexpr uint32_t kCardProbeMs = 500;   // spec 05 §1.7 card-detect debounce
}  // namespace

bool cardMissing() { return g_cardMissing; }

bool serviceCardRemoved(App& a, uint32_t now) {
    if (g_cardProbed && now - g_cardProbeMs < kCardProbeMs) return g_cardMissing;
    g_cardProbed = true;
    g_cardProbeMs = now;
    const bool present = a.fs().listDir("", stopAtFirst, nullptr) >= 0;
    if (present) {
        if (g_cardMissing) {
            // spec 05 §1.5: state 3 -> 2 -> start-up load (reopen the last project).
            g_cardMissing = false;
            a.boot();
            a.ui.dirty();
        }
        return false;
    }
    if (!g_cardMissing) {
        g_cardMissing = true;
        a.abortJob();   // its files are gone with the card
        a.seq.stop();
        a.allVoicesOff();
    }
    if (a.hal().display) {
        Gfx g(*a.hal().display);
        g.beginFrame(0);
        g.text(400, 240, kFontLarge, 0xFFFFFF, "Please insert SD card to continue", IDisplay::kOptCenter);
        g.endFrame();
    }
    return true;
}


// ---------------------------------------------------------------------------
// Instruments

uint32_t App::sampleBytesNeeded(const char* path) {
    IStorage& f = fs();
    if (endsWithNoCase(path, ".pti")) {
        IStorage::Handle h = f.open(path, IStorage::kRead);
        if (h < 0) return 0;
        int32_t sz = f.size(h);
        f.close(h);
        return sz > int32_t(pti::kDataOff) ? uint32_t(sz) - pti::kDataOff : 0;
    }
    IStorage::Handle h = f.open(path, IStorage::kRead);
    if (h < 0) return 0;
    StorageReader r(f, h);
    WavInfo info;
    const bool ok = wavParseHeader(r, info);
    f.close(h);
    if (!ok || !wavIsSupported(info)) return 0;
    return wavResampledFrames(wavSourceFrames(info), info.sampleRate) * 2;
}

bool App::importSample(const char* path, int slot, uint16_t startMarker, uint16_t endMarker, bool lowQuality) {
    if (slot < 0 || slot >= kSampleInstrumentCount) return false;
    finishJob();   // the bank moves under a running instrument write
    IStorage& f = fs();
    allVoicesOff();   // bank moves invalidate voice sample pointers
    bank.remove(slot);
    instrPresent[slot] = false;
    Instrument ins;
    if (endsWithNoCase(path, ".pti")) {
        PtiSampleLayout lay;
        if (loadInstrumentFile(f, path, ins, bank.tail(), uint32_t(bank.freeSamples()), false, &lay) != FmtStatus::Ok) {
            markInstrumentDirty(slot);
            return false;
        }
        bank.commitTail(slot, ins.lengthFrames, ins.stereo);
    } else {
        WavImportOptions opt;
        opt.startMarker = startMarker;
        opt.endMarker = endMarker;
        WavImportResult res;
        if (wavImportFile(f, path, opt, bank.tail(), uint32_t(bank.freeSamples()), res) != FmtStatus::Ok) {
            markInstrumentDirty(slot);
            return false;
        }
        uint32_t frames = res.frames;
        if (lowQuality) {
            // spec 06 §5.5: store at 22.05 kHz, quality byte -12.
            int16_t* d = bank.tail();
            for (uint32_t i = 0; i < frames / 2; ++i) d[i] = int16_t((int32_t(d[2 * i]) + d[2 * i + 1]) / 2);
            frames /= 2;
            ins.reserved173 = uint8_t(int8_t(-12));
        }
        ins.setDefault();
        if (lowQuality) ins.reserved173 = uint8_t(int8_t(-12));
        // Name = file name without extension, max 31 chars (spec 06 §5.4).
        const char* bn = baseName(path);
        size_t n = std::strlen(bn);
        const char* dot = std::strrchr(bn, '.');
        if (dot) n = size_t(dot - bn);
        if (n > size_t(kInstrumentNameLen)) n = kInstrumentNameLen;
        std::memcpy(ins.name, bn, n);
        ins.name[n] = 0;
        ins.lengthFrames = frames;
        bank.commitTail(slot, frames, false);
    }
    ins.lengthFrames = bank.frames(slot);
    instruments[slot] = ins;
    instrPresent[slot] = true;
    markInstrumentDirty(slot, true);
    return true;
}

bool App::deleteInstrument(int slot) {
    if (slot < 0 || slot >= kSampleInstrumentCount) return false;
    finishJob();   // the bank moves under a running instrument write
    allVoicesOff();
    bank.remove(slot);
    instruments[slot].setDefault();
    instrPresent[slot] = false;
    markInstrumentDirty(slot, true);
    return true;
}

bool App::copyInstrument(int src, int dst) {
    if (src < 0 || dst < 0 || src >= kSampleInstrumentCount || dst >= kSampleInstrumentCount || src == dst) return false;
    finishJob();   // the bank moves under a running instrument write
    if (!instrPresent[src]) return false;
    allVoicesOff();
    if (!bank.copy(src, dst)) return false;
    instruments[dst] = instruments[src];
    instrPresent[dst] = true;
    markInstrumentDirty(dst, true);
    return true;
}

void App::renameInstrument(int slot, const char* name) {
    if (slot < 0 || slot >= kSampleInstrumentCount || !name) return;
    std::snprintf(instruments[slot].name, sizeof(instruments[slot].name), "%s", name);
    markInstrumentDirty(slot);
}

}  // namespace ot
