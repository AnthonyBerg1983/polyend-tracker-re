// USB Storage Mode screen (spec 01 §6.1, spec 05 §1.6 "USB Storage Mode";
// stock strings 0xa2c7c / 0xa2c90 / 0xa2d34, screen 0x29670 / 0x295a8).
//
// Interface event 10 shows / hides a full-screen page: the title, a warning
// not to unplug the device, and an Exit button. In stock 1.9.2 the hardware
// hook behind it is an empty stub and there is no USB mass-storage class, so
// the card is not handed to the host; OpenTracker does the same (the SD card
// stays mounted, nothing is exported over USB). The Ui owns the overlay:
// while it is shown every button, pad and encoder event goes to it.
#pragma once

#include <cstdint>

namespace ot {

class Gfx;
struct Theme;

class UsbStorageMode {
public:
    static constexpr const char* kTitle = "USB Storage Mode";
    // SPEC GAP: the strings diff gives the stock warning (0xa2c90) only as
    // "Do not unplug…"; the rest of the sentence is OpenTracker's.
    static constexpr const char* kWarning = "Do not unplug the USB cable or remove the SD card";
    static constexpr const char* kExit = "Exit";
    // SPEC GAP: the Exit control's position is not recovered; it sits over F8.
    static constexpr uint8_t kExitKey = 7;   // btn::F8

    void show(bool on) { active_ = on; }
    bool active() const { return active_; }
    // Returns true when the event closes the page (Exit pressed).
    bool onButton(uint8_t id, uint8_t state);
    void draw(Gfx& g, const Theme& th) const;

private:
    bool active_ = false;
};

}  // namespace ot
