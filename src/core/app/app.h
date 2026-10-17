// OpenTracker application object: owns the project model, sequencer, audio
// engine (spec 04), storage/workspace handling and the UI (spec 05 §1.1-§1.5,
// spec 02 §1.1).
#pragma once

#include <cstdint>

#include "app/capture.h"
#include "app/config.h"
#include "app/midi_cc.h"
#include "app/pattern_ops.h"
#include "app/project_io.h"
#include "app/undo.h"
#include "audio/engine.h"
#include "hal_api.h"
#include "model/instrument.h"
#include "model/pattern.h"
#include "model/project.h"
#include "sequencer/sequencer.h"
#include "ui/notes.h"
#include "ui/ui.h"

namespace ot {

// Interfaces handed to the app (mirrors hal::HalSet without depending on src/hal).
struct HalRefs {
    IDisplay* display = nullptr;
    IAudioOut* audio = nullptr;
    IStorage* storage = nullptr;
    IMidiPort* midi = nullptr;
    IInput* input = nullptr;
    ILeds* leds = nullptr;
    IClock* clock = nullptr;
    void (*service)() = nullptr;
    // Firmware update hand-off (injected by the device glue; null on host).
    bool (*isUpdateImageName)(const char* name) = nullptr;
    bool (*requestFirmwareUpdate)(IStorage& fs, const char* name) = nullptr;
    // Line input for the next audio block (128 interleaved frames) or nullptr;
    // called from the audio-block context. Null when the board has no capture.
    const int16_t* (*lineIn)(void* ctx) = nullptr;
    void* lineInCtx = nullptr;
    // Recorder input routing (spec 05 appendix C §2.4/§2.5): source 0 line
    // in, 1 radio, 2 mic low gain, 3 mic high gain; gain 0..100. Returns
    // false when the source is not available. Null: only line in (no
    // hardware gain; OpenTracker applies the gain digitally either way).
    bool (*selectInput)(void* ctx, uint8_t source, uint8_t gain) = nullptr;
    void* inputCtx = nullptr;
    IRadio* radio = nullptr;               // null = no tuner driver
    // Global config image store (spec 05 §5.4 EEPROM). Null: the image is
    // kept in kConfigPath on the card instead.
    INvStore* nvstore = nullptr;
};

// Large buffers (SDRAM on the device, spec 02 §8.2 for the sample bank).
struct AppMemory {
    int16_t* sampleBank = nullptr;
    uint32_t sampleBankSamples = 0;
    int16_t* fxMemory = nullptr;          // delay lines + reverb rings (spec 04 §5.1, §5.2)
    uint32_t fxSamples = 0;               // >= audio::AudioEngine::kFxMemorySamples
    Pattern* patterns = nullptr;          // kPatternCount entries; pattern n at [n-1]
    Pattern* undo = nullptr;              // undoSlots entries (spec 05 §5.1: 20)
    uint8_t* undoNums = nullptr;
    int undoSlots = 0;
    Pattern* fileUndo = nullptr;          // spec 05 §5.1 (b): 10
    uint8_t* fileUndoNums = nullptr;
    int fileUndoSlots = 0;
    Pattern* clipboard = nullptr;
    uint8_t* scratch = nullptr;           // file copy / decode buffer
    uint32_t scratchBytes = 0;
    PerfPatternMemory* perfPatterns = nullptr;   // per-track pattern overrides (spec 12 §C.1)
};

constexpr const char* kWorkspace = "Workspace";
constexpr const char* kProjectsDir = "Projects";
constexpr const char* kExportDir = "Export";

class App final : public ISequencerHost {
public:
    App();
    bool init(const HalRefs& hal, const AppMemory& mem);
    // Mount check, folder layout, config, workspace load or new project.
    bool boot();
    void loop();          // one main-loop pass (spec 05 §1.3 application loop)
    [[noreturn]] void run();
    uint32_t millis() const { return hal_.clock ? hal_.clock->millis() : 0; }
    uint32_t micros() const { return hal_.clock ? hal_.clock->micros() : 0; }

