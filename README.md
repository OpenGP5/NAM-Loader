# NAM Loader

**A capture player firmware for the Valeton GP-5, built on [OpenGP5](https://github.com/OpenGP5/OpenGP5).**
Version 1.3.0, on OpenGP5 0.8.0-alpha.

It plays [Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) A2 captures (their A2-Lite
part) on the pedal itself, at 48 kHz, the rate captures are trained at. The model is split across both of the
pedal's processors: the DSP runs the first layers, the control processor (at 320 MHz) the rest, the output and a
cab impulse response. You keep up to 30 captures and 12 cab IRs on the pedal, and any capture can play any of
the IRs (or none). Each capture keeps its own IR choice, noise gate and 3-band EQ.

The network is the capture's own, every layer and weight, computed in fixed point (the weights in 16 bits, the
signal through the network in 32): the output is about 65 dB from NAM's own floating-point result on guitar (on the captures it was developed with). The
pedal's processors are too slow at floating point to run it in real time.

New in 1.3.0: captures and cab IRs are chosen independently. 30 capture slots (from 12) and a bank of 12 IRs; on
the pedal, turn PARA between the NAM page (the capture) and the CAB page (its IR), push it for that page's list.
The captures play exactly as in 1.2. IRs are stored in 16 bits (one storage sector each; the older float files
still play): 30 captures and 12 IRs fill the storage. Your 1.2 pairings carry over (tone 3 plays IR 3 until you
choose another), and so do the gate and EQ settings. The screen is drawn in strips, so USB and the controls wait
less.

1.2.0: a noise gate (after the model, before the cab IR) and a bass / mid / treble EQ (last) for each capture,
set from Launchpad and kept with the capture. Its settings are OpenGP5 1.0 parameters, so any tool that speaks
OpenGP5 can read and set them (`gp5_link.py params`).

1.1.0: a cab IR per slot (1,024 taps, 21 ms), 48 kHz, captures driven as hard as they were recorded
(input calibration) and every capture equally loud (output levelling).

> **Alpha.** Flashing custom firmware can go wrong. Read OpenGP5's
> [getting-started guide](https://github.com/OpenGP5/OpenGP5/blob/main/GETTING-STARTED.md), especially
> *Recovery*, before you flash.

## Controls

| control | does |
|---|---|
| turn PARA | switch between the NAM page (the capture playing) and the CAB page (its IR); in a list: move |
| push PARA | on NAM: the 30 capture slots; on CAB: the IRs, with NONE; in a list: load the highlighted one (the capture playing keeps playing while you browse; after 5 s without turning, back) |
| hold PARA | the TONE page: gate, bass, mid, treble of the capture playing |
| foot switch | bypass |
| volume knob | output level |

The screen shows the capture's name or the IR's (a long name slides to its end and back), the slot or IR number,
an input meter and the volume. The slot playing is remembered across power-off.

## Getting captures onto the pedal

Use [OpenGP5 Launchpad](https://github.com/OpenGP5/Launchpad) 0.2.0 or later: its Tones page takes a `.nam` file
(A2), converts its A2-Lite part and writes it into a slot. A1 (older) captures can't be played. With the bank
empty, the pedal plays its built-in capture, "NAM A2 demo".

- **Input calibration.** The pedal's input is +7.1 dBu at full scale (measured). A capture that says what level
  it was recorded at (`input_level_dbu` in its metadata) gets the difference as input gain, so it is driven as
  hard as the gear was. Captures without it play as they are.
- **Output levelling.** The converter plays plucked chords through the capture at the level of hard playing,
  measures its loudness (K-weighted) and sets it to -20 dB, its peaks at most -3 dBFS. Across 48 captures the
  loudness had spread over 32 dB. Launchpad has a switch for each of these two.
- **Cab IRs.** Drop a `.wav` impulse response on one of the 12 IRs in the Tones page, then choose it for any
  capture there (or on the pedal's CAB page). The first 1,024 samples at 48 kHz are used (any rate and length is
  resampled and cut), set to the same loudness as every other IR. For captures of an amp without a cab (DI); the IR
  switches itself off if the processor ever runs short of time.

## Build and flash

You need what OpenGP5 needs (Python 3; on macOS Docker Desktop for the control processor's compiler; Linux / WSL
need the 32-bit C library; see OpenGP5's guide).

```
git clone --recursive https://github.com/OpenGP5/NAM-Loader.git
cd NAM-Loader
opengp5/release/setup-toolchain.sh      # once: fetches the two compilers into opengp5/fw-build/
./build.sh                              # -> build/GP-5_nam-player.bin
python3 opengp5/tools/gp5tool.py flash build/GP-5_nam-player.bin --sections all
```

Installing it wipes the storage of the firmware that was on the pedal before (OpenGP5 0.5: storage belongs to
the installed firmware). Back that up first, for example with Launchpad.

## How it is made

| file | what |
|---|---|
| `kernel/a2r.c`, `a2r.h` | the A2-Lite network in fixed point with a 32-bit residual stream; builds for the control processor (NDS32 DSP extension), the DSP (Cortex-M4) and the computer |
| `kernel/a2r_blob.c`, `a2r_blob.h` | the capture file format: the quantized weights, their scales and the name |
| `split.h` | where the model is split, the sizes of the capture and IR banks, and the audio link's words; shared by both sides |
| `pack24.h` | the DSP's results packed as 24-bit block floating point, so they fit the audio link at 48 kHz |
| `nds32/a2.c` | the control processor's layers, the output and the cab IR, run in the audio link's interrupt; the capture and IR banks, and switching (fade out, load, fade in) |
| `firmware.json`, `params.h` | who it is (OpenGP5 1.0) and its parameters: cab IR on / off, taps, gate, bass, mid, treble, the IR chosen |
| `tone.h` | the noise gate and the 3-band EQ, in fixed point |
| `nds32/firmware.c` | the controls, the yellow 160 x 80 screen (NAM and CAB pages, the lists, TONE), and each capture's gate, EQ and IR (kept with its checksum) |
| `dsp/firmware.h` | the DSP's layers, the bypass and the dry signal |
| `cab/cabq.h` | the cab IR: a fixed-point partitioned FFT convolver (64-sample blocks, the control processor's integer DSP instructions; its float unit is too slow for it) |
| `default_capture.inc` | the built-in capture (see `CAPTURE-LICENSE.txt`) |
| `tools/a2r_export.py` | converts a `.nam` file into a capture file (`.a2r`) or a built-in one (`.inc`), with input calibration and output levelling (`--no-calibration`, `--no-level`); needs numpy and soundfile |
| `tools/make_cab.py` | converts a `.wav` IR into a cab file (the older float format, which the pedal still plays; Launchpad writes the 16-bit one); needs numpy, soundfile and scipy |
| `opengp5/` | the platform, a git submodule pinned to a tested release (0.8.0-alpha) |

To rebuild the built-in capture:

```
python3 tools/a2r_export.py A2.nam synth default_capture.inc --name "NAM A2 demo"
```

`synth` is a made-up calibration signal (`tools/calib_synth.py`) used to find each layer's range, in place of
NAM's own input recording.

## Licence

MIT (`LICENSE`). The built-in capture is NeuralAmpModelerCore's example A2 model, MIT
(`CAPTURE-LICENSE.txt`). Not affiliated with or endorsed by Valeton or the Neural Amp Modeler project.
"Valeton" and "GP-5" are Valeton's trademarks, used only to say which pedal this works with.
