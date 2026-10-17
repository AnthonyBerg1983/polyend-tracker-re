// Delay, reverb, limiter (spec 04 §5).
//
// The reverb follows the topology of Mutable Instruments' Clouds/Rings
// reverb (stmlib FxEngine, MIT licence, (c) Emilie Gillet) as adapted in
// spec 04 §5.2; the code here is written from the spec, not copied.
#include "audio/effects.h"

#include <cmath>
#include <cstring>
#include <type_traits>

#include "audio/dsp.h"
#include "audio/ring_io.h"

namespace ot {
namespace audio {

namespace {
constexpr uint32_t kDMask = kDelayLineLen - 1;
constexpr uint32_t kRMask = kReverbRingLen - 1;
// |v| as OR-able magnitude: v >= 0 -> v, v < 0 -> -v - 1 (ones' complement).
inline uint32_t mag(int32_t v) { return uint32_t(v ^ (v >> 31)); }

// Block working buffers (SRAM). One engine renders at a time.
int16_t gX[kBlock];
int16_t gR0[kBlock], gR1[kBlock];
int16_t gW0[kBlock], gW1[kBlock];
}  // namespace

// --- Delay (spec 04 §5.1) ----------------------------------------------------

void Delay::init(int16_t* mem) {
    a_ = mem;
    b_ = mem + kDelayLineLen;
    c_ = mem + 2 * kDelayLineLen;
    setFilter(20000.0f);
    clear();
}

void Delay::clear() {
    if (a_) std::memset(a_, 0, sizeof(int16_t) * kMemorySamples);
    xa_ = xb_ = xc_ = ya_ = yb_ = yc_ = 0;
    w_ = 0;
    quiet_ = kDelayLineLen;    // an empty ring is silent
    fresh_ = kDelayLineLen;
    zeroed_ = kDelayLineLen;   // all lines are zero
    bypass_ = false;
}

bool Delay::process(const float* in, float* outL, float* outR) {
    if (disabled_ || !a_) {
        {
            zeroFloats(outL, kBlock);
            zeroFloats(outR, kBlock);
        }
        return false;
    }
    int32_t any = 0;
    for (uint32_t i = 0; i < kBlock; ++i) {
        gX[i] = toS16(in[i]);
        any |= gX[i];
    }
    const uint32_t t = time_;
    const bool pp = pingpong_;
    if (pp != activePP_) {
        // The other line set becomes active: it starts empty.
        activePP_ = pp;
        fresh_ = 0;
        quiet_ = 0;
        zeroed_ = 0;
        xa_ = xb_ = xc_ = ya_ = yb_ = yc_ = 0;
    }
    const uint32_t yq = pp ? (mag(ya_) | mag(yb_)) : mag(yc_);
    if (any == 0 && quiet_ >= t && yq <= uint32_t(kQuiet)) {
        // Bypass: silent input, decayed tail.
        bypass_ = true;
        {
            zeroFloats(outL, kBlock);
            zeroFloats(outR, kBlock);
        }
        if (zeroed_ < kDelayLineLen) {
            if (pp) {
                ringZero(a_, kDMask, w_, kBlock);
                ringZero(b_, kDMask, w_, kBlock);
            } else {
                ringZero(c_, kDMask, w_, kBlock);
            }
            zeroed_ += kBlock;
        }
        xa_ = xb_ = xc_ = ya_ = yb_ = yc_ = 0;
        w_ = (w_ + kBlock) & kDMask;
        if (quiet_ < 0x7FFFFFFFu) quiet_ += kBlock;
        if (fresh_ < kDelayLineLen) fresh_ += kBlock;
        return false;
    }
    bypass_ = false;
    zeroed_ = 0;   // the ring is no longer known to be clear
    const int32_t b = b_coef_, c = c_coef_, k = k_;
    uint32_t w = w_;
    uint32_t loud = 0;
    for (uint32_t i0 = 0; i0 < kBlock;) {
        // Chunks of at most t samples: every read precedes the write of the
        // same position (reads at w - t, writes at w).
        uint32_t n = kBlock - i0;
        if (n > t) n = t;
        const uint32_t r = (w - t) & kDMask;
        const int16_t* x = gX + i0;
        float* oL = outL + i0;
        float* oR = outR + i0;
        if (pp) {
            ringIn(a_, kDMask, r, gR0, n);
            ringIn(b_, kDMask, r, gR1, n);
            if (fresh_ < t)
                for (uint32_t j = 0; j < n && fresh_ + j < t; ++j) gR0[j] = gR1[j] = 0;
            int32_t xa = xa_, xb = xb_, ya = ya_, yb = yb_;
            for (uint32_t j = 0; j < n; ++j) {
                const int32_t ra = gR0[j], rb = gR1[j];
                // One-pole filter per tap (spec 07 Q9): y = (x*b + x1*b - y1*c) >> 10.
                const int32_t fa = (ra * b + xa * b - ya * c) >> 10;
                const int32_t fb = (rb * b + xb * b - yb * c) >> 10;
                xa = ra;
                xb = rb;
                ya = fa;
                yb = fb;
                oL[j] = s16ToF(sat16(fa));
                oR[j] = s16ToF(sat16(fb));
                // Ping-pong cross-feed A<->B (spec 04 §5.1).
                const int32_t wb = sat16(x[j] + ((fa * k) >> 8));
                const int32_t wa = sat16((fb * k) >> 8);
                gW1[j] = int16_t(wb);
                gW0[j] = int16_t(wa);
                loud |= mag(ra) | mag(rb) | mag(wa) | mag(wb);
            }
            xa_ = xa;
            xb_ = xb;
            ya_ = ya;
            yb_ = yb;
            ringOut(a_, kDMask, w, gW0, n);
            ringOut(b_, kDMask, w, gW1, n);
        } else {
            ringIn(c_, kDMask, r, gR0, n);
            if (fresh_ < t)
                for (uint32_t j = 0; j < n && fresh_ + j < t; ++j) gR0[j] = 0;
            int32_t xc = xc_, yc = yc_;
            for (uint32_t j = 0; j < n; ++j) {
                const int32_t rc = gR0[j];
                const int32_t fc = (rc * b + xc * b - yc * c) >> 10;
                xc = rc;
                yc = fc;
                const float o = s16ToF(sat16(fc));
                oL[j] = o;
                oR[j] = o;
                // Mono line C (spec 04 §5.1).
                const int32_t wc = sat16(x[j] + ((fc * k) >> 8));
                gW0[j] = int16_t(wc);
                loud |= mag(rc) | mag(wc);
            }
            xc_ = xc;
            yc_ = yc;
            ringOut(c_, kDMask, w, gW0, n);
        }
        w = (w + n) & kDMask;
        i0 += n;
        fresh_ = fresh_ + n > kDelayLineLen ? kDelayLineLen : fresh_ + n;
    }
    w_ = w;
    if (loud > uint32_t(kQuiet)) quiet_ = 0;
    else if (quiet_ < 0x7FFFFFFFu) quiet_ += kBlock;
    return true;
}

// --- Reverb (spec 04 §5.2) ---------------------------------------------------
//
// Ring A layout (offsets from the decrementing write pointer, spec 04 §5.2):
//   ap1 0..149, ap2 151..364, ap3 366..684, ap4 686..1212,
//   dap1a 1214..3395, dap1b 3397..6086, del1 6088..10588, pre-delay write 10590.
// Ring B: dap2a 0..2524, dap2b 2526..4722, del2 base 4724 (modulated read +6211 + 100*(lfo+0.5)).

void Reverb::init(int16_t* mem) {
    a_ = mem;
    b_ = mem + kReverbRingLen;
    // LFO: s[n+1] = c*s[n] - s[n-1], c ~ 1.99999, updated every 32 samples.
    lfoC_ = 1.99999f;
    clear();
}

void Reverb::clear() {
    if (a_) std::memset(a_, 0, sizeof(int16_t) * kMemorySamples);
    std::memset(ap_, 0, sizeof(ap_));
    lp1_ = lp2_ = lp3_ = 0.0f;
    const float w = std::acos(lfoC_ * 0.5f);
    lfoPrev_ = 0.0f;
    lfoS_ = std::sin(w);
    p_ = 0;
    level_ = 0.0f;
    quiet_ = kReverbRingLen;   // an empty ring is silent
    bypass_ = false;
}

namespace {
// Reverb block buffers (SRAM). Taps are read as 128-sample spans at fixed
// offsets from the block's start pointer P: sample i (pointer P - i) uses
// index 127 - i of a span read from P + offset - 127. Allpass spans are
// updated in place (the read of index k precedes its write).
constexpr uint32_t kModSpan = kBlock + 210;   // modulated del2 read (+-100 + 1)
int16_t gTap[5][kBlock];      // dap1a, dap1b, del1 tap, dap2a, dap2b
int16_t gDel1[kBlock], gDel2[kBlock], gPre[kBlock], gPd[kBlock];
int16_t gMod[kModSpan];
int32_t gModIdx[kBlock];
float gModFrac[kBlock];
}  // namespace

void Reverb::stepLfo(uint32_t p) {
    if ((p & 31u) == 0) {
        const float s = lfoC_ * lfoS_ - lfoPrev_;
        lfoPrev_ = lfoS_;
        lfoS_ = s > 1.0f ? 1.0f : (s < -1.0f ? -1.0f : s);
    }
}

bool Reverb::process(const float* in, float* outL, float* outR) {
    if (!a_) {
        {
            zeroFloats(outL, kBlock);
            zeroFloats(outR, kBlock);
        }
        return false;
    }
    int16_t* A = a_;
    int16_t* B = b_;
    const uint32_t P = p_;
    const float kap = kap_, klp = klp_, krt = krt_, amount = amount_, gain = gain_;
    const int32_t pre = predelay_ < 0 ? 4999 : predelay_;
    // Pre-delay input (written before its read, spec order) and silence check.
    bool anyIn = false;
    uint32_t q = 0;
    for (uint32_t i = 0; i < kBlock; ++i) {
        const float L = in[i], R = in[i];
        anyIn |= L != 0.0f;
        const int16_t v = toS16((L + R) * gain);
        gPre[kBlock - 1 - i] = v;
        q |= mag(v);
    }
    constexpr float kQuietF = float(kQuiet) / 32768.0f;
    auto small = [&](float v) { return v <= kQuietF && v >= -kQuietF; };
    if (!anyIn && quiet_ >= kReverbRingLen && small(lp1_) && small(lp2_) && small(lp3_)) {
        // Bypass: the whole ring holds +-kQuiet residue only.
        bypass_ = true;
        {
            zeroFloats(outL, kBlock);
            zeroFloats(outR, kBlock);
        }
        uint32_t p = P;
        for (uint32_t i = 0; i < kBlock; ++i) {
            stepLfo(p);
            p = (p - 1u) & kRMask;
        }
        p_ = p;
        lp1_ = lp2_ = lp3_ = 0.0f;
        level_ = 0.0f;
        if (quiet_ < 0x7FFFFFFFu) quiet_ += kBlock;
        return false;
    }
    bypass_ = false;
    // LFO and the modulated del2 read positions (relative to P).
    // (The LFO steps every 32 samples, so the offset is piecewise constant.)
    int32_t lo = 0x7FFFFFFF, hi = -0x7FFFFFFF;
    {
        int32_t oi = 0;
        float of = 0.0f;
        uint32_t p = P;
        for (uint32_t i = 0; i < kBlock; ++i) {
            if (i == 0 || (p & 31u) == 0) {
                if (i > 0) lo = oi - int32_t(i - 1) < lo ? oi - int32_t(i - 1) : lo;   // segment end
                stepLfo(p);
                const float off = 4724.0f + 6211.0f + 100.0f * (lfoS_ + 0.5f);
                const uint32_t u = uint32_t(off);
                oi = int32_t(u);
                of = off - float(u);
                hi = oi - int32_t(i) > hi ? oi - int32_t(i) : hi;                     // segment start
            }
            gModIdx[i] = oi - int32_t(i);
            gModFrac[i] = of;
            p = (p - 1u) & kRMask;
        }
        lo = oi - int32_t(kBlock - 1) < lo ? oi - int32_t(kBlock - 1) : lo;
        p_ = p;
    }
    uint32_t modN = uint32_t(hi - lo + 2);
    if (modN > kModSpan) modN = kModSpan;   // cannot happen (|lfo| <= 1)
    // Reads (all before the block's writes: every other tap pair is >= 128
    // samples apart or reads the older value).
    auto tapIn = [&](const int16_t* ring, uint32_t off, int16_t* buf) {
        ringIn(ring, kRMask, P + off - (kBlock - 1), buf, kBlock);
    };
    tapIn(A, 3395, gTap[0]);
    tapIn(A, 6086, gTap[1]);
    tapIn(A, 10588, gTap[2]);
    tapIn(B, 2524, gTap[3]);
    tapIn(B, 4722, gTap[4]);
    ringIn(B, kRMask, P + uint32_t(lo), gMod, modN);
    // Pre-delay: write the block, then read it back pre samples later. The
    // pre-delay region (offsets >= 10590) is read by nothing else and is
    // rewritten by ap1 before the pointer comes round, so with no pre-delay
    // the read returns exactly the block just written: skip both accesses.
    const int16_t* pd = gPre;
    if (pre != 0 && prevPre_ == 0) {
        // Pre-delay switched on: the region was not kept up to date; it
        // starts silent (the first pre samples read zeros).
        ringZero(A, kRMask, P + 10591u, 5000u + kBlock);
    }
    prevPre_ = pre;
    if (pre != 0) {
        ringOut(A, kRMask, P + 10590u - (kBlock - 1), gPre, kBlock);
        tapIn(A, 10590u + uint32_t(pre), gPd);
        pd = gPd;
    }

    float lp1 = lp1_, lp2 = lp2_, lp3 = lp3_;
    float sumAbs = 0.0f;
    // Quietness is tracked only for a silent input (otherwise quiet_ resets).
    auto run = [&](auto trackTag) {
    constexpr bool kTrack = decltype(trackTag)::value;
    for (uint32_t i = 0; i < kBlock; ++i) {
        const uint32_t k = kBlock - 1 - i;
        const float L = in[i], R = in[i];
        lp1 += klp * (s16ToF(pd[k]) - lp1);
        float acc = lp1;
        // Four diffusing all-passes.
        // Four diffusing all-passes (SRAM ring ap_, per sample as in spec order).
        const uint32_t pi = P - i;
        static constexpr uint16_t kApBase[4] = {0, 151, 366, 686};
        static constexpr uint16_t kApTail[4] = {149, 364, 684, 1212};
#pragma GCC unroll 4
        for (int a = 0; a < 4; ++a) {
            const float t = s16ToF(ap_[(pi + kApTail[a]) & (kApLen - 1)]);
            acc += kap * t;
            const int16_t v = toS16(acc);
            ap_[(pi + kApBase[a]) & (kApLen - 1)] = v;
            if (kTrack) q |= mag(v);
            acc = t - kap * acc;
        }
        const float apout = acc;
        // Left tank: modulated read of del2 (ring B).
        {
            const int32_t m = gModIdx[i] - lo;
            const float a0 = s16ToF(gMod[m]), a1 = s16ToF(gMod[m + 1]);
            acc = apout + krt * (a0 + (a1 - a0) * gModFrac[i]);
        }
        lp2 += klp * (acc - lp2);
        acc = lp2;
        {   // dap1a: read -kap, write-allpass +kap
            const float t = s16ToF(gTap[0][k]);
            acc += -kap * t;
            const int16_t v = toS16(acc);
            gTap[0][k] = v;
            if (kTrack) q |= mag(v);
            acc = kap * acc + t;
        }
        {   // dap1b: read +kap, write-allpass -kap
            const float t = s16ToF(gTap[1][k]);
            acc += kap * t;
            const int16_t v = toS16(acc);
            gTap[1][k] = v;
            if (kTrack) q |= mag(v);
            acc = t - kap * acc;
        }
        {   // del1
            const int16_t v = toS16(acc);
            gDel1[k] = v;
            if (kTrack) q |= mag(v);
        }
        const float wetL = 2.0f * acc;
        // Right tank: fixed del1 tap.
        acc = apout + krt * (s16ToF(gTap[2][k]));
        lp3 += klp * (acc - lp3);
        acc = lp3;
        {   // dap2a
            const float t = s16ToF(gTap[3][k]);
            acc += kap * t;
            const int16_t v = toS16(acc);
            gTap[3][k] = v;
            if (kTrack) q |= mag(v);
            acc = t - kap * acc;
        }
        {   // dap2b
            const float t = s16ToF(gTap[4][k]);
            acc += -kap * t;
            const int16_t v = toS16(acc);
            gTap[4][k] = v;
            if (kTrack) q |= mag(v);
            acc = kap * acc + t;
        }
        {   // del2
            const int16_t v = toS16(acc);
            gDel2[k] = v;
            if (kTrack) q |= mag(v);
        }
        const float wetR = 2.0f * acc;
        outL[i] = clampUnit(L + amount * (wetL - L));
        outR[i] = clampUnit(R + amount * (wetR - R));
        sumAbs += (wetL < 0.0f ? -wetL : wetL);
    }
    };
    if (anyIn) run(std::false_type{});
    else run(std::true_type{});
    // Writes (disjoint spans).
    auto tapOut = [&](int16_t* ring, uint32_t off, const int16_t* buf) {
        ringOut(ring, kRMask, P + off - (kBlock - 1), buf, kBlock);
    };
    tapOut(A, 1214, gTap[0]);
    tapOut(A, 3397, gTap[1]);
    tapOut(A, 6088, gDel1);
    tapOut(B, 0, gTap[3]);
    tapOut(B, 2526, gTap[4]);
    tapOut(B, 4724, gDel2);
    // Keep the filter states finite and bounded.
    lp1_ = clampUnit(lp1);
    lp2_ = clampUnit(lp2);
    lp3_ = clampUnit(lp3);
    level_ = sumAbs / float(kBlock);
    if (anyIn || q > uint32_t(kQuiet)) quiet_ = 0;
    else if (quiet_ < 0x7FFFFFFFu) quiet_ += kBlock;
    return true;
}

// --- Limiter (spec 04 §5.3) --------------------------------------------------

void Limiter::reset() {
    env_ = 0.0f;
    gs_ = 1.0f;
    for (float& h : hist_) h = 0.0f;
    hp_ = 0;
}

namespace {
template <bool kSc>
inline float limiterRun(const float* __restrict x, const float* __restrict sc, float* __restrict out,
                        const float* __restrict dl, float& envR, float& gsR, float& gR, float ca, float cr,
                        float thr) {
    float env = envR, gs = gsR, g = gR, sum = 0.0f;
    for (uint32_t i = 0; i < kBlock; ++i) {
        const float d = kSc ? sc[i] : x[i];
        const float a = d < 0.0f ? -d : d;
        const float c1 = a > env ? ca : cr;
        env = c1 * a + (1.0f - c1) * env;
        g = env > thr ? thr / env : 1.0f;
        const float c2 = g < gs ? ca : cr;
        gs = c2 * g + (1.0f - c2) * gs;
        // 4-sample look-ahead line.
        float y = gs * dl[i];
        if (kSc) y += (1.0f - gs) * sc[i];
        out[i] = y;
        sum += gs;
    }
    envR = env;
    gsR = gs;
    gR = g;
    return sum;
}
}  // namespace

bool Limiter::process(const float* x, const float* sc, float* out) {
    if (disabled_) {
        for (uint32_t i = 0; i < kBlock; ++i) out[i] = x[i];
        return true;
    }
    // At rest (empty look-ahead, envelope ~0, gain at its fixed point 1) a
    // silent block leaves the state unchanged and outputs zeros.
    if (thr_ > 0.0f && env_ <= 1e-30f && hist_[0] == 0.0f && hist_[1] == 0.0f && hist_[2] == 0.0f &&
        hist_[3] == 0.0f) {
        const float c2 = 1.0f < gs_ ? ca_ : cr_;
        if (c2 * 1.0f + (1.0f - c2) * gs_ == gs_) {
            bool silent = true;
            for (uint32_t i = 0; i < kBlock; ++i) silent &= x[i] == 0.0f;
            if (sc)
                for (uint32_t i = 0; i < kBlock; ++i) silent &= sc[i] == 0.0f;
            if (silent) {
                zeroFloats(out, kBlock);
                env_ = 0.0f;
                lastG_ = 1.0f;
                meanG_ = gs_;
                return false;
            }
        }
    }
    // Look-ahead: dl[i] = x[i - 4] (history first, oldest at hp_).
    float dl[kBlock + 4];
    for (uint32_t j = 0; j < 4; ++j) dl[j] = hist_[(hp_ + j) & 3u];
    for (uint32_t i = 0; i < kBlock; ++i) {
        OT_NO_LIBCALL();
        dl[i + 4] = x[i];
    }
    float g = 1.0f;
    const float sum = sc ? limiterRun<true>(x, sc, out, dl, env_, gs_, g, ca_, cr_, thr_)
                         : limiterRun<false>(x, sc, out, dl, env_, gs_, g, ca_, cr_, thr_);
    for (uint32_t j = 0; j < 4; ++j) hist_[j] = x[kBlock - 4 + j];
    hp_ = 0;
    lastG_ = g;
    meanG_ = sum / float(kBlock);
    return true;
}

// --- Line-input dynamics (spec 04 §5.6) --------------------------------------

namespace {
// log2(v) for a positive normal float, |error| < 2.1e-4 (0.0013 dB).
inline float fastLog2(float v) {
    uint32_t b;
    std::memcpy(&b, &v, 4);
    const float e = float(int32_t(b >> 23) - 127);
    b = (b & 0x007FFFFFu) | 0x3F800000u;
    float m;
    std::memcpy(&m, &b, 4);
    const float f = m - 1.0f;
    return e + (2.0372331e-4f + f * (1.4361024f + f * (-0.66952725f + f * (0.31222615f + f * -0.079153834f))));
}
// 2^v for -126 < v < 127, relative error < 2.1e-4 (0.002 dB).
inline float fastExp2(float v) {
    const int32_t i = int32_t(v) - (v < 0.0f ? 1 : 0);   // floor
    const float f = v - float(i);
    const float p = 0.99981218f + f * (0.69683754f + f * (0.22412730f + f * 0.079020150f));
    uint32_t b;
    std::memcpy(&b, &p, 4);
    b += uint32_t(i) << 23;
    float r;
    std::memcpy(&r, &b, 4);
    return r;
}
constexpr float kDbPerLog2 = 6.02059991f;     // 20 * log10(2)
constexpr float kLog2PerDb = 0.166096405f;    // 1 / kDbPerLog2
constexpr float kOpenLin = 0.0251188643f;     // 10^(-32/20)
constexpr float kCloseLin = 0.0125892541f;    // 10^(-38/20)
constexpr float kKneeLoLin = 0.00707945784f;  // 10^(-43/20): below it no compression
constexpr float kAttack128 = 7.39726663e-6f;  // kAttack^kBlock
constexpr float kRelease128 = 0.306836576f;   // kRelease^kBlock
static_assert(kBlock == 128, "closed-form idle() constants assume 128-sample blocks");
}  // namespace

void LineDynamics::process(const float* x, float* y) {
    float env = env_, gdb = gateDb_, cdb = compDb_;
    bool open = open_;
    for (uint32_t i = 0; i < kBlock; ++i) {
        OT_NO_LIBCALL();
        const float xi = x[i];
        const float a = xi < 0.0f ? -xi : xi;
        const float er = env * kRelease;
        env = a > er ? a : er;
        open = env >= kOpenLin ? true : (env < kCloseLin ? false : open);
        const float gt = open ? 0.0f : kFloorDb;
        gdb = gt + (gdb - gt) * (gt > gdb ? kAttack : kRelease);
        float lim = 0.0f;
        if (env > kKneeLoLin) {
            const float lvl = kDbPerLog2 * fastLog2(env);
            const float ct = staticGainDb(lvl);
            cdb = ct + (cdb - ct) * (ct < cdb ? kAttack : kRelease);
            const float over = lvl + cdb + kMakeupDb - kLimitDb;
            lim = over > 0.0f ? -over : 0.0f;
        } else {
            cdb *= kRelease;   // target 0 dB, gain rising
        }
        y[i] = xi * fastExp2((gdb + cdb + kMakeupDb + lim) * kLog2PerDb);
    }
    env_ = env;
    gateDb_ = gdb;
    compDb_ = cdb;
    open_ = open;
    settled_ = false;
}

void LineDynamics::decay() {
    env_ *= kRelease128;
    if (env_ < kCloseLin) open_ = false;
    const float gt = open_ ? 0.0f : kFloorDb;
    gateDb_ = gt + (gateDb_ - gt) * (gt > gateDb_ ? kAttack128 : kRelease128);
    compDb_ *= kRelease128;
    if (env_ < 1e-9f && !open_ && gateDb_ < kFloorDb + 0.01f && compDb_ > -0.001f) reset();
}

}  // namespace audio
}  // namespace ot