    // Audio: the engine is the IAudioOut callback; at the start of every block
    // it calls preBlock (spec 04 §1.1 step 1), which applies pending mixer
    // changes, hands over line-in and advances the sequencer.
    static void preBlockThunk(void* ctx, float seconds);
    void preBlock(float seconds);
    // Mixer / master / track settings from project + config -> engine.
    void applyEngineSettings();

    // Audition (spec 07 Q7, spec 05 §4.2 pad manager): every held source
    // (voiceId = pad number, or one of the ids below) takes a track voice:
    // starting at the pattern-editor cursor track, scanning 12 tracks with
    // wrap, the first track that is not muted, holds no other audition note
    // and (sample instruments) is < 8. Sample instruments release the track's
    // note and start the audition note on the track voice (up to 8 at once);
    // MIDI instruments (>= 48) send a note on their channel. Release = normal
    // envelope release (note-off mode -4). forceTrack >= 0 plays on that
    // track only (pattern editor note entry in edit mode).
    static constexpr uint16_t kAuditionLoader = 0x60;        // Sample Loader F3 (07 Q7)
    static constexpr uint16_t kAuditionInstrument = 0x61;    // Instrument screen preview
    static constexpr uint16_t kAuditionEncoder = 0x62;       // pattern editor note encoder
    static constexpr uint16_t kAuditionSampleEditor = 0x63;  // sample editor preview
    static constexpr uint16_t kAuditionMidiIn = 0x100;       // + MIDI note number
    static constexpr uint16_t kAuditionNone = 0xFFFF;
    // Returns the track used, or -1 when no track was free.
    // startTrack >= 0 starts the search there instead of editorTrack (MIDI
    // input: the pattern-editor target rectangle, spec 17 §1.4 step 4).
    int previewOn(uint16_t voiceId, uint8_t instrument, uint8_t note, uint8_t velocity, int forceTrack = -1,
                  int startTrack = -1);
    void previewOff(uint16_t voiceId, int startTrack = -1);
    // Live velocity (spec 17 §1.4 step 1): 127 unless Recording options is a
    // Velocity mode. Live volume vel * IV / 127 (step 7, FW:0x1191c).
    uint8_t liveVelocity(uint8_t velocity) const;
    uint8_t liveVolume(uint8_t instrument, uint8_t vel) const;
    void previewAllOff();
    // Track holding audition voiceId, or -1.
    int auditionTrack(uint16_t voiceId, int from = -1) const;
    // Pattern-editor cursor track: start of the audition track search.
    uint8_t editorTrack = 0;
    // Sample loader file preview (spec 04 §1.3 WAVPREVIEW); null stops it.
    void previewFile(const int16_t* data, uint32_t frames, bool stereo) { engine.previewBuffer(data, frames, stereo); }
    // Stop every voice (also before any sample-bank move: voices point into it).
    void allVoicesOff();

    // ---- model -----------------------------------------------------------
    Project project;
    PatternNames names;
    Instrument instruments[kSampleInstrumentCount];
    bool instrPresent[kSampleInstrumentCount] = {};
    audio::AudioEngine engine;
    audio::SampleBank& bank;          // engine.bank(): spec 02 §8.2 packing
    Config cfg;
    Sequencer seq;
    PadNotes padNotes;
    UndoRing undo;
    FileUndo fileUndo;
    Ui ui;

    IStorage& fs() { return *hal_.storage; }
    const HalRefs& hal() const { return hal_; }
    const AppMemory& mem() const { return mem_; }

    // ---- patterns ----------------------------------------------------------
    Pattern& pattern() { return *seq.currentPattern(); }
    uint8_t patternNumber() const { return seq.currentPatternNumber(); }
    Pattern& stored(int num) { return mem_.patterns[num - 1]; }
    bool patternPresent(int num) const { return num >= 1 && num <= kPatternCount && (present_[num >> 3] >> (num & 7)) & 1; }
    bool patternHasData(int num);
    // After editing the current pattern: copy into the store and mark it dirty.
    void patternEdited();
    void markPatternDirty(int num);
    // Change the current (edited) pattern; queues while playing (spec 06 §1.7 focus 0).
    void selectPattern(int num);
    // Pattern + Up/Down (spec 16 §A.2, FW:0x3d548): change the current pattern
    // at once, also while playing (step position kept).
    void switchPatternNow(int num);
    // The pattern the editor shows next: a pending immediate switch, else current.
    int effectivePatternNumber() const { return seq.pendingSwitch() ? seq.pendingSwitch() : patternNumber(); }
    int firstEmptyPattern();
    void reloadCurrentFromStore();

