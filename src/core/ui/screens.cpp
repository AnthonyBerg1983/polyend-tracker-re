#include "ui/screens.h"

#include "app/app.h"
#include "ui/screen_games.h"

namespace ot {

namespace {
// All screens in one object, created once per Ui (startup only, not in the audio path).
struct ScreenSet {
    ScreenSet(Ui& u, App& a)
        : file(u, a), loader(u, a), pattern(u, a), playback(u, a), song(u, a), instrument(u, a), editor(u, a),
          config(u, a), recorder(u, a), performance(u, a), mixer(u, a), games(u, a) {}
    FileScreen file;
    LoaderScreen loader;
    PatternScreen pattern;
    PlaybackScreen playback;
    SongScreen song;
    InstrumentScreen instrument;
    EditorScreen editor;
    ConfigScreen config;
    RecorderScreen recorder;
    PerformanceScreen performance;
    MixerScreen mixer;
    GamesScreen games;
};
}  // namespace

void* createScreens(Ui& ui, App& app, Screen** out) {
    auto* s = new ScreenSet(ui, app);
    out[kModFile] = &s->file;
    out[kModLoader] = &s->loader;
    out[kModPattern] = &s->pattern;
    out[kModPlayback] = &s->playback;
    out[kModSong] = &s->song;
    out[kModInstrument] = &s->instrument;
    out[kModEditor] = &s->editor;
    out[kModConfig] = &s->config;
    out[kModRecorder] = &s->recorder;
    out[kModPerformance] = &s->performance;
    out[kModMaster] = &s->mixer;
    out[kModGames] = &s->games;
    return s;
}

void destroyScreens(void* owner, Screen** screens) {
    if (!owner) return;
    delete static_cast<ScreenSet*>(owner);
    for (int i = 0; i < kModCount; ++i) screens[i] = nullptr;
}

}  // namespace ot
