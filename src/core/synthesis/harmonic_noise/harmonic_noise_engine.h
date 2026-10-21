#pragma once

#include "core/synthesis/engine/synthesis_engine.h"
#include "core/types/tracks.h"
#include "core/types/units.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>

// HarmonicNoiseEngine: the pitch-shift engine that keeps the residual (ROADMAP Phase 4; ADR-0016,
// ADR-0017). Design: docs/design/phase4-pitch-edit.md.

namespace anno {

/// A set of frame classes: canonical (no order, no duplicates), so equal sets give equal cache
/// keys.
class FrameClassSet {
public:
    constexpr FrameClassSet() noexcept = default;
    constexpr FrameClassSet(std::initializer_list<FrameClass> classes) noexcept {
        for (const FrameClass c : classes) {
            bits_ |= std::uint32_t{1} << static_cast<unsigned>(c);
        }
    }
    [[nodiscard]] constexpr bool contains(FrameClass c) const noexcept {
        return (bits_ >> static_cast<unsigned>(c) & 1U) != 0U;
    }
    [[nodiscard]] constexpr std::uint32_t bits() const noexcept { return bits_; }
    friend constexpr bool operator==(FrameClassSet, FrameClassSet) noexcept = default;

private:
    std::uint32_t bits_ = 0;
};

struct HarmonicNoiseEngineParams {
    // Frames of these classes are shifted; every other frame keeps the original samples (research
    // routing, ADR-0017). The empty set shifts nothing (the output is the original). Provisional:
    // voiced consonants are not a class of their own yet. Phase 4 shifts ALL measured harmonics of
    // a breathy frame; the "harmonics below the maximum voiced frequency only" clause of ADR-0017
    // needs the MVF per frame, which the Note does not store (deferred, ADR-0017 amendment).
    FrameClassSet shifted_classes{FrameClass::voiced, FrameClass::breathy};
    // Equal-power crossfade between the original and the shifted harmonics where a shifted run
    // meets an unshifted one inside a note; >= 0, finite. The renderer, not this fade, handles the
    // note's own outer edges when the edge frame is shifted (see render_note).
    Seconds class_edge_fade{0.004};
    // The shifted harmonics leave the audible band smoothly: their gain falls from 1 to 0 between
    // these fractions of the sample rate (dimensionless, 0 < start < end <= 0.5). Applies to the
    // shifted harmonics only, never to the original model.
    double taper_start_of_rate = 0.40;
    double taper_end_of_rate = 0.45;
};

/// Shifts the whole pitch curve of a note by eff.center - note.pitch_center (a constant number of
/// cents) and leaves everything else of the recording where it is:
///   out = x - (1 - cos(pi u / 2)) * H_original + sin(pi u / 2) * H_shifted      (u = w(t), 0..1)
/// where x is the original audio of the zone, H_original the additive resynthesis of the note's
/// measured harmonics (McAulay-Quatieri cubic phase), H_shifted the same harmonics moved by the
/// shift with amplitudes re-read from the note's envelope at the new frequencies (formants stay),
/// and w is 1 on frames of a shifted class that have at least one measured harmonic, 0 on all other
/// frames, with raised-cosine edges of class_edge_fade (the sine/cosine pair makes the fade between
/// the two harmonic models equal-power). The residual x - H_original (breath, rasp, consonant
/// noise) is never moved. Wherever w == 0 the engine copies the read_through_map samples and does
/// not evaluate the formula, so those samples are the original bits (a -0.0 stays -0.0).
///
/// Margin: the note's tracks cover only the note. Beyond the first and last frame the model is held
/// (frequency and amplitude as the edge frame's, phase running on) when that frame is shifted, so
/// the zone's margin continues the edited run and the renderer's ramp (ADR-0006) is the only fade
/// at the note's outer edge. When the edge frame is not shifted (consonant, silence) or has no
/// measured harmonic, the margin on that side is the original samples and the run's inner edge
/// fades over class_edge_fade instead.
///
/// Supported: mono sources; effective values neutral except `center` and `onset_shift` (which the
/// TimeMap realises); a zone inside one slope-1 warp of the time map; an active note whose
/// harmonic, noise-class and envelope tracks share the note's grid. A shift within
/// k_neutral_snap_offset returns the original samples exactly.
/// Errors: not_implemented (stereo, any other non-neutral edit, a warp that is not slope 1 over
/// the zone, an inactive note, one without harmonic or envelope tracks), invalid_argument (a bad
/// range, bad params, a non-finite center, tracks that disagree in frame count), out_of_range (from
/// the source). Const; thread-safe; not real-time safe (allocates).
class HarmonicNoiseEngine final : public SynthesisEngine {
public:
    explicit HarmonicNoiseEngine(HarmonicNoiseEngineParams params = {});

    [[nodiscard]] std::string_view name() const noexcept override { return "harmonic_noise"; }
    [[nodiscard]] std::uint32_t version() const noexcept override { return 1; }
    /// A fixed layout: a format tag, the class-set bits and the bit pattern of every double in hex,
    /// so equal params give equal bytes on every platform and locale.
    [[nodiscard]] std::string settings_key() const override;

    [[nodiscard]] Result<OutputAudio> render_note(const AudioSource& src, const Note& note,
                                                  const EffectiveNote& eff, const TimeMap& time_map,
                                                  OutputRange out_range) const override;

private:
    HarmonicNoiseEngineParams params_;
};

}  // namespace anno
