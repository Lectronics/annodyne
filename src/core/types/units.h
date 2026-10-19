#pragma once

#include <compare>
#include <concepts>
#include <cstdint>

// Strong unit types (CLAUDE.md rules 3-4, ADR-0003). Construction from a raw number is explicit,
// nothing converts implicitly, and the two time axes cannot be mixed. Raw values are read with
// value(); conversions between units are the named functions at the end of this file.
// Design: docs/design/phase0-foundations.md §4.

namespace anno {

struct RatioTag;

/// A value of one unit. Same-unit arithmetic, scaling by a raw number, and Quantity / Quantity
/// (a dimensionless Ratio). Pure, constexpr, trivially copyable.
template <class Tag, class Rep>
class Quantity {
public:
    using rep = Rep;
    using tag = Tag;

    constexpr Quantity() noexcept = default;  // zero
    constexpr explicit Quantity(Rep v) noexcept : v_{v} {}

    [[nodiscard]] constexpr Rep value() const noexcept { return v_; }

    constexpr Quantity& operator+=(Quantity other) noexcept {
        v_ += other.v_;
        return *this;
    }
    constexpr Quantity& operator-=(Quantity other) noexcept {
        v_ -= other.v_;
        return *this;
    }

    [[nodiscard]] friend constexpr Quantity operator+(Quantity a, Quantity b) noexcept {
        return Quantity{a.v_ + b.v_};
    }
    [[nodiscard]] friend constexpr Quantity operator-(Quantity a, Quantity b) noexcept {
        return Quantity{a.v_ - b.v_};
    }
    [[nodiscard]] friend constexpr Quantity operator-(Quantity a) noexcept {
        return Quantity{-a.v_};
    }

    [[nodiscard]] friend constexpr Quantity operator*(Quantity a, Rep scale) noexcept {
        return Quantity{a.v_ * scale};
    }
    [[nodiscard]] friend constexpr Quantity operator*(Rep scale, Quantity a) noexcept {
        return Quantity{scale * a.v_};
    }
    [[nodiscard]] friend constexpr Quantity operator/(Quantity a, Rep divisor) noexcept {
        return Quantity{a.v_ / divisor};
    }
    /// Always a floating-point ratio, also for Samples (no integer truncation).
    [[nodiscard]] friend constexpr Quantity<RatioTag, double> operator/(Quantity a,
                                                                        Quantity b) noexcept {
        return Quantity<RatioTag, double>{static_cast<double>(a.v_) / static_cast<double>(b.v_)};
    }

    friend constexpr auto operator<=>(Quantity, Quantity) noexcept = default;
    friend constexpr bool operator==(Quantity, Quantity) noexcept = default;

private:
    Rep v_{};
};

using Ratio = Quantity<RatioTag, double>;                   // dimensionless multiplier
using Seconds = Quantity<struct SecondsTag, double>;        // duration, axis-free
using Samples = Quantity<struct SamplesTag, std::int64_t>;  // frame count/offset, axis-free
using Cents = Quantity<struct CentsTag, double>;            // absolute (A4 = 6900) or interval
using Decibels = Quantity<struct DecibelsTag, double>;      // relative gain or dBFS
using LoudnessUnits =
    Quantity<struct LoudnessUnitsTag, double>;              // BS.1770: LU, or LUFS if absolute
using Hz = Quantity<struct HzTag, double>;                  // frequency and sample rate
using LinearGain = Quantity<struct LinearGainTag, double>;  // amplitude factor, 1 = unity

/// Scaling a floating-point quantity by a Ratio, e.g. Cents * drift_scale, Seconds * length_scale.
/// Not available for Ratio itself (see the overloads below) or for Samples: scale time in Seconds
/// and convert with an explicit Rounding.
template <class Tag>
    requires(!std::same_as<Tag, RatioTag>)
[[nodiscard]] constexpr Quantity<Tag, double> operator*(Quantity<Tag, double> q, Ratio r) noexcept {
    return q * r.value();
}
template <class Tag>
    requires(!std::same_as<Tag, RatioTag>)
[[nodiscard]] constexpr Quantity<Tag, double> operator*(Ratio r, Quantity<Tag, double> q) noexcept {
    return r.value() * q;
}
template <class Tag>
    requires(!std::same_as<Tag, RatioTag>)
[[nodiscard]] constexpr Quantity<Tag, double> operator/(Quantity<Tag, double> q, Ratio r) noexcept {
    return q / r.value();
}
/// Ratio * Ratio. (Ratio / Ratio is the generic Quantity / Quantity.)
[[nodiscard]] constexpr Ratio operator*(Ratio a, Ratio b) noexcept {
    return Ratio{a.value() * b.value()};
}

/// A point on one time axis. Affine: point - point = Seconds, point +/- Seconds = point;
/// point + point and mixing axes do not compile. Cross-axis only through a TimeMap.
template <class Axis>
class TimePoint {
public:
    using axis = Axis;

