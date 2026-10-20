# Third-party code

| Library | Used by | Licence | Source |
|---|---|---|---|
| Catch2 v3 | tests | BSL-1.0 | https://github.com/catchorg/Catch2 |
| nlohmann/json 3.11.3 | `cli/*`, tests (ADR-0007) | MIT | https://github.com/nlohmann/json |
| c4dm/pyin v1.2, commit 1d68cac9c5ecb1d81054d25d90158e5838dd78c2 (vendored in `third_party/c4dm_pyin/`) | `core/analysis/pitch` only (ADR-0014) | GPL-2.0-or-later | https://github.com/c4dm/pyin |
| pocketfft (C++), commit 4d865cba24359b05b37da4e75aee24498c3b78e7 | `core/dsp` only (ADR-0009) | BSD-3-Clause | https://github.com/mreineck/pocketfft |

| Vocadito dataset (audio + annotations; test data only, never linked or shipped) | `tests/golden/vocals/vocadito/` | CC BY 4.0 | https://zenodo.org/records/5578259 |

pocketfft: Copyright (C) 2010-2022 Max-Planck-Society, author Martin Reinecke. Redistribution and use in source and binary forms, with or without modification, are permitted provided that the copyright notice, the list of conditions and the following disclaimer are retained (full text in the upstream `LICENSE.md`).

Vocadito: R. M. Bittner, K. Pasalo, J. J. Bosch, G. Meseguer-Brocal, D. Rubinstein, "vocadito: A dataset of solo vocals with f0, note, and lyric annotations", arXiv:2110.05580, 2021; data on Zenodo, record 5578259. Licensed CC BY 4.0 (https://creativecommons.org/licenses/by/4.0/). We selected 5 of its 40 tracks and use them unmodified; the audio and annotations are the original authors' work and are used only as test data. (Author list and title taken from memory: verify against the Zenodo record before release.)

c4dm/pyin (pYIN pitch tracker): Copyright Matthias Mauch and others, Centre for Digital Music, Queen Mary, University of London. Licensed under the GNU General Public License, version 2 or (at your option) any later version; the licence text is `third_party/c4dm_pyin/COPYING`. Their request, repeated here: public or commercial use of this software should credit the authors and Queen Mary, University of London in user-visible documentation (the product's about/credits screen). Method: M. Mauch and S. Dixon, "pYIN: A fundamental frequency estimator using probabilistic threshold distributions", ICASSP 2014; YIN: A. de Cheveigne and H. Kawahara, JASA 2002. Annodyne's modifications are listed in `third_party/c4dm_pyin/ANNODYNE.md`. Because the product links this code, the product as a whole must be distributable under the GPL (ADR-0014).
