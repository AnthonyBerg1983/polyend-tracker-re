// Project save/open helpers on top of App (spec 05 appendix E1.3, E2, E3;
// spec 02 §1.1, §7, §8.2; spec 10 §4).
#pragma once

#include <cstdint>

#include "formats/wav.h"
#include "hal_api.h"
#include "model/instrument.h"
#include "model/project.h"

namespace ot {

class App;
struct Recents;

// ---- background project operations (spec 05 appendix E3/E4) -------------
// Stock runs open / save / workspace load / autosave as the asynchronous
// project-manager state machine 0x1d5e0: one bounded step per main-loop pass
// while a progress popup is shown (spec 05 §1.3 "progress popup"), so the UI,
// the audio and the sequencer clock keep running. OpenTracker does the same:
// App::serviceProjectJob() runs at most kJobPassBytes of file work (one small
// file, or one kJobChunk-sized piece of a large one, per unit) and at most
// kJobPassUs of wall time per pass.
enum class JobOp : uint8_t { None = 0, LoadWorkspace, Open, Save, Autosave, External };

// External job (File > Import Mod / Export As .it, spec 11 §0.1): unit()
// does one bounded piece of work and adds its byte cost; it returns 1 while
// more work remains, 0 when finished, -1 on failure. percent() feeds the
// progress popup.
using JobUnitFn = int (*)(void* ctx, uint32_t& cost);
using JobPercentFn = int (*)(void* ctx);

constexpr uint32_t kJobChunk = 0x4000;        // bytes per chunked read/write unit
constexpr uint32_t kJobPassBytes = 0x10000;   // work per main-loop pass
constexpr uint32_t kJobPassUs = 8000;         // wall-time budget per pass
constexpr uint32_t kJobSmallCost = 0x800;     // a small-file operation
constexpr uint32_t kJobProbeCost = 0x40;      // an existence check of a missing file

struct ProjectJob {
    JobOp op = JobOp::None;
    uint8_t stage = 0;
    int idx = 0;                   // item index inside the stage
    uint8_t phase = 0;             // sub-step of a chunked item (0 = not started)
    uint8_t wphase = 0;            // sub-step of a chunked .pti write
    bool ok = true;
    bool popup = false;            // progress popup shown (not for autosave)
    bool opened = false;           // Open: status flags := 0 at the end (E2)
    bool sizeMismatch = false;     // legacy Workspace project.mt (spec 10 §1.3)
    int loadErrors = 0;            // files that exist but could not be read
    const char* title = "";
    char dir[mt::kFolderMax + mt::kNameMax + 2] = {};   // open source / save target
    char wavBase[mt::kFolderMax + mt::kNameMax + 2] = {};   // legacy samples/instrNN.wav root
    char wipe[300] = {};           // current folder of the incremental Workspace wipe
    Project proj;                  // Open: the upgraded project to write (spec 10 §4.2 step 2)
    uint8_t kind[kSampleInstrumentCount] = {};      // 0 none, 1 .pti payload, 2 legacy WAV
    uint32_t fileSize[kSampleInstrumentCount] = {};
    uint32_t dataOff[kSampleInstrumentCount] = {};
    bool copied[kSampleInstrumentCount] = {};
    uint8_t modes[kSampleInstrumentCount] = {};
    int listCount = 0;
    // Chunked item state.
    IStorage::Handle in = -1, out = -1;
    uint32_t pos = 0, len = 0;
    const int16_t* src = nullptr;  // autosave/import: samples being written
    uint32_t srcFrames = 0;
    bool srcStereo = false;
    bool sampleDirty = false;
    WavImporter wav;
    Instrument ins;
    // Progress (bytes of work, estimated at the start).
    uint64_t done = 0, total = 1;
    // JobOp::External callbacks.
    JobUnitFn extUnit = nullptr;
    JobPercentFn extPercent = nullptr;
    void* extCtx = nullptr;
    // Called once when the job ends (after the job state is cleared).
    void (*onDone)(void* ctx, bool ok) = nullptr;
    void* ctx = nullptr;
};

constexpr const char* kRecentsPath = "Projects/recents";

// spec 05 appendix E1.3: Save writes straight into "<folder>/<name>" only when
// the project is not flagged never-saved AND "<folder>/<name>/project.mt"
// exists; otherwise Save behaves like Save As.
bool canDirectSave(App& a);

// spec 05 appendix E3 SAVE AS (0x1d428 with a (folder, name) pair): set folder
// and name, flush the Workspace and copy it into "<folder>/<name>".
// App::saveProjectAs(name) = this with the browser root "Projects";
// App::saveProject() = this with the project's own folder and name.
bool saveProjectInto(App& a, const char* folder, const char* name);
// Background variant (File screen): returns false if it cannot start; else
// `done(ctx, ok)` runs when the save has finished.
bool startSaveProjectInto(App& a, const char* folder, const char* name, void (*done)(void*, bool), void* ctx);
inline bool saveProjectAs(App& a, const char* folder, const char* name) { return saveProjectInto(a, folder, name); }

// ---- memory pre-check (spec 02 §1.1 / §8.2, spec 10 §4.2 step 0) --------
// Sample bytes the saved project "<dir>" (= "<folder>/<name>") needs: the
// payload of each "<N> <name>.pti", or for a legacy slot (no .pti for N but
// instrument_NN.mti) the data of samples/instrNN.wav.
uint64_t projectSampleBytes(App& a, const char* dir);
// True when that sum fits the sample bank (0xB40000 bytes on the device).
bool projectFitsMemory(App& a, const char* folder, const char* name);

// ---- Projects/recents (spec 02 §7, spec 05 appendix E2) ------------------
// Load and compact: entries whose "<path>/project.mt" no longer exists are
// removed (0x236e0). Returns the number of valid entries (slots 0..n-1).
int loadRecents(App& a, Recents& r);
// When a project is left (new / open / save as): the CURRENT project's path,
// if it exists on the card, moves to the top; file rewritten (0x23784).
void recentsLeaveCurrent(App& a);
// Open Recent entry `index` (0x238fc). False if it is gone or fails.
bool openRecent(App& a, int index);
bool startOpenRecent(App& a, int index, void (*done)(void*, bool), void* ctx);
// Split a recents path into folder and name.
bool splitProjectPath(const char* path, char* folder, int folderCap, char* name, int nameCap);

// ---- SD card removed while running (spec 05 §1.5 state 3, appendix A) ----
// Call once per main-loop pass before input/UI. Probes the card every
// 500 ms (spec 05 §1.7 card-detect debounce); while it is missing the
// sequencer is stopped and "Please insert SD card to continue" is shown, and
// true is returned (the caller skips the rest of the pass). When the card
// comes back the last project (Workspace) is reopened like at boot.
bool serviceCardRemoved(App& a, uint32_t now);
bool cardMissing();

}  // namespace ot
