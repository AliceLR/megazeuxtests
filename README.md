# MegaZeux utilities and benchmark worlds

This repository contains some miscellaneous utilities primarily intended for [MegaZeux](https://github.com/AliceLR/megazeux/) and [libxmp](https://github.com/libxmp/libxmp/) development, as well as testing/benchmark worlds for MegaZeux.

## Utilities

This repository is currently the home of a handful of utilities which all need better names. These are mostly written with libxmp or misc. archival tasks in mind:

* `modutil` dumps a bunch of information about module formats I've had to work with developing libxmp, most (but not all) of which are formats supported by MegaZeux. Some of the information printed is actually useful!
  * MOD, S3M, XM, IT
  * Other DOS formats: 669, DSMI AMF, ASYLUM AMF, DSIK DSM, FAR, GDM, MTM, Epic MegaGames PSM (both), STM, ULT
  * Other Amiga formats: DIGI Booster Pro, MED (partial support), Oktalyzer, 
  * RISC OS formats: Archimedes !Tracker, Coconizer, Desktop Tracker, Digital Symphony
  * `iffdump` is a testing program for `modutil`'s IFF support that sometimes is also useful debugging new IFF-based formats.
* `dimgutil`
  * Lighter testing programs written in C: `unarc` and `unarcfs`

All source code in `src/` is available under the ISC license, which is equivalent to MIT and BSD-2 but with slightly less boilerplate.

## MegaZeux testing/benchmark worlds

* `graphics/` contains worlds primarily testing renderer functionality.
* `joystick/` contains worlds for testing joystick support.
* `music/` contains worlds for testing different audio formats.
* `ram/` contains worlds for testing various high-memory things and triggering out of memory errors.

All MegaZeux worlds and loose Robotic source should be considered to be under the ISC license, but not all of them have a notice internally. The module files are here for *testing purposes* and are all subject to their own various copyrights that really need to be scrutinized better (see CREDITS.md).
