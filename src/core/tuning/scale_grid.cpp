#include "core/tuning/scale_grid.h"

#include <algorithm>
#include <cmath>

namespace anno {

namespace {

constexpr double k_semitone_cents = 100.0;
constexpr int k_pitch_classes = 12;
constexpr int k_a_pitch_class = 9;  // C = 0
constexpr int k_search_semitones = 12;

Result<void> check_grid(const ScaleGrid& g) {
    if (!std::isfinite(g.a4_reference.value()) || g.a4_reference.value() <= 0.0) {
        return fail(ErrorCode::invalid_argument,
                    "target_pitch: a4_reference must be finite and > 0");
    }
    if (g.root < 0 || g.root >= k_pitch_classes) {
        return fail(ErrorCode::invalid_argument, "target_pitch: root must be a pitch class 0..11");
    }
    if (g.degrees.empty() || g.degrees.front() < 0 || g.degrees.back() >= k_pitch_classes ||
        std::ranges::adjacent_find(g.degrees, std::greater_equal<>{}) != g.degrees.end()) {
        return fail(ErrorCode::invalid_argument,
                    "target_pitch: degrees must be non-empty, strictly ascending and within 0..11");
    }
    return {};
}

}  // namespace

std::vector<int> scale_degrees(ScaleType type) {
    switch (type) {
    case ScaleType::chromatic: return {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11};
    case ScaleType::major: return {0, 2, 4, 5, 7, 9, 11};
    case ScaleType::natural_minor: return {0, 2, 3, 5, 7, 8, 10};
    case ScaleType::harmonic_minor: return {0, 2, 3, 5, 7, 8, 11};
    case ScaleType::major_pentatonic: return {0, 2, 4, 7, 9};
    case ScaleType::minor_pentatonic: return {0, 3, 5, 7, 10};
    case ScaleType::dorian: return {0, 2, 3, 5, 7, 9, 10};
    case ScaleType::mixolydian: return {0, 2, 4, 5, 7, 9, 10};
    }
    return {0, 2, 4, 5, 7, 9, 11};
}

Result<ScaleGrid> make_scale_grid(ScaleType type, int root, Hz a4_reference) {
    ScaleGrid grid{.a4_reference = a4_reference, .root = root, .degrees = scale_degrees(type)};
    if (auto ok = check_grid(grid); !ok) {
        return Unexpected{ok.error()};
    }
    return grid;
}

Result<Cents> target_pitch(Cents pitch, const ScaleGrid& grid) {
    if (!std::isfinite(pitch.value())) {
        return fail(ErrorCode::invalid_argument, "target_pitch: the pitch is not finite");
    }
    if (auto ok = check_grid(grid); !ok) {
        return Unexpected{ok.error()};
    }
    const double a4 = to_cents(grid.a4_reference, k_a4_standard_hz).value();
    const double nearest = std::floor((pitch.value() - a4) / k_semitone_cents + 0.5);
    double best = nearest;
    double best_distance = 1e300;
    // Semitone s from A4 has pitch class (9 + s) mod 12; it is a scale point when that class is
    // root + a degree. The nearest chromatic point is at most half a semitone away, and every scale
    // has a gap of at most 11 semitones, so +-12 semitones around it hold the answer. Equal
    // distances keep the later (higher) candidate.
    for (int offset = -k_search_semitones; offset <= k_search_semitones; ++offset) {
        const double s = nearest + offset;
        const int pitch_class = static_cast<int>(std::lround(
            std::fmod(std::fmod(s + k_a_pitch_class - grid.root, k_pitch_classes) + k_pitch_classes,
                      k_pitch_classes)));
        if (!std::ranges::binary_search(grid.degrees, pitch_class)) {
            continue;
        }
        const double distance = std::abs(a4 + s * k_semitone_cents - pitch.value());
        if (distance <= best_distance) {
            best_distance = distance;
            best = s;
        }
    }
    return Cents{a4 + best * k_semitone_cents};
}

}  // namespace anno
