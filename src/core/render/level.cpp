#include "core/render/level.h"

#include "core/types/frame_grid.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace anno {

namespace {

double fade_shape_gain(FadeShape shape, double x) {
    if (x <= 0.0) {
        return 0.0;
    }
    if (x >= 1.0) {
        return 1.0;
    }
    switch (shape) {
    case FadeShape::linear: return x;
    case FadeShape::equal_power: return std::sin(0.5 * std::numbers::pi * x);
    case FadeShape::log: return std::pow(10.0, k_log_fade_floor_db.value() * (1.0 - x) / 20.0);
    }
    return x;
}

/// The flatten correction in dB at each frame centre (output samples), or empty if none applies.
struct FlattenPoints {
    std::vector<double> position;
    std::vector<double> db;
};

FlattenPoints flatten_points(const Note& note, double amount, const LevelZone& zone) {
    FlattenPoints out;
    if (amount <= 0.0 || note.grid.n_frames <= 0 ||
        note.amp.size() != static_cast<std::size_t>(note.grid.n_frames)) {
        return out;
    }
    double sum = 0.0;
    std::size_t count = 0;
    for (const Decibels a : note.amp) {
        if (std::isfinite(a.value())) {
            sum += a.value();
            ++count;
        }
    }
    if (count == 0) {
        return out;
    }
    const double rough_mean = sum / static_cast<double>(count);
    const double floor_db = rough_mean - k_flatten_floor_below_mean_db.value();
    sum = 0.0;
    count = 0;
    for (const Decibels a : note.amp) {
        if (std::isfinite(a.value()) && a.value() >= floor_db) {
            sum += a.value();
            ++count;
        }
    }
    const double mean = sum / static_cast<double>(count);
    for (std::int64_t i = 0; i < note.grid.n_frames; ++i) {
        const double a = note.amp[static_cast<std::size_t>(i)].value();
        const double seconds = (frame_time(note.grid, i) - SourceTime{}).value();
        out.position.push_back(seconds * zone.rate.value() +
                               static_cast<double>(zone.shift_samples));
        out.db.push_back(std::isfinite(a) && a >= floor_db
                             ? std::clamp((mean - a) * amount, -k_max_flatten_db.value(),
                                          k_max_flatten_db.value())
                             : 0.0);
    }
    return out;
}

double flatten_db_at(const FlattenPoints& p, double position) {
    if (p.position.empty()) {
        return 0.0;
    }
    if (position <= p.position.front()) {
        return p.db.front();
    }
    if (position >= p.position.back()) {
        return p.db.back();
    }
    const auto upper = std::ranges::upper_bound(p.position, position);
    const auto k = static_cast<std::size_t>(upper - p.position.begin());
    const double w = (position - p.position[k - 1]) / (p.position[k] - p.position[k - 1]);
    return (1.0 - w) * p.db[k - 1] + w * p.db[k];
}

}  // namespace

bool has_level_edit(const EffectiveNote& eff) noexcept {
    return eff.gain.value() != 0.0 || eff.within_note_flatten.value() != 0.0 ||
           eff.fade_in.value() != 0.0 || eff.fade_out.value() != 0.0;
}

EffectiveNote without_level(EffectiveNote eff) {
    eff.gain = Decibels{};
    eff.within_note_flatten = Ratio{};
    eff.fade_in = Seconds{};
    eff.fade_out = Seconds{};
    eff.fade_shape = FadeShape::linear;
    return eff;
}

Result<std::vector<float>> level_gain(const Note& note, const EffectiveNote& eff,
                                      const LevelZone& zone) {
    const auto bad = [](const char* what) {
        return fail(ErrorCode::invalid_argument, std::string{"level_gain: "} + what);
    };
    if (zone.count < 0 || zone.note_last < zone.note_first) {
        return bad("a negative zone or note length");
    }
    if (!std::isfinite(zone.rate.value()) || !(zone.rate.value() > 0.0)) {
        return bad("the sample rate must be finite and > 0");
    }
    if (!std::isfinite(eff.gain.value()) || !std::isfinite(eff.fade_in.value()) ||
        !std::isfinite(eff.fade_out.value()) || !std::isfinite(eff.within_note_flatten.value()) ||
        eff.fade_in.value() < 0.0 || eff.fade_out.value() < 0.0) {
        return bad("a non-finite or negative level value");
    }
    const double gain_linear =
        eff.gain.value() <= -120.0 ? 0.0 : std::pow(10.0, eff.gain.value() / 20.0);
    const auto note_length = static_cast<double>(zone.note_last - zone.note_first);
    double fade_in = eff.fade_in.value() * zone.rate.value();
    double fade_out = eff.fade_out.value() * zone.rate.value();
    if (fade_in + fade_out > note_length && fade_in + fade_out > 0.0) {
        const double scale = note_length / (fade_in + fade_out);
        fade_in *= scale;
        fade_out *= scale;
    }
    const FlattenPoints flatten =
        flatten_points(note, std::clamp(eff.within_note_flatten.value(), 0.0, 1.0), zone);
    std::vector<float> out(static_cast<std::size_t>(zone.count));
    for (std::int64_t i = 0; i < zone.count; ++i) {
        const std::int64_t n = zone.first + i;
        double g = gain_linear;
        if (fade_in > 0.0) {
            g *=
                fade_shape_gain(eff.fade_shape, static_cast<double>(n - zone.note_first) / fade_in);
        }
        if (fade_out > 0.0) {
            g *=
                fade_shape_gain(eff.fade_shape, static_cast<double>(zone.note_last - n) / fade_out);
        }
        const double db = flatten_db_at(flatten, static_cast<double>(n));
        if (db != 0.0) {
            g *= std::pow(10.0, db / 20.0);
        }
        out[static_cast<std::size_t>(i)] = static_cast<float>(g);
    }
    return out;
}

Result<void> apply_gain(AudioBuffer& audio, std::span<const float> gain) {
    if (static_cast<std::int64_t>(gain.size()) != audio.frame_count().value()) {
        return fail(ErrorCode::invalid_argument, "apply_gain: one gain per frame is required");
    }
    for (int c = 0; c < audio.channel_count(); ++c) {
        auto channel = audio.channel(c);
        for (std::size_t i = 0; i < channel.size(); ++i) {
            channel[i] =
                static_cast<float>(static_cast<double>(channel[i]) * static_cast<double>(gain[i]));
        }
    }
    return {};
}

}  // namespace anno
