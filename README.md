# MegaZeux utilities and benchmark worlds

This repository contains some miscellaneous utilities primarily intended for [MegaZeux](https://github.com/AliceLR/megazeux/) and [libxmp](https://github.com/libxmp/libxmp/) development, as well as testing/benchmark worlds for MegaZeux.

## Utilities

This repository is currently the home of a handful of utilities which all need better names. These are mostly written with libxmp or misc. archival tasks in mind:

* `modutil` dumps a bunch of information about module formats I've had to work with developing libxmp, most (but not all) of which are formats supported by MegaZeux. Some of the information printed is actually useful!
  * MOD, S3M, XM, IT
  * Other DOS formats: 669, DSMI AMF, ASYLUM AMF, DSIK DSM, FAR, GDM, MTM, Epic MegaGames PSM (both), STM, ULT
  * Other Amiga formats: DIGI Booster Pro, MED (partial support), Oktalyzer
  * RISC OS formats: Archimedes !Tracker, Coconizer, Desktop Tracker, Digital Symphony
  * `iffdump` is a testing program for `modutil`'s IFF support that sometimes is also useful debugging new IFF-based formats.
* `dimgutil`
  * Lighter testing programs written in C: `unarc`, `unarcfs`, `unlzx`

All source code in `src/` is available under the MIT license rather than
MegaZeux's GPL 2.0+ license, as the vast majority of this code is old file
format and decompression support code that may be useful to permissive
licensed projects.

## MegaZeux testing/benchmark worlds

* `graphics/` contains worlds primarily testing renderer functionality.
* `joystick/` contains worlds for testing joystick support.
* `music/` contains worlds for testing different audio formats.
* `ram/` contains worlds for testing various high-memory things and triggering out of memory errors.

All MegaZeux worlds and loose Robotic source should be considered to be under
the MIT license, but not all of them have a notice internally. The module files
are here for *testing purposes* and are all subject to their own various
copyrights that really need to be scrutinized better (see CREDITS.md).

## License

Copyright (C) 2020-2022 Lachesis <petrifiedrowan@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