    // Undo (spec 05 §5.1).
    void pushUndo();
    bool undoEdit();
    bool redoEdit();

    // Clipboard (spec 06 §1.4 Copy / §1.11).
    void copyToClipboard(const SelRect& r);
    bool clipboardValid() const { return clipValid_; }
    const SelRect& clipRect() const { return clipRect_; }
    const Pattern& clipboard() const { return *mem_.clipboard; }

    // ---- project settings shortcuts ---------------------------------------
    uint8_t currentInstrument() const { return project.u8(0x111); }
    void setCurrentInstrument(int i);
    uint8_t scale() const { return project.u8(0x115); }
    uint8_t rootNote() const { return project.u8(0x117); }
    uint8_t stepJump() const;
    void setStepJump(int v);
    bool trackMuted(int t) const;
    void setTrackMute(int t, bool m);
    uint8_t padNote(uint8_t pad);                 // current scale map, slice-aware
    void refreshPadNotes();
    void applySequencerSettings();

    // ---- dirty state / autosave (spec 02 §1.1) -----------------------------
    void markProjectDirty() { projectDirty_ = true; unsaved_ = true; engineDirty_ = true; }
    void markNamesDirty() { namesDirty_ = true; unsaved_ = true; }
    void markInstrumentDirty(int i, bool sample = false);
    void markConfigDirty() { configDirty_ = true; engineDirty_ = true; }
    // Config persistence (spec 05 §5.4): the dirty config is written at the
    // next 5 s boundary; force = on the next loop pass (spec 12 §A.5 leave).
    void requestConfigSave(bool force) { markConfigDirty(); if (force) configForce_ = true; }
    bool saveConfig();                            // INvStore, else kConfigPath
    void loadConfig();                            // boot: EEPROM, else import the file once
    void setNvStore(INvStore* nv) { hal_.nvstore = nv; }
    // Recorder settings live in the project (0x134..0x13C, spec 02 §3.3);
    // cfg.rec* are the working copies. Called every loop pass.
    void syncRecorderSettings();
    bool unsavedChanges() const { return unsaved_; }
    bool neverSaved() const { return neverSaved_; }
    void autosave();                              // writes dirty items to Workspace now
    uint32_t lastAutosave() const { return lastAutosave_; }

    // ---- project operations (project_io.cpp) ------------------------------
    // The blocking calls below run the same background job as the start*()
    // variants to completion (power-off, boot, tests).
    bool newProject();
    bool loadWorkspace();
    bool saveProject();                           // to <folder>/<name>
    bool saveProjectAs(const char* name);
    bool openProject(const char* folder, const char* name);
    // Background jobs (spec 05 appendix E3): false = not started. `done`
    // runs from loop() once the job has finished.
    using JobDoneFn = void (*)(void* ctx, bool ok);
    bool startOpenProject(const char* folder, const char* name, JobDoneFn done = nullptr, void* ctx = nullptr);
    bool startSaveProjectAs(const char* nameOrPath, JobDoneFn done = nullptr, void* ctx = nullptr);
    bool startSaveProject(JobDoneFn done = nullptr, void* ctx = nullptr);
    bool startLoadWorkspace(JobDoneFn done = nullptr, void* ctx = nullptr);
    bool startAutosave();                         // false when nothing is dirty
    // Games (spec 14 §1.6, OpenTracker): true while the sample memory is lent
    // to the NES emulator; the periodic autosave is held so garbage sample
    // data is never written. The lender reloads the Workspace afterwards.
    bool sampleBankLent = false;
    // External background job with the progress popup `title` (spec 11
    // §0.1 import / export state machines); false when another job runs.
    bool startExternalJob(const char* title, JobUnitFn unit, JobPercentFn percent, void* unitCtx,
                          JobDoneFn done = nullptr, void* ctx = nullptr);
    JobOp jobOp() const { return pj_.op; }
    bool jobBusy() const { return pj_.op != JobOp::None; }
    int jobPercent() const;
    bool lastJobOk() const { return lastJobOk_; }
    // Files that existed but could not be read/written by the last job.
    int lastJobErrors() const { return lastJobErrors_; }
    // One bounded step (called by loop()); true while a job is still running.
    bool serviceProjectJob();
    // Run the current job to completion (spec 05 appendix E3: every project
    // API call first flushes an in-progress autosave).
    void finishJob();
    // Card removed: drop the job (open handles closed, popup removed).
    void abortJob();
    bool projectExists(const char* folder, const char* name);
    bool removeTree(const char* path);
    bool copyFile(const char* from, const char* to);