    constexpr TimePoint() noexcept = default;  // the axis origin

    /// Escape hatch to raw seconds. Allowed only in core/types and core/timemap (enforced by
    /// scripts/check_includes.py). Everyone else uses the conversions below.
    [[nodiscard]] static constexpr TimePoint unchecked_from_origin(Seconds offset) noexcept {
        TimePoint point;
        point.offset_ = offset;
        return point;
    }
    [[nodiscard]] constexpr Seconds unchecked_since_origin() const noexcept { return offset_; }

    constexpr TimePoint& operator+=(Seconds d) noexcept {
        offset_ += d;
        return *this;
    }
    constexpr TimePoint& operator-=(Seconds d) noexcept {
        offset_ -= d;
        return *this;
    }

    [[nodiscard]] friend constexpr Seconds operator-(TimePoint a, TimePoint b) noexcept {
        return a.offset_ - b.offset_;
    }
    [[nodiscard]] friend constexpr TimePoint operator+(TimePoint a, Seconds d) noexcept {
        a += d;
        return a;
    }
    [[nodiscard]] friend constexpr TimePoint operator+(Seconds d, TimePoint a) noexcept {
        a += d;
        return a;
    }
    [[nodiscard]] friend constexpr TimePoint operator-(TimePoint a, Seconds d) noexcept {
        a -= d;
        return a;
    }

