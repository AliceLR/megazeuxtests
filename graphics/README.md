# MegaZeux renderer benchmarks.

This folder contains worlds useful for benchmarking MZX renderers.
Worlds should generally support the earliest port version required (2.80X for simple tests and 2.91X for tests requiring layer rendering or preset vlayer/SMZX data).
This collection of tests should not be considered "complete".

| File | Info |
|------|------|
| `#safe.mzx`              | Does nothing. This is first in the list so it's quicker to switch to when there is UI lag.
| `3dscrashbud.mzx`        | This world was a simple way of inducing crashes in the 3DS CTR renderer prior to 2.92b.
| `abuse0.mzx`             | Creates 256 large sprites with constantly changing color/transparency data. Originally designed to fill the Wii GX renderer's FIFO.
| `abuse0_align64.mzx`     | `abuse0.mzx` but all sprites are aligned to even pixels. Intended to test the 64-aligned 32bpp software layer renderers.
| `abuse0_align_char.mzx`  | `abuse0.mzx` but all sprites are aligned to char boundaries. Intended to test the 64-aligned 8bpp software layer renderers.
| `abuse1.mzx`             | `abuse0.mzx` but in SMZX mode 1.
| `abuse1_align64.mzx`     | `abuse1.mzx` but all sprites are aligned to even pixels.
| `abuse3.mzx`             | `abuse0.mzx` but in SMZX mode 3.
| `abuse3_align64.mzx`     | `abuse3.mzx` but all sprites are aligned to even pixels.
| `lancer.mzx`             | Uses an array of 56 sprites in SMZX mode to create a 320x350 256-color display a la Lancer-X's 2017 "eftlt" demo.
| `logicow.mzx`            | Uses an array of 112 sprites in MZX mode to create a 640x350 16-color display a la Logicow's "Anticapitalizard".
| `mzx_speed_1.mzx`        | Mostly blank board.
| `mzx_speed_1_layer.mzx`  | Mostly blank board with a small unbound sprite.
| `smzx_message.mzx`       | Mostly blank board in SMZX mode 3 with smzx_message=0 (forcing the UI layer to render).
| `smzx_speed_1.mzx`       | Mostly blank board in SMZX mode 2.
| `smzx_speed_1_layer.mzx` | Mostly blank board in SMZX mode 3 with a small unbound sprite.
| `smzx_speed_1_pal.mzx`   | Mostly blank board in SMZX mode 2 with a forced palette update every cycle.

## TODO

* Add a test for various degrees of character set updates in MZX and SMZX modes since this causes a non-trivial load in the `glsl` and `opengl2` renderers.
