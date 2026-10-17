#include "app/capture.h"

#include <cmath>

#include "audio/engine.h"

namespace ot {

void Capture::setBuffer(int16_t* buf, uint32_t capacityFrames) {
    buf_ = buf;
    cap_ = buf ? capacityFrames : 0;
}

float Capture::gainFactor(uint8_t g) {
    if (g > 100) g = 100;
    return std::pow(10.0f, (float(g) - 50.0f) * 0.48f / 20.0f);
}

void Capture::setGain(uint8_t g) { gain_ = gainFactor(g); }

float Capture::takePeak() {
    const float p = peak_;
    peak_ = 0.0f;
    return p;
}

void Capture::start(uint32_t steps, float stepSeconds, uint32_t beatSteps, uint32_t barSteps,
                    bool clickWhileRecording) {
    len_ = 0;
    full_ = false;
    countPos_ = 0;
    stepFrames_ = stepSeconds > 0.0f ? uint32_t(stepSeconds * 44100.0f + 0.5f) : 0;
    beatSteps_ = beatSteps ? beatSteps : 1;
    barSteps_ = barSteps ? barSteps : beatSteps_;
    clickRec_ = clickWhileRecording && stepFrames_ > 0;
    if (steps && stepFrames_ && buf_) {
        countLeft_ = stepFrames_ * steps;
        state_ = kCountIn;
    } else {
        countLeft_ = 0;
        state_ = buf_ ? kRecording : kIdle;
    }
}

void Capture::stop() {
    state_ = kIdle;
    countLeft_ = 0;
}

void Capture::process(const int16_t* in, uint32_t frames, audio::AudioEngine* eng) {
    const uint8_t st = state_;
    if (!metering_ && st == kIdle) return;
    float peak = peak_;
    uint32_t len = len_;
    for (uint32_t i = 0; i < frames; ++i) {
        float x = 0.0f;
        if (in) {
            const float l = float(in[2 * i]), r = float(in[2 * i + 1]);
            x = chMode_ == 1 ? l : (chMode_ == 2 ? r : 0.5f * (l + r));
            x *= gain_;
        }
        if (x > 32767.0f) x = 32767.0f;
        if (x < -32768.0f) x = -32768.0f;
        const float a = std::fabs(x) / 32768.0f;
        if (a > peak) peak = a;
        // Metronome on every step boundary (spec 07 Q10 case 3; accent 100 /
        // normal 45 cents as the sequencer clicks, spec 03 §2.5).
        if ((st == kCountIn || (st == kRecording && clickRec_)) && stepFrames_) {
            if (countPos_ % stepFrames_ == 0 && eng) {
                const uint32_t s = countPos_ / stepFrames_;
                if (s % barSteps_ == 0) eng->metronomeClick(true, 100);
                else if (s % beatSteps_ == 0) eng->metronomeClick(false, 45);
            }
            ++countPos_;
        }
        if (st == kCountIn) {
            if (countLeft_ > 0) --countLeft_;
        } else if (st == kRecording) {
            if (len < cap_) buf_[len++] = int16_t(std::lrint(x));
            else full_ = true;
        }
    }
    len_ = len;
    peak_ = peak;
    if (st == kCountIn && countLeft_ == 0) state_ = kRecording;
    if (st == kRecording && full_) state_ = kIdle;   // memory full: recording stopped
}

void autoTrim(const int16_t* d, uint32_t len, uint32_t* start, uint32_t* end) {
    *start = 0;
    *end = len;
    if (!d || len == 0) return;
    // Envelope: attack coefficient exp(-0.0499), release one-pole 0.99/0.01.
    const float att = std::exp(-0.0499f);
    const float thr = 32767.0f * std::pow(10.0f, -3.5f);
    float env = 0.0f;
    int64_t first = -1, last = -1;
    for (uint32_t i = 0; i < len; ++i) {
        const float a = std::fabs(float(d[i]));
        env = a > env ? att * env + (1.0f - att) * a : 0.99f * env + 0.01f * a;
        if (env > thr) {
            if (first < 0) first = i;
            last = i;
        }
    }
    if (first < 0) return;
    int64_t s = first - 44, e = last + 1 + 44;
    if (s < 0) s = 0;
    if (e > int64_t(len)) e = len;
    if (e < 1) e = 1;
    if (s > e - 1) s = e - 1;
    *start = uint32_t(s);
    *end = uint32_t(e);
}

int meterUnits(float peak) {
    if (peak <= 0.0f) return 0;
    const float db = 20.0f * std::log10(peak);
    const float f = db <= -3.0f ? (db + 60.0f) * 85.0f / 57.0f : 85.0f + (db + 3.0f) * 5.0f;
    const int u = int(f);
    return u < 0 ? 0 : (u > 100 ? 100 : u);
}

}  // namespace ot
