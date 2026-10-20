# OpenTracker preset pack

Our own synth patches for the ACD, VAP and PERC engines of OpenTracker 2.x, written by
`reimpl/tools/make_patches` (`make -C reimpl patches`) through the firmware's own Save As path.
Every patch starts from the engine's built-in Init patch; no factory patch is used.

Files: `Patches/<MODEL>/<name>.patch`. Copy the `Patches` folder to the root of the SD card,
then use Load Patch on a synth page.

Macros: M1-M6 (FX CC A-F). A bipolar macro (+-) at 63 and a unipolar macro at 0 play the
patch as saved. M7 is Velocity (Volume FX), as in the Init patch. Levels are voiced for the
default slot gain (-9 dB); `tests/patches_test.cpp` checks that every patch is audible, stays
at least 1 dB below full scale (VAP: 4-voice chords; PERC: five drums at once, toms alone) and does not
clip with all macros at either extreme.

Poly: the suggested slot polyphony (1 = mono, 4 = chords). It is a slot setting on the synth
page, not part of the patch.

## ACD (12)

| Patch | Description | Poly | Macros M1-M6 |
|---|---|---|---|
| OT Rubber Acid | Round saw acid line: RD3 ladder, fast filter envelope, legato glide. | 1 | Cutoff (+-), Resonance (+-), Env Amt (+-), Decay (+-), Glide, Sub |
| OT Squelch Box | Square-wave squelch: high ladder resonance, snappy envelope. | 1 | Cutoff (+-), Resonance (+-), Env Amt (+-), Decay (+-), Wobble, Glide |
| OT Deep Sub | Clean sub bass: triangle sub with a little square on top. | 1 | Tone (+-), Body, Punch (+-), Release (+-), Glide, Grit |
| OT Rubber Bass | Pulse + sub bass with a short filter bounce. | 1 | Cutoff (+-), Resonance (+-), Env Amt (+-), Decay (+-), Width (+-), Sub (+-) |
| OT Night Pluck | Saw + square pluck, dark and short; good with delay. | 1 | Cutoff (+-), Resonance (+-), Env Amt (+-), Decay (+-), Release (+-), Vibrato |
| OT Glass Pluck | Narrow pulse pluck with a pulse-width sweep; bright and glassy. | 1 | Cutoff (+-), Resonance (+-), Width (+-), PW Sweep (+-), Decay (+-), Release (+-) |
| OT Acid Lead | Singing mono lead: saw + pulse, slow filter swell, vibrato, glide. | 1 | Cutoff (+-), Resonance (+-), Env Amt (+-), Vibrato (+-), Glide, Release (+-) |
| OT Wobble Acid | Ladder bass with a triangle LFO on the cutoff (wobble). | 1 | Cutoff (+-), Resonance (+-), Wobble (+-), Rate (+-), Env Amt (+-), Square |
| OT Hollow Bass | Square bass with a two-octave sub; hollow and woody. | 1 | Cutoff (+-), Resonance (+-), Width (+-), Sub (+-), Env Amt (+-), Release (+-) |
| OT Bent Acid | Dirty acid: pulse sweep, a touch of noise, glide on every note. | 1 | Cutoff (+-), Resonance (+-), Env Amt (+-), Decay (+-), Noise, Glide (+-) |
| OT Sub Pluck | Short sub-heavy pluck bass for house and garage lines. | 1 | Cutoff (+-), Punch (+-), Decay (+-), Saw (+-), Resonance (+-), Release (+-) |
| OT Wah Lead | Saw lead with a slow resonant filter wah. | 1 | Cutoff (+-), Resonance (+-), Wah Depth (+-), Wah Rate (+-), Release (+-), Glide |

## VAP (16)

