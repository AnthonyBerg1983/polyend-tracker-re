// App telemetry line (src/hal/telemetry.h): loop timing window, 5 s period,
// line format and the 70 % render alarm. PLAN_PHASE2 T1c.
#include "test.h"

#include <cstring>
#include <string>

#include "hal/diag_log.cpp"    // platform independent; not part of libcore
#include "hal/telemetry.cpp"

using namespace ot::diag;

TEST(telemetry_period) {
    Telemetry t;
    CHECK(!t.due(1000));      // first call starts the window
    CHECK(!t.due(5999));
    CHECK(t.due(6000));
    CHECK(!t.due(6001));
    CHECK(t.due(11000));
    // millis wrap
    Telemetry w;
    CHECK(!w.due(0xFFFFF000u));
    CHECK(!w.due(0x00000100u));
    CHECK(w.due(0x00000F00u));
}

TEST(telemetry_loop_max) {
    Telemetry t;
    t.loopMark(100);
    CHECK(t.loops() == 0);
    t.loopMark(600);
    t.loopMark(12600);
    t.loopMark(12700);
    CHECK(t.loops() == 3);
    CHECK(t.loopMaxUs() == 12000);
    t.resetWindow();
    CHECK(t.loops() == 0);
    CHECK(t.loopMaxUs() == 0);
    t.loopMark(13700);        // measured from the last mark before the reset
    CHECK(t.loopMaxUs() == 1000);
    // micros wrap
    Telemetry w;
    w.loopMark(0xFFFFFF00u);
    w.loopMark(0x00000100u);
    CHECK(w.loopMaxUs() == 0x200u);
}

TEST(telemetry_format) {
    TelemetrySample s;
    s.cycMin = 41000;
    s.cycAvg = 52224;
    s.cycMax = 104448;        // 20.0 %
    s.blocks = 1723;
    s.loopMaxUs = 12345;
    s.loops = 4000;
    s.sdramFree = 491520;
    s.i2c0Errors = 1;
    s.i2c2Errors = 2;
    char b[256];
    size_t n = formatTelemetry(b, sizeof b, s);
    CHECK(n == std::strlen(b));
    CHECK(std::string(b) ==
          "telemetry: render cyc/block min 41000 avg 52224 max 104448 (20.0% of 522240) blocks 1723 | "
          "loop max 12.345 ms (n 4000) | SDRAM free 491520 B | i2c errors i2c0 1 i2c2 2");
    s.cycMax = 365568;        // exactly 70 %
    s.loopMaxUs = 7;
    formatTelemetry(b, sizeof b, s);
    CHECK(std::strstr(b, "(70.0% of 522240)") != nullptr);
    CHECK(std::strstr(b, "loop max 0.007 ms") != nullptr);
    CHECK(std::strstr(b, " RENDER>70%") != nullptr);
    // Small buffers truncate safely.
    char small[16];
    formatTelemetry(small, sizeof small, s);
    CHECK(std::strlen(small) == 15);
}