    friend constexpr auto operator<=>(TimePoint, TimePoint) noexcept = default;
    friend constexpr bool operator==(TimePoint, TimePoint) noexcept = default;

private:
    Seconds offset_{};
};

struct SourceAxis;
struct OutputAxis;
using SourceTime = TimePoint<SourceAxis>;  // seconds in the original recording
using OutputTime = TimePoint<OutputAxis>;  // seconds after timing edits

inline constexpr Cents k_a4_cents{6900.0};
inline constexpr Hz k_a4_standard_hz{440.0};
/// Returned by to_decibels for (near-)zero amplitude, so no -inf ever leaves a conversion.
inline constexpr Decibels k_silence_floor_db{-200.0};
/// Amplitude corresponding to k_silence_floor_db: 10^(-200/20).
inline constexpr LinearGain k_silence_floor_gain{1e-10};

/// How a fractional sample position becomes an integer. nearest rounds halves away from zero.
/// down/up act on the exact double product, so 0.1 s * 48 kHz (4800.000000000001) rounds up to
/// 4801: use nearest unless a boundary must be strictly inside or outside a range.
enum class Rounding { nearest, down, up };

// ---- Conversions --------------------------------------------------------------------------------
// All pure, thread-safe and real-time safe. Preconditions are checked with ANNO_EXPECTS (always
// on). Not constexpr: C++20 <cmath> is not constexpr.

/// Duration to a whole number of frames. Pre: d finite; sample_rate finite and > 0;
/// |d * sample_rate| < 2^62.
[[nodiscard]] Samples to_samples(Seconds d, Hz sample_rate, Rounding rounding) noexcept;

/// Frames to duration. Pre: sample_rate finite and > 0.
[[nodiscard]] Seconds to_seconds(Samples n, Hz sample_rate) noexcept;

/// Sample index of a time point, counted from the axis origin (e.g. the start of the recording for
/// SourceTime, as AudioSource::read expects). Same preconditions as to_samples.
template <class Axis>
[[nodiscard]] Samples to_sample_index(TimePoint<Axis> t, Hz sample_rate,
                                      Rounding rounding) noexcept {
    return to_samples(t.unchecked_since_origin(), sample_rate, rounding);
}

/// Time point of a sample index counted from the axis origin. Same preconditions as to_seconds.
template <class Axis>
[[nodiscard]] TimePoint<Axis> to_time_point(Samples index, Hz sample_rate) noexcept {
    return TimePoint<Axis>::unchecked_from_origin(to_seconds(index, sample_rate));
}

/// Absolute pitch in cents (A4 = 6900) to frequency: a4 * 2^((pitch - 6900) / 1200).
/// The reference is explicit so a detected tuning (AnalysisResult::tuning_ref) can't be
/// ignored silently; pass k_a4_standard_hz for 440 Hz. Pre: pitch finite; a4_reference finite > 0.
[[nodiscard]] Hz to_hz(Cents pitch, Hz a4_reference) noexcept;

/// Frequency to absolute pitch in cents (A4 = 6900). Pre: f finite and > 0; a4_reference finite >
/// 0.
[[nodiscard]] Cents to_cents(Hz f, Hz a4_reference) noexcept;

/// Frequency ratio of an interval: 2^(interval / 1200). Pre: interval finite.
[[nodiscard]] Ratio to_ratio(Cents interval) noexcept;

/// Amplitude factor of a gain: 10^(g / 20). Pre: g finite.
[[nodiscard]] LinearGain to_linear_gain(Decibels g) noexcept;

/// Level of an amplitude: 20 log10(|a|). The sign is ignored (polarity is not level).
/// |a| <= k_silence_floor_gain gives k_silence_floor_db. Pre: a is not NaN and not infinite.
[[nodiscard]] Decibels to_decibels(LinearGain a) noexcept;

namespace unit_literals {

// For tests and constants: 1.5_s, 200.0_cents, -6.0_db, 440.0_hz, 512_samples, 0.5_ratio.
[[nodiscard]] constexpr Seconds operator""_s(long double v) noexcept {
    return Seconds{static_cast<double>(v)};
}
[[nodiscard]] constexpr Seconds operator""_s(unsigned long long v) noexcept {
    return Seconds{static_cast<double>(v)};
}
[[nodiscard]] constexpr Cents operator""_cents(long double v) noexcept {
    return Cents{static_cast<double>(v)};
}
[[nodiscard]] constexpr Cents operator""_cents(unsigned long long v) noexcept {
    return Cents{static_cast<double>(v)};
}
[[nodiscard]] constexpr Decibels operator""_db(long double v) noexcept {
    return Decibels{static_cast<double>(v)};
}
[[nodiscard]] constexpr Decibels operator""_db(unsigned long long v) noexcept {
    return Decibels{static_cast<double>(v)};
}
[[nodiscard]] constexpr Hz operator""_hz(long double v) noexcept {
    return Hz{static_cast<double>(v)};
}
[[nodiscard]] constexpr Hz operator""_hz(unsigned long long v) noexcept {
    return Hz{static_cast<double>(v)};
}
[[nodiscard]] constexpr Ratio operator""_ratio(long double v) noexcept {
    return Ratio{static_cast<double>(v)};
}
[[nodiscard]] constexpr Samples operator""_samples(unsigned long long v) noexcept {
    return Samples{static_cast<std::int64_t>(v)};
}

}  // namespace unit_literals

}  // namespace anno
