# NAM Loader

**A capture player firmware for the Valeton GP-5, built on [OpenGP5](https://github.com/OpenGP5/OpenGP5).**
Version 1.0.0, on OpenGP5 0.5.5-alpha.

It plays [Neural Amp Modeler](https://github.com/sdatkinson/neural-amp-modeler) A2 captures (their A2-Lite
part) on the pedal itself. The model is split across both of the pedal's processors: the control processor runs
the first layers, the DSP the rest and the output. You keep up to 12 captures on the pedal and switch between
them with the PARA knob.

> **Alpha.** Flashing custom firmware can go wrong. Read OpenGP5's
> [getting-started guide](https://github.com/OpenGP5/OpenGP5/blob/main/GETTING-STARTED.md), especially
> *Recovery*, before you flash.

## Controls

| control | does |
|---|---|
| turn PARA | browse the 12 slots (the capture playing keeps playing); after 5 s without turning, back to the one playing |
| push PARA | load the highlighted capture, or start browsing |
| foot switch | bypass |
| volume knob | output level |

The screen shows the capture's name (a long name slides to its end and back), the slot, an input meter and the
volume. The slot playing is remembered across power-off.

## Getting captures onto the pedal

Use [OpenGP5 Launchpad](https://github.com/OpenGP5/Launchpad): its Tones page takes a `.nam` file (A2), converts
its A2-Lite part and writes it into a slot. A1 (older) captures can't be played. With the bank empty, the
pedal plays its built-in capture, "NAM A2 demo".

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
| `split.h` | where the model is split, the slot bank, and the audio link's words; shared by both sides |
| `nds32/a2.c` | the control processor's layers, run in the audio link's interrupt; the slot bank and switching (fade out, load, fade in) |
| `nds32/firmware.c` | the controls and the yellow 160 x 80 screen |
| `dsp/firmware.h` | the DSP's layers, the output and the bypass |
| `default_capture.inc` | the built-in capture (see `CAPTURE-LICENSE.txt`) |
| `tools/a2r_export.py` | converts a `.nam` file into a capture file (`.a2r`) or a built-in one (`.inc`); needs numpy and soundfile |
| `opengp5/` | the platform, a git submodule pinned to a tested release (0.5.5-alpha) |

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