| Patch | Description | Poly | Macros M1-M6 |
|---|---|---|---|
| OT Velvet Pad | Warm detuned-saw pad, slow attack, a gentle tempo-synced filter drift. | 4 | Cutoff (+-), Resonance (+-), Attack (+-), Release (+-), Motion, Detune (+-) |
| OT Glass Pad | Airy triangle + pulse pad an octave apart, slow pulse-width shimmer. | 4 | Cutoff (+-), Shimmer (+-), Attack (+-), Release (+-), PWM Depth, Detune (+-) |
| OT Warm Strings | String ensemble: detuned saws, soft swell, gentle vibrato. | 4 | Cutoff (+-), Attack (+-), Release (+-), Vibrato (+-), Detune (+-), Env Amt (+-) |
| OT Brass Stab | Punchy synth brass: ladder filter with a quick envelope bite. | 4 | Cutoff (+-), Env Amt (+-), Attack (+-), Release (+-), Resonance (+-), Detune (+-) |
| OT Sync Lead | Hard-sync lead: an envelope sweeps the synced oscillator; glide and vibrato. | 1 | Cutoff (+-), Sync Sweep (+-), Sweep Time (+-), Vibrato, Glide, Release (+-) |
| OT PWM Lead | Two pulse waves with independent width LFOs; thick and moving. | 1 | Cutoff (+-), Resonance (+-), PWM Depth (+-), PWM Rate (+-), Glide, Release (+-) |
| OT Soft Pluck | Mellow triangle + saw pluck with an octave on top. | 4 | Cutoff (+-), Env Amt (+-), Decay (+-), Release (+-), Resonance (+-), Tone (+-) |
| OT Harp Pluck | Bright pulse + triangle pluck with a long ring; phase-reset attack. | 4 | Cutoff (+-), Env Amt (+-), Decay (+-), Width (+-), Release (+-), Resonance (+-) |
| OT Fat Bass | Saw + square an octave down through the ladder filter; mono, legato glide. | 1 | Cutoff (+-), Resonance (+-), Env Amt (+-), Decay (+-), Sub (+-), Glide |
| OT Bell Keys | Struck bell: two triangles at an inharmonic interval, bright strike, long ring. | 4 | Tone (+-), Strike (+-), Decay (+-), Release (+-), Ratio (+-), Chorus |
| OT Mellow Keys | Soft electric-piano-like keys with a gentle tremolo. | 4 | Cutoff (+-), Env Amt (+-), Decay (+-), Release (+-), Tremolo, Tone (+-) |
| OT Wind Sweep | Resonant band-passed noise, slowly swept over four bars. | 4 | Cutoff (+-), Resonance (+-), Sweep Depth (+-), Air (+-), Attack (+-), Release (+-) |
| OT Arp Blip | Short square + saw blip with phase reset; made for arpeggios and fast lines. | 4 | Cutoff (+-), Resonance (+-), Env Amt (+-), Length (+-), Octave (+-), Release (+-) |
| OT Unison Lead | Wide detuned-saw mono lead with glide and vibrato. | 1 | Cutoff (+-), Detune (+-), Vibrato, Glide, Resonance (+-), Release (+-) |
| OT Dark Drone | Low resonant drone, very slow filter and pitch drift; hold a note. | 4 | Cutoff (+-), Resonance (+-), Motion, Attack (+-), Release (+-), Detune (+-) |
| OT Organ Keys | Square + triangle an octave up, instant on/off, light vibrato. | 4 | Tone (+-), Drawbar (+-), Vibrato, Shape (+-), Release (+-), Detune (+-) |

## PERC (8)

Notes (MIDI numbers): kick 60, snare 62, hi-hats 64-67 (closed to open), cymbal 69, perc 71; any other
note plays the tom at that pitch.

| Patch | Description | Poly | Macros M1-M6 |
|---|---|---|---|
| OT Kit Boom | Analog-style boom kit: long sine kick with a pitch drop, soft snare, clap. | 1 | Tone (+-), Decay (+-), Pitch (+-), Snap (+-), Drive (+-), Space (+-) |
| OT Kit Punch | Punchy club kit: clicky kick, bright snare, noisy hats, saturated bus. | 1 | Tone (+-), Decay (+-), Pitch (+-), Snap (+-), Drive (+-), Space (+-) |
| OT Kit Dusty | Lo-fi kit: 6-bit crush, rolled-off highs, warm low end. | 1 | Tone (+-), Decay (+-), Pitch (+-), Snap (+-), Drive (+-), Space (+-) |
| OT Kit Foundry | Industrial kit: folded kick, resonant toms, hard overdrive, metal hats. | 1 | Tone (+-), Decay (+-), Pitch (+-), Snap (+-), Drive (+-), Space (+-) |
| OT Kit FM Metal | FM kit: phase-modulated kick, toms and snare, FM hats and cowbell. | 1 | Tone (+-), Decay (+-), Pitch (+-), Snap (+-), Drive (+-), Space (+-) |
| OT Kit Minimal | Minimal kit: tight kick, rimshot-like snare, short hats, shaker on delay. | 1 | Tone (+-), Decay (+-), Pitch (+-), Snap (+-), Drive (+-), Space (+-) |
| OT Kit Studio | Tight acoustic-like kit: beater kick, ringing toms, crisp snare, washy cymbal. | 1 | Tone (+-), Decay (+-), Pitch (+-), Snap (+-), Drive (+-), Space (+-) |
| OT Kit Glitch | Glitch kit: bit-twisted bodies, crushed bus, delays on hats and shaker. | 1 | Tone (+-), Decay (+-), Pitch (+-), Snap (+-), Drive (+-), Space (+-) |

Total: 36 patches.