    // ---- instruments (project_io.cpp) -------------------------------------
    // Import a .wav (resampled) or .pti into a sample slot. Markers 0..65535.
    bool importSample(const char* path, int slot, uint16_t startMarker = 0, uint16_t endMarker = 65535,
                      bool lowQuality = false);
    bool deleteInstrument(int slot);
    bool copyInstrument(int src, int dst);
    void renameInstrument(int slot, const char* name);
    uint32_t sampleBytesNeeded(const char* path);  // projected memory of a file (bytes)

    // ---- MIDI CC input (midi_cc.h; spec 12 §B, spec 05 appendix C §8) ----
    // One accepted Control Change (after the Config "CC in" port/channel
    // filter): Performance (screen active), master levels, instrument map.
    void handleControlChange(uint8_t cc, uint8_t value);
    // Performance screen hook for CC 41-48 / 51-62 (task B4); null = ignore.
    PerformanceCcSink* perfCc = nullptr;
    // Instrument chosen in the MIDI Mapping list (appendix C §1 +0x335,
    // 0..47): the CC target while the current instrument is a MIDI one.
    uint8_t ccMapInstrument = 0;
    // Sample instrument the instrument CC map edits (appendix C §8).
    uint8_t ccTargetInstrument() const;

    // ---- export / render (render.cpp) -------------------------------------
    // Offline render through the engine (spec 04 §6.1). stems: `path` is a
    // folder receiving track1..8/reverb/delay/master.wav.
    enum class ExportKind : uint8_t { Song, Pattern, Selection };
    bool exportAudio(ExportKind kind, const char* path, const SelRect* sel = nullptr, bool stems = false);
    // First free Export/<proj>/... name (.wav file, or a folder for stems).
    bool exportName(ExportKind kind, char* out, int cap, bool stems = false);
    bool rendering() const { return rendering_; }

    // ---- transport ----------------------------------------------------------
    void togglePlay(bool shift);
    // Pattern `num` is in the song playlist.
    bool patternInSong(int num) const;
    uint32_t playStartMs() const { return playStart_; }

    // ---- ISequencerHost ---------------------------------------------------
    void loadPattern(uint8_t number, Pattern& dst, bool sync) override;
    void onStepChanged() override { stepChanged_ = true; }
    void onPatternEnd() override { ++patternEnds_; }
    uint32_t patternEndCount() const { return patternEnds_; }
    bool noteInScale(uint8_t note) override;
    uint8_t instrumentVolume(uint8_t instrument) override;
    void markPatternDirty() override { patternEditedFromSeq_ = true; }
    bool songScreenActive() override { return ui.active() == kModSong; }

    // Paths
    void workspacePath(char* out, int cap, const char* rel) const;
    void projectPath(char* out, int cap) const;   // <folder>/<name>

    // Live recording state used by the pattern editor.
    bool editMode = false;

