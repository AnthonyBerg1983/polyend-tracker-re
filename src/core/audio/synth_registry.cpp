// Synth engine registry (synth_engine.h): Tracker Mini engine id -> engine.
//
// WP-F: add each engine here (one line per id) as it lands.
#include <cmath>

#include "audio/engines/acd.h"
#include "audio/engines/fat.h"
#include "audio/engines/perc.h"
#include "audio/engines/vap.h"
#include "audio/synth_engine.h"

namespace ot {
namespace audio {

ISynthEngine* synthTestEngine();   // synth_test_engine.cpp (internal)

ISynthEngine* synthEngineFor(int32_t engineId) {
    switch (engineId) {
        case kSynthEngineAcd:
            return synthAcdEngine();
        case kSynthEngineFat:
            return fat::engine();
        case kSynthEngineVap:
            return vap::engine();
        case kSynthEnginePerc:
            return percEngine();
        case kSynthEngineTest:
            return synthTestEngine();
        // WTFM is planned for 2.2 (docs/MINI_PLAN.md §3): the slot loads
        // and saves, the UI shows "not available", nothing sounds.
        default:
            return nullptr;
    }
}

float synthPitchHz(float pitch) { return 440.0f * std::exp2((pitch - 69.0f) * (1.0f / 12.0f)); }

}  // namespace audio
}  // namespace ot
