// Host benchmark for the audio engine render path (not a unit test).
//
//   make -C tests bench        host build (-O2, no sanitizers), wall-clock time
//   make -C tests bench-m4     the same code built with the device flags
//                              (cortex-m4, -O2, hard float) and run under
//                              qemu-system-arm -M mps2-an386 -icount shift=0,
//                              which counts executed M4 instructions
//                              (tests/bench_m4/: startup, linker script)
//
// Renders scenarios through AudioEngine::renderLive() -- the same entry the
// device's I2S DMA ISR calls through fillCallback() -- and reports host time
// per 128-frame block, the share of the 2901 us block period, and a rough
// device estimate (see deviceEstimate below).
//
// Scenarios:
//   idle      stopped, no voices, effect tails finished, line input of zeros
//             (the device feeds the codec input every block)
//   play8     8 looping voices, delay + reverb sends, some filters, 1 LFO
//   worst     8 voices + metronome + file preview: granular / wavetable /
//             ping-pong, filter + overdrive + bit crusher on all, 5 LFOs,
//             saturation, side-chain limiter, line-in with sends
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#ifndef OT_BENCH_M4
#include <chrono>
#endif

#include "audio/engine.h"

using namespace ot;
using namespace ot::audio;

namespace ot {
namespace audio {
uint32_t gSdramAccesses = 0;   // counted by the engine (OT_AUDIO_SDRAM_STATS)
}
}  // namespace ot

