# Rubber Band Library as used in Annodyne

Source: https://github.com/breakfastquay/rubberband, commit `e4296ac80b1170018a110bc326fd0d45a0eb27d6` (4.0.0 line, 2025-02-27).
Authors: Chris Cannam and Particular Programs Ltd (Breakfast Quay).
Licence: GNU General Public License, version 2 or (at your option) any later version (`COPYING`); a commercial licence exists upstream for other terms. Annodyne follows the GPL-2.0-or-later consequence of ADR-0014 (no closed edition). Decision: ADR-0026.

Vendored: `rubberband/` (public headers), `src/` (without `src/test` and `src/jni`), `single/RubberBandSingle.cpp` (the single-file build: built-in FFT and resampler, vDSP/Accelerate FFT on Apple, no threading), `COPYING`, `README.md`, `CHANGELOG`. **No file is modified.**

Built by `cmake/AnnoRubberBand.cmake` as `anno_rubberband`; only `core/synthesis/stretch` may link it (AnnoLayers.cmake allowlist), privately.
