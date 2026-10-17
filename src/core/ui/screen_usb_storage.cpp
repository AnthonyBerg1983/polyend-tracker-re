// USB Storage Mode screen (spec 01 §6.1; see screen_usb_storage.h).
#include "ui/screen_usb_storage.h"

#include "ui/buttons.h"
#include "ui/gfx.h"
#include "ui/theme.h"
#include "ui/ui.h"

namespace ot {

bool UsbStorageMode::onButton(uint8_t id, uint8_t state) {
    // Stock exit path 0x28fa8 -> 0x28964 -> 0x284f0(0): Exit hides the page.
    if (!active_ || state != kPress || id != kExitKey) return false;
    active_ = false;
    return true;
}

void UsbStorageMode::draw(Gfx& g, const Theme& th) const {
    if (!active_) return;
    // spec 20 §4.13 [I] (no stock shot): ROM-font-29 banner centred at
    // (400, 240), the message in a text box (100, 300, 600, 100), "Exit" on
    // the stock strip. The banner uses the set's text colour (colour[0]):
    // colour[1] of the Original set is the 121212 background tone.
    g.fill(0, 0, kScreenW, kScreenH, th.bg);
    g.text(kScreenW / 2, 240, kFontTitle, th.slot(slot::kRomMessage), kTitle, IDisplay::kOptCenter);
    g.text(kScreenW / 2, 305, kFont1, th.slot(slot::kTextBox), kWarning, IDisplay::kOptCenterX);
    BottomBar b;
    b.set(kExitKey, kExit);
    b.draw(g, th);
}

}  // namespace ot
