// App side of the Sample Recorder (spec 05 §2.11, appendix C §2; spec 02
// §8.3 recordings): input routing, output folder and file naming, WAV write.
#include <cstdio>
#include <cstring>

#include "app/app.h"
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

}  // namespace ot
