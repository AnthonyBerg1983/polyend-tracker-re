// In-memory pattern (spec 02 §4.2-§4.4).
#pragma once

#include <cstdint>

#include "model/constants.h"

namespace ot {

struct StepFx {
    uint8_t type = 0;   // FxId value
    uint8_t value = 0;
};

// One step. fx[0] is "FX slot 0", fx[1] is "FX slot 1". On disk the order is
// slot 1 then slot 0 (spec 02 §4.2); the codec handles the swap.
struct Step {
    int8_t note = kNoteEmpty;
    uint8_t instrument = 0;
    StepFx fx[kFxPerStep];

    // Firmware clear routine: note empty, instrument 0, FX types 0. FX
    // values are left untouched (spec 02 §4.2).
    void clear() {
        note = kNoteEmpty;
        instrument = 0;
        for (auto& f : fx) f.type = 0;
    }
    bool operator==(const Step& o) const {
        return note == o.note && instrument == o.instrument && fx[0].type == o.fx[0].type &&
               fx[0].value == o.fx[0].value && fx[1].type == o.fx[1].type && fx[1].value == o.fx[1].value;
    }
};

struct Track {
    uint8_t lastStep = kDefaultTrackLastStep;  // length - 1 (0..127), spec 02 §4.2
    Step steps[kStepCount];

    int length() const { return int(lastStep) + 1; }
    void clear(uint8_t last) {
        lastStep = last;
        for (auto& s : steps) s.clear();
    }
};

struct Pattern {
    Track tracks[kTrackCount];

    // Default pattern (spec 02 §4.4): every track 32 steps, all steps empty.
    void setDefault() {
        for (auto& t : tracks) {
            t.lastStep = kDefaultTrackLastStep;
            for (auto& s : t.steps) s = Step{};
        }
    }
};

}  // namespace ot
