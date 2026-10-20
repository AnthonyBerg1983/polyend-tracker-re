// App side of the Sample Recorder (spec 05 §2.11, appendix C §2; spec 02
// §8.3 recordings): input routing, output folder and file naming, WAV write.
#include <cstdio>
#include <cstring>

#include "app/app.h"
#include "fmath.h"
#include "formats/wav.h"

namespace ot {

bool App::selectInput(uint8_t source, uint8_t gain) {
    if (source > 3) return false;
    if (source == 1 && !hal_.radio) return false;   // radio: needs a tuner driver
    capture.setChannelMode(cfg.lineInChannel);
    if (hal_.selectInput) return hal_.selectInput(hal_.inputCtx, source, gain);
    return source == 0;   // without a routing hook only the line input exists
}

namespace {
bool countEntry(const IStorage::DirEntry&, void*) { return true; }
}  // namespace

bool App::recordingsFolder(char* out, int cap) {
    IStorage& f = fs();
    // "Recordings", then the highest existing "Recordings N" (N >= 2).
    int n = 1;
    for (int k = 2; k < 1000; ++k) {
        char p[32];
        std::snprintf(p, sizeof(p), "Recordings %d", k);
        if (!f.exists(p)) break;
        n = k;
    }
    auto name = [&](int k) {
        if (k <= 1) std::snprintf(out, size_t(cap), "Recordings");
        else std::snprintf(out, size_t(cap), "Recordings %d", k);
    };
    name(n);
    if (f.exists(out)) {
        // More than 98 entries: start the next folder (spec 02 §8.3).
        const int32_t entries = f.listDir(out, &countEntry, nullptr);
        if (entries > 98) name(n + 1);
    }
    return f.mkdir(out);
}

bool App::recordingName(const char* folder, char* out, int cap) {
    for (int k = 1; k <= 999; ++k) {
        char p[96];
        std::snprintf(p, sizeof(p), "%s/recording%03d.wav", folder, k);
        if (!fs().exists(p)) {
            std::snprintf(out, size_t(cap), "recording%03d", k);
            return true;
        }
    }
    return false;
}

int App::firstFreeSampleSlot() const {
    for (int i = 0; i < kSampleInstrumentCount; ++i)
        if (!instrPresent[i]) return i;
    return -1;
}

bool App::writeWav(const char* path, const int16_t* mono, uint32_t frames) {
    IStorage& f = fs();
    if (f.exists(path)) f.remove(path);
    WavWriter w;
    if (!w.open(f, path, 1)) return false;
    bool ok = true;
    for (uint32_t i = 0; i < frames && ok; i += 4096) {
        const uint32_t n = frames - i < 4096 ? frames - i : 4096;
        ok = w.write(mono + i, n);
    }
    return w.close() && ok;
}

bool App::writeWav(const char* path, const int16_t* l, const int16_t* r, uint32_t frames) {
    if (!r) return writeWav(path, l, frames);
    IStorage& f = fs();
    if (f.exists(path)) f.remove(path);
    WavWriter w;
    if (!w.open(f, path, 2)) return false;
    // 2.x stereo recording: the planar lanes interleaved (L, R) through a
    // small transient chunk (2 KB of stack, no sample-memory use).
    constexpr uint32_t kChunk = 512;
    int16_t buf[2 * kChunk];
    bool ok = true;
    for (uint32_t i = 0; i < frames && ok; i += kChunk) {
        const uint32_t n = frames - i < kChunk ? frames - i : kChunk;
        for (uint32_t k = 0; k < n; ++k) {
            buf[2 * k] = l[i + k];
            buf[2 * k + 1] = r[i + k];
        }
        ok = w.write(buf, n);
    }
    return w.close() && ok;
}

uint32_t App::previewStep(int note) {
    // 2^((note - 60) / 12) as 16.16: 2048 (note 0) .. ~3.1e6 (note 127).
    const int n = note < 0 ? 0 : (note > 127 ? 127 : note);
    return uint32_t(fm::kNoteRatio[n] * 65536.0 + 0.5);
}

}  // namespace ot