    // ---- sample recorder (recorder.cpp; spec 05 §2.11 + appendix C §2) ----
    Capture capture;
    volatile bool inputMonitorMuted = false;
    bool selectInput(uint8_t source, uint8_t gain);   // false = source not available
    IRadio* radio() const { return hal_.radio; }
    // Output folder "Recordings" / "Recordings N" (created) and the first
    // free "recordingNNN" name in it (spec 02 §8.3, appendix C §2.12).
    bool recordingsFolder(char* out, int cap);
    bool recordingName(const char* folder, char* out, int cap);
    int firstFreeSampleSlot() const;
    // Write mono frames as a 16-bit 44.1 kHz WAV (spec 02 §8.3).
    bool writeWav(const char* path, const int16_t* mono, uint32_t frames);

    // ---- screenshot (screenshot.cpp; spec 05 §3.4) -------------------------
    // Writes the first free Snapshots/Snapshot_NNN.bmp; false if the display
    // cannot read back or the card write fails.
    bool screenshot(char* pathOut = nullptr, int cap = 0);

    // ---- File > Export > Samples (render.cpp; spec 05 appendix E1.4) --------
    // Every used sample instrument to <folder>/<name>/export/samples/; returns
    // the number of files written, < 0 on error.
    int exportSamples();

private:
    void serviceMidi();
    void midiByte(MidiPortId port, uint8_t b);
    void handleMidiMessage(uint8_t st, uint8_t d1, uint8_t d2);
    // Project job internals (project_io.cpp).
    bool jobUnit(uint32_t& cost);
    void jobEnd();
    void jobFail();
    void jobCloseFiles();
    bool jobCopyUnit(const char* from, const char* to, uint32_t& cost, bool* failed);
    bool jobPtiWriteUnit(int slot, const char* path, uint32_t& cost, bool* failed);
    bool jobSamplesUnit(int slot, uint32_t& cost);
    int jobWipeUnit(uint32_t& cost);
    void jobBeginLoad();
    void jobProgress();
    void estimateJobTotal();
    void setPresent(int num, bool on);
    void attachSequencer();
    void defaultPattern(Pattern& p) const;
    static bool exportRunning(void* ctx);

    HalRefs hal_;
    AppMemory mem_;
    Pattern bufA_, bufB_;
    uint8_t present_[32] = {};
    uint8_t patDirty_[32] = {};
    bool projectDirty_ = false, namesDirty_ = false, configDirty_ = false, configForce_ = false;
    uint8_t recShadow_[12] = {};                  // project bytes 0x134..0x13F last synced
    bool recShadowValid_ = false;
    bool instrHeaderDirty_[kSampleInstrumentCount] = {};
    bool instrSampleDirty_[kSampleInstrumentCount] = {};
    bool unsaved_ = false, neverSaved_ = true;
    bool clipValid_ = false;
    SelRect clipRect_;
    uint32_t lastAutosave_ = 0;
    uint32_t lastConfigSave_ = 0;
    volatile bool rendering_ = false;
    volatile bool engineDirty_ = true;
    // Export state machine (render.cpp).
    struct ExportJob {
        ExportKind kind = ExportKind::Pattern;
        const SelRect* sel = nullptr;
        bool started = false;
        uint32_t blocks = 0, tail = 0;
        int songLen = 0;
    } job_;
    ProjectJob pj_;                 // background project operation (project_io.cpp)
    bool lastJobOk_ = true;
    int lastJobErrors_ = 0;
    uint32_t jobProgressMs_ = 0;
    volatile bool stepChanged_ = false;
    volatile uint32_t patternEnds_ = 0;
    volatile bool patternEditedFromSeq_ = false;
    bool booted_ = false;
    bool wasPlaying_ = false;
    uint32_t playStart_ = 0;
    uint32_t lastNoCardTry_ = 0;
    // MIDI input parser per port.
    struct MidiIn {
        uint8_t status = 0, data[2] = {}, count = 0;
    } midiIn_[2];
    // Per-track audition record (spec 07 Q7: track record +0x60/+0x61/+0x78/+0x7C).
    struct Audition {
        bool busy = false;
        bool midi = false;
        uint16_t id = kAuditionNone;
        uint8_t instrument = 0, note = 0, ch = 0;
    } aud_[kTrackCount];
    void releaseAudition(int track);
};

}  // namespace ot
