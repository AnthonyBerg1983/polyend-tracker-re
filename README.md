# OpenTracker

A clean-room reimplementation of the original Polyend Tracker firmware
(NXP MK66FX1M0). 
It contains no vendor code, and it runs on real hardware.

## What to install

Everything the firmware needs is in `third_party/`: the PJRC Teensy core,
FatFs, and the font data for the simulator. Nothing else has to be downloaded
except the tools below.

| Tool | Needed for | Tested version | Arch / Debian package |
|---|---|---|---|
| `arm-none-eabi-gcc` + newlib (incl. `nano.specs`) | firmware | 16.2 (10.3 or newer should work) | `arm-none-eabi-gcc arm-none-eabi-newlib` / `gcc-arm-none-eabi libnewlib-arm-none-eabi` |
| GNU make | everything | 4.4 | `make` |
| Python 3 | image check (`tools/check_image.py`) | 3.14 | `python` / `python3` |
| g++ (C++17, with AddressSanitizer) | host tests | 16.2 | `gcc` / `g++` |
| SDL2 dev files | simulator | 2.32 | `sdl2` / `libsdl2-dev` |
| ALSA dev files (optional) | simulator MIDI ports | 1.2 | `alsa-lib` / `libasound2-dev` |

For the device itself you only need the stock firmware file
`PolyendTracker_1.9.2.ptf` from polyend.com, as the way back.

## Build

```
make                 # firmware  -> build/PolyendTrackerOTv<version digits>.ptf
make VERSION=0.2.0   # set the version (the file name stays 24 characters)
make SELFTEST=1      # hardware selftest -> build/selftest/PolyendTracker_OTst<N>.ptf
make -C tests        # host unit tests
make sim             # desktop simulator -> build/sim/opentracker-sim (see sim/README.md)
make clean
```

Every image goes through `tools/check_image.py`. It refuses an image unless:
- its flash-security bytes match stock, so the chip can't be locked;
- it stays inside the stock flash range;
- its name is exactly 24 characters.

## Install

Put the `.ptf` in `/Firmware` on the SD card. Install it with Config >
Firmware update, or write its name into `/Firmware/ot_restore.txt` and
power-cycle. Go back to stock the same way with `PolyendTracker_1.9.2.ptf`.

## Differences from the original

- Image names are exactly 24 characters, because the bootloader ignores longer names.
- At boot the SD card is checked first for `ot_restore.txt`, so there is always a way back to stock.
- USB identity is 1209:0001 "OpenTracker".
- Hardware init fixes: I2S clock divider and start order, pad-LED I2C timeout.
- Diagnostics over USB serial.
- Saving replaces only the project's pattern, metadata and instrument files; it does not wipe its sample folders.
- Opening old (1.3-era) projects imports their WAV samples straight from the project folder instead of copying them into `Workspace/samples` first; the result is the same.
- Project open, save and autosave run as background jobs with a progress popup. A file that cannot be read is reported, never loaded silently as empty.
- No Polyend branding, help text or fonts.
- OpenTracker's own, where the specs only give meanings:
  - the metronome click;
  - the Sample Editor effect DSP and overdrive types;
  - the on-screen keyboard layout and auto-name word list;
  - the picker look, scale names and MIDI-Mapping hint text;
  - the theme colours;
  - the second font set, which uses FT81x ROM fonts of the same size.
- The headphone level is fixed at 0.7; the stock level is unknown.
- The Loader's SD preview decodes into free sample memory, so there is no preview when memory is full; stock streams it.
- MIDI note entry in the pattern editor advances by the step jump.
- Master EQ, Bass Boost and Space are editable but have no DSP, as in stock 1.9.2.
- Power: hold for 1.5 s to switch off; "off" is a low-power stop with no EEPROM power flag.
- Not included:
  - the Manual viewer (unreachable in stock);
  - the factory test (use `make SELFTEST=1`);
  - the `/.develop` overlay;
  - the USB Storage Mode screen (the stock hook does nothing);
  - battery / fuel-gauge readout.
- The USB ID 1209:0001 is the pid.codes test ID.
- Config "Font" switches between two sets of the display controller's built-in (ROM) fonts: Set 1 is smooth (anti-aliased), Set 2 is a sharp bitmap font of about the same height. The pattern grid font stays the same in both. Stock's second set is wider and taller, but our screen layouts are sized for Set 1, so Set 2 keeps the same size and changes only the look.
- MIDI CC fixes:
  - Master level CCs (71–82) mark the project as changed, so autosave keeps them.
  - Instrument CCs set the exact target value; the encoder's step limits (LFO speed ±5, Tune ±10 → octave) do not apply.
