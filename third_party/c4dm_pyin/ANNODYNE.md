# c4dm/pyin as used in Annodyne

Source: https://github.com/c4dm/pyin, commit `1d68cac9c5ecb1d81054d25d90158e5838dd78c2` (tag v1.2).
Authors: Matthias Mauch and others, Centre for Digital Music, Queen Mary, University of London.
Licence: GNU General Public License, version 2 or (at your option) any later version (`COPYING`).
The authors ask that public or commercial use credit them and Queen Mary, University of London in
user-visible documentation (upstream `README`); Annodyne does so in `THIRD_PARTY.md` and must do so
in the product's about/credits screen. Decision and consequences: ADR-0014.

Vendored files (the pitch-tracking core, without the Vamp plugin glue and without the note HMM):
`Yin.{h,cpp}`, `YinUtil.{h,cpp}`, `MonoPitchHMM.{h,cpp}`, `SparseHMM.{h,cpp}`, `MeanFilter.h`,
`COPYING`, `README`.

## Modifications (the only changes to upstream files)
1. `MonoPitchHMM.h` and `MonoPitchHMM.cpp`: the `#include <boost/math/distributions.hpp>` line is
   removed. Nothing in these files uses Boost; the note HMM (which does) is not vendored.

## Known upstream behaviour kept as it is
- `YinUtil::fastDifference` updates the running power term with `in[tau + N]` where the exact sum
  needs `in[tau - 1 + N]`; the difference function is off by a small amount. The shim test uses the
  same recurrence so the FFT part is still checked exactly.
- `YinUtil::parabolicInterpolation` calls `abs(adjustment)` on a double, which resolves to the
  integer overload on some standard libraries (the guard `> 1` then never fires).

## Additions (not upstream)
- `vamp-sdk/FFT.h`: a small replacement for `Vamp::FFTReal` on top of pocketfft, so the Vamp SDK is
  not needed. Checked by `tests/unit/c4dm_pyin_test.cpp` against a brute-force difference function.
- `ANNODYNE.md`: this file.

The pipeline that upstream keeps in `PYinVamp.cpp` (framing, the low-amplitude penalty, feeding the
pitch HMM, `nearestFreq`) is re-implemented in `src/core/analysis/pitch/c4dm_pyin.cpp`, which is
therefore a derivative work and is covered by the same licence.