namespace {

constexpr float kPi = 3.14159265f;

#ifdef OT_BENCH_M4
// QEMU mps2-an386: CMSDK APB timer 0 at 25 MHz; with -icount shift=0 one
// instruction is 1 ns of virtual time, so one timer tick = 40 instructions.
// Large buffers live in the board's PSRAM (section .psram, tests/bench_m4).
#define OT_BIG __attribute__((section(".psram")))
volatile uint32_t* const kTimer = reinterpret_cast<volatile uint32_t*>(0x40000000u);
void clockStart() {
    kTimer[0] = 0;
    kTimer[2] = 0xFFFFFFFFu;   // RELOAD
    kTimer[1] = 0xFFFFFFFFu;   // VALUE
    kTimer[0] = 1;             // enable
}
double nowUnits() { return double(0xFFFFFFFFu - kTimer[1]) * 40.0; }   // instructions
constexpr const char* kUnit = "M4 insn";
constexpr int kReps = 1;
constexpr int kScale = 8;   // fewer blocks under emulation
#else
#define OT_BIG
void clockStart() {}
double nowUnits() {
    return double(std::chrono::duration_cast<std::chrono::nanoseconds>(
                      std::chrono::steady_clock::now().time_since_epoch()).count()) / 1000.0;
}
constexpr const char* kUnit = "us";
constexpr int kReps = 5;
constexpr int kScale = 1;
#endif

OT_BIG int16_t gFx[AudioEngine::kFxMemorySamples];
OT_BIG int16_t gPool[2u << 20];
Instrument gIns[kSampleInstrumentCount];
AudioEngine gEng;

struct Rig {
    Instrument* ins = gIns;
    AudioEngine* eng = &gEng;
    Rig() {
        for (int i = 0; i < kSampleInstrumentCount; ++i) gIns[i] = Instrument();
        eng->settings() = MixerSettings();
        eng->setLineInput(nullptr);
        eng->init(gFx, AudioEngine::kFxMemorySamples, gPool, sizeof(gPool) / 2, gIns);
        eng->clearEffects();
        eng->stopAll();
        for (int t = 0; t < kVoices; ++t) eng->voice(t).resetFx();
    }
    void load(int i, const std::vector<int16_t>& d, PlayMode m) {
        ins[i].lengthFrames = uint32_t(d.size());
        ins[i].playMode = uint8_t(m);
        eng->bank().store(i, d.data(), uint32_t(d.size()), false);
    }
    void note(uint8_t track, uint8_t instr, uint8_t note) {
        VoiceNoteOn n;
        n.instrument = instr;
        n.note = note;
        n.volume = -1;
        eng->voiceNoteOn(track, n);
    }
};

std::vector<int16_t> tone(uint32_t len, float hz) {
    std::vector<int16_t> d(len);
    uint32_t seed = 11;
    for (uint32_t i = 0; i < len; ++i) {
        seed = seed * 1664525u + 1013904223u;
        const float n = float(int32_t(seed) >> 16) * (1.0f / 32768.0f);
        d[i] = int16_t(20000.0f * std::sin(2.0f * kPi * hz * float(i) / 44100.0f) + 2000.0f * n);
    }
    return d;
}

volatile int16_t gSink;

double gAccPerBlock = 0.0;   // SDRAM accesses per block of the last timeBlocks()

// Per-block cost in kUnit (best of kReps runs of n blocks).
double timeBlocks(Rig& r, int n, const int16_t* lineIn) {
    n /= kScale;
    int16_t out[kBlock * 2];
    double best = 1e30;
    for (int rep = 0; rep < kReps; ++rep) {
        gSdramAccesses = 0;
        const double t0 = nowUnits();
        for (int b = 0; b < n; ++b) {
            if (lineIn) r.eng->setLineInput(lineIn);
            r.eng->renderLive(out);
            gSink = out[b & 255];
        }
        const double per = (nowUnits() - t0) / n;
        gAccPerBlock = double(gSdramAccesses) / n;
        if (per < best) best = per;
    }
    return best;
}

// Device estimate: M4 cycles ~= instructions + SDRAM accesses * kSdramCycles.
// Calibrated on the field data of the previous engine: stopped and silent it
// cost 292,000 cycles/block on the device, and this harness (newlib-nano,
// like the device) counts 167,000 instructions for it plus 3,712 uncached
// 16-bit SDRAM accesses per block (reverb 23 + delay 6 per sample):
// (292,000 - 167,000) / 3,712 = ~34 cycles per access. The engine now uses
// sequential 32-bit accesses, so this is on the pessimistic side.
constexpr double kSdramCycles = 34.0;
void report(const char* name, double v) {
#ifdef OT_BENCH_M4
    const double est = v + gAccPerBlock * kSdramCycles;
    std::printf("  %-6s %7.0f insn + %5.0f SDRAM acc/block -> est. %7.0f cycles (%5.1f%% of 522240)\n", name, v,
                gAccPerBlock, est, 100.0 * est / 522240.0);
#else
    std::printf("  %-6s %8.2f %s/block  (%5.2f%% of the 2901 us block period on this host), %5.0f SDRAM acc/block\n",
                name, v, kUnit, 100.0 * v / 2901.0, gAccPerBlock);
#endif
}


// Per-component cost (one block each, averaged), loud input.
void components() {
    std::printf("  components (per block):\n");
    float in[kBlock], l[kBlock], r[kBlock];
    for (uint32_t i = 0; i < kBlock; ++i) in[i] = 0.3f * std::sin(float(i) * 0.05f);
    const int n = 400 / kScale;
    {
        static Delay d;
        d.init(gFx);
        d.setTimeSamples(16000);
        d.setFeedbackK(128);
        const double t0 = nowUnits();
        for (int b = 0; b < n; ++b) d.process(in, l, r);
        std::printf("    delay    %10.1f\n", (nowUnits() - t0) / n);
    }
    {
        static Reverb v;
        v.init(gFx + Delay::kMemorySamples);
        const double t0 = nowUnits();
        for (int b = 0; b < n; ++b) v.process(in, l, r);
        std::printf("    reverb   %10.1f\n", (nowUnits() - t0) / n);
    }
    {
        static Limiter lim;
        lim.reset();
        lim.setThreshold(0.5f);
        lim.setCoefs(0.01f, 0.001f);
        const double t0 = nowUnits();
        for (int b = 0; b < n; ++b) lim.process(in, nullptr, l);
        std::printf("    limiter  %10.1f\n", (nowUnits() - t0) / n);
    }
    {
        Rig r;
        r.load(0, tone(44100, 220.0f), PlayMode::ForwardLoop);
        r.ins[0].delaySend = 30;
        r.note(0, 0, 60);
        static VoiceOut o;
        float ds[kBlock] = {}, rs[kBlock] = {};
        VoiceControl c;
        Voice& v = r.eng->voice(0);
        double t0 = nowUnits();
        for (int b = 0; b < n; ++b) v.control(c);
        std::printf("    v.ctrl   %10.1f\n", (nowUnits() - t0) / n);
        t0 = nowUnits();
        for (int b = 0; b < n; ++b) v.render(c, o, ds, rs);
        std::printf("    v.render %10.1f  (plain loop voice)\n", (nowUnits() - t0) / n);
        r.ins[0].filterEnabled = 1;
        r.ins[0].cutoff = 0.5f;
        v.control(c);
        t0 = nowUnits();
        for (int b = 0; b < n; ++b) v.render(c, o, ds, rs);
        std::printf("    v.render %10.1f  (+ filter)\n", (nowUnits() - t0) / n);
    }
}

}  // namespace