- MOD / IT import and `.it` export fix the stock defects (spec 11):
  - MOD: every 4-channel signature (M.K., M!K!, FLT4, 4CHN), 2–8-channel files and the 15-sample layout are read; more than 8 channels is refused with a message before the project is touched.
  - MOD samples are converted to 16 bit at full length and notes use the matching offset (stock stores the 8-bit bytes as half-length 16-bit frames).
  - MOD finetuned and extended-octave periods map to the nearest note, and the sample finetune becomes the instrument finetune (stock drops them).
  - MOD song = the MOD song length (stock copies all 128 orders); a one-pattern module no longer reads an extra pattern before the samples.
  - MOD Fxx speed and tempo both convert to BPM = 6·tempo/speed, written as a Tempo FX of BPM/2 with no doubling; the project tempo is the song's start tempo, and a pattern entered at another tempo restates it on row 0.
  - MOD 9xx / IT Oxx sample offset is converted relative to the sample length (stock: xx/256 of the sample).
  - IT: the IMPM magic is checked, and sample-mode files (no instruments) import their samples as instruments.
  - IT 8-bit samples use the 44100 Hz reference like 16-bit (stock 22050 Hz: one octave high); unsigned, delta, stereo and IT 2.14/2.15 compressed samples are decoded.
  - IT ping-pong without the loop flag is one-shot; the sustain loop is used when there is no normal loop.
  - IT pattern note/instrument memory is per channel, a note without an instrument uses the channel's instrument, instrument 48 is usable, and note off/cut/fade become OFF/CUT/FADE (stock: note 127).
  - IT "+++" orders are skipped, the song ends at "---", and an empty pattern in the song becomes 64 empty rows; the project tempo is 6·IT/IS.
  - IT effects are imported into FX slot 0 (stock drops them all): Axx/Txx → Tempo, Cxx → Break, Gxx → Glide, Oxx → Position, Xxx → Panning.
  - Import and export run as background jobs with the progress popup; a file that cannot be imported is reported and the current project is kept.
  - `.it` export creates `Mod/` when it is missing (stock fails).
  - `.it` export reads each instrument's own sample data (stock's running cursor shifted every sample after a stereo one); stereo instruments export as stereo samples, and empty slots have no "sample present" flag.
  - `.it` export filter envelope: it checks envelope 2's own LFO flag (stock reads envelope 0's), uses the ±32 range for sustain, and keeps the filter flag in LFO mode; pan/filter LFOs use their own rate table.
  - `.it` export writes the song up to its end and patterns 1–200 (stock: 200 slots and patterns 1–199), sets the stereo flag with full separation, and maps volume 50 (0 dB) to 64 (stock: half volume).
  - `.it` export keeps both volume and panning FX (panning as Xxx), writes FX on steps without a note, exports Tempo/Break/Glide/Position as Txx/C00/Gxx/Oxx, restates the project tempo on each pattern when Tempo FX are used, skips MIDI-instrument steps and writes fade as note fade.
  - `.it` export above 255 BPM uses speed 3 (stock wraps the tempo byte), and patterns are read from memory instead of reloading each file into the edit buffer.
- Performance fixes:
  - Loading a per-track pattern no longer overwrites the next song or queued pattern that is preloading.
  - In play modes C1–C20, a missed roll on selector 2 now clears the previous roll (`kFixStockRollCarry`).
  - A per-track pattern queued to switch exactly at a song advance is kept, not lost.
  - "Back" play mode wraps to the active pattern length, including the Pattern length FX.
  - A MIDI CC (51–62) on a Pattern length slot applies the length; stock only shows the value.
  - The encoder never stores into the MIDI-CC live row; stock writes past the slot there.
  - Shift+Play while playing queues every track back to the current pattern at its own loop end (the stock sync-point action is not specified).
- Config is kept in the stock EEPROM area (same 0xF4-byte image); an `OpenTracker/config.bin` from an older build is imported once, and is used instead only when the EEPROM is not ready.
- Recorder source, gains, monitor and radio frequency are saved per project (stock bytes 0x134–0x13F); a project that never stored them gets gain 50 and monitor on.
- Song F1 "Loop Pattern"/"Continue Song" still pending when you leave the Song screen is applied at once instead of at the next step (spec 06 §2.3).
- Track Mixer meters read the engine's block peak (scaled to sine RMS) every 10 ms for the 10-reading mean; stock reads an RMS analyser (spec 06 §4.4).
- A non-looping selection preview stops after exactly its selected rows; stock counts `sel.end` master steps, so a selection not starting at step 0 replays rows (spec 03 §2.1).
- Auto slice: the onset-refinement window is clamped to the sample end (stock reads up to ~6144 frames past it), the slice count is cleared when detection starts, and detection advances only while Sample Playback is shown (leaving the screen finishes it at once) (spec 13 §F).
- Pattern grid: the 3-letter FX abbreviations and the chord/arp/LFO value glyphs are OpenTracker's own (the spec gives meanings only); Reverse shows ">>>" for 0 and "<<<" for 1; Pattern + the same column key twice clears the column-view mask (spec 06 §1.2, §1.9).
- Games (File > Games, spec 14):
  - The NES emulator is OpenTracker's own scanline-based core (`src/core/games/`); stock uses the GPL Nofrendo core. It supports mappers 0, 1, 2, 3, 4 (MMC3) and 7; stock supports 34. Raster effects are accurate to the line, not the dot.
  - A ROM that cannot be loaded (unreadable, too large, bad header, unsupported mapper) shows an error and stays in the browser. Stock shows nothing and enters a black game screen with the music muted.
  - The ROM and emulator memory live in the free part of the sample memory. If the project's samples leave too little room, the whole sample memory is lent to the game: the project is autosaved first, the autosave is held while playing, and the samples are reloaded (background Workspace load) when the game ends. In that case a screen key ends the game instead of suspending it.
  - A suspended game is resumed only if its memory is untouched; after a sample import or project load it is ended with a message.
  - The frame is 256 x 240 colours from an NTSC signal model, RGB332, at 60.1 Hz (stock: 224 core lines, 60.0 Hz). Only rows that changed are sent to the display, at RAM_G 384,000 (stock 840,000, which overlaps the cover image).
  - ROMs are loaded by their listed file name; stock appends ".nes" to the shortened display name.