int main() {
    static int16_t zeros[kBlock * 2] = {};
    static int16_t lineNoise[kBlock * 2];
    uint32_t s = 3;
    for (auto& v : lineNoise) {
        s = s * 1103515245u + 12345u;
        v = int16_t(int32_t(s >> 16) % 2000);
    }
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    clockStart();
#ifdef OT_BENCH_M4
    std::printf("audio_bench (cortex-m4 -O2 under QEMU, instruction counts):\n");
#else
    std::printf("audio_bench (host, -O2):\n");
#endif

#ifndef OT_BENCH_PROFILE
    {   // idle: fresh engine, then also after a played-out tail
        Rig r;
        report("idle", timeBlocks(r, 4000, zeros));
        r.load(0, tone(30000, 220.0f), PlayMode::OneShot);
        r.ins[0].delaySend = 60;
        r.ins[0].reverbSend = 60;
        r.note(0, 0, 60);
        timeBlocks(r, 1, zeros);
        int16_t out[kBlock * 2];
        int tail = 0;
        for (; tail < 6000; ++tail) {   // until the output is exactly silent for 1 s
            r.eng->setLineInput(zeros);
            r.eng->renderLive(out);
            static int quiet = 0;
            bool z = true;
            for (int16_t v : out) z &= v == 0;
            quiet = z ? quiet + 1 : 0;
            if (quiet > 345) break;
        }
        std::printf("  (tail of a delay/reverb note silent after %d blocks)\n", tail);
        // keep rendering silence a while so effect bypass can engage
        for (int b = 0; b < 3000 / kScale; ++b) {
            r.eng->setLineInput(zeros);
            r.eng->renderLive(out);
        }
        report("idle2", timeBlocks(r, 4000, zeros));
    }
#endif
    {   // typical 8-voice playback with delay + reverb
        Rig r;
        for (int i = 0; i < 4; ++i) {
            r.load(i, tone(44100, 110.0f * float(i + 1)), PlayMode::ForwardLoop);
            r.ins[i].delaySend = uint8_t(20 + 10 * i);
            r.ins[i].reverbSend = uint8_t(30 + 5 * i);
        }
        r.ins[1].filterEnabled = 1;
        r.ins[1].cutoff = 0.5f;
        r.ins[2].env[kAutoPanning].enabled = 1;
        r.ins[2].env[kAutoPanning].loop = 1;
        r.eng->settings().delayTimeMs = 375;
        r.eng->settings().delayFeedback = 45;
        for (int t = 0; t < 8; ++t) r.note(uint8_t(t), uint8_t(t & 3), uint8_t(48 + 3 * t));
        report("play8", timeBlocks(r, 2000, lineNoise));
#ifdef OT_BENCH_PROFILE
        return 0;
#endif
    }
    {   // worst case
        Rig r;
        auto noise = tone(88200, 330.0f);
        r.load(0, noise, PlayMode::Granular);
        r.load(1, noise, PlayMode::PingPongLoop);
        std::vector<int16_t> wt(2048 * 8);
        for (size_t i = 0; i < wt.size(); ++i)
            wt[i] = int16_t(20000.0f * std::sin(2.0f * kPi * float(i % 2048) / 2048.0f * float(1 + i / 2048)));
        r.load(2, wt, PlayMode::Wavetable);
        r.ins[2].wavetableWindowSize = 2048;
        r.ins[2].wavetableWindowCount = 8;
        for (int i = 0; i < 3; ++i) {
            r.ins[i].filterEnabled = 1;
            r.ins[i].filterType = uint8_t(i % 3);
            r.ins[i].resonance = 1.0f;
            r.ins[i].overdrive = 40;
            r.ins[i].bitDepth = 12;
            r.ins[i].delaySend = 50;
            r.ins[i].reverbSend = 50;
            for (int k = 1; k < kAutomationCount; ++k) {
                r.ins[i].env[k].enabled = 1;
                r.ins[i].env[k].loop = 1;
            }
        }
        MixerSettings& m = r.eng->settings();
        m.sidechain = 1;
        m.saturationMode = 3;
        m.delayPingPong = true;
        m.lineInDelaySend = 50;
        m.lineInReverbSend = 50;
        for (int t = 0; t < 8; ++t) r.note(uint8_t(t), uint8_t(t % 3), uint8_t(50 + t));
        r.eng->metronomeClick(true, 100);
        r.eng->previewBuffer(noise.data(), uint32_t(noise.size()), false);
        report("worst", timeBlocks(r, 2000, lineNoise));
    }
    components();
    return 0;
}
