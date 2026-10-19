#include "core/types/time_map.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <ranges>

namespace anno {

namespace {

double raw(SourceTime t) {
    return t.unchecked_since_origin().value();
}
double raw(OutputTime t) {
    return t.unchecked_since_origin().value();
}

SourceTime make_source(double v) {
    return SourceTime::unchecked_from_origin(Seconds{v});
}
OutputTime make_output(double v) {
    return OutputTime::unchecked_from_origin(Seconds{v});
}

double source_start_of(const TimeSpan& s) {
    return raw(s.source_start);
}
double output_start_of(const TimeSpan& s) {
    return raw(s.output_start);
}
double source_length_of(const TimeSpan& s) {
    return raw(s.source_end) - raw(s.source_start);
}
double output_length_of(const TimeSpan& s) {
    return raw(s.output_end) - raw(s.output_start);
}

bool all_finite(const TimeSpan& s) {
    return std::isfinite(raw(s.source_start)) && std::isfinite(raw(s.source_end)) &&
           std::isfinite(raw(s.output_start)) && std::isfinite(raw(s.output_end));
}

TimeSpan warp(double s0, double s1, double t0, double t1) {
    return TimeSpan{.kind = SpanKind::warp,
                    .source_start = make_source(s0),
                    .source_end = make_source(s1),
                    .output_start = make_output(t0),
                    .output_end = make_output(t1),
                    .segment = std::nullopt};
}

Result<void> check_shape(const TimeSpan& s) {
    const double src = source_length_of(s);
    const double out = output_length_of(s);
    const bool ok = (s.kind == SpanKind::warp && src > 0.0 && out > 0.0) ||
                    (s.kind == SpanKind::cut && src > 0.0 && out == 0.0) ||
                    (s.kind == SpanKind::insert && src == 0.0 && out > 0.0);
    if (!ok) {
        return fail(ErrorCode::invalid_argument,
                    "time map span has the wrong shape for its kind (warp: both ranges positive; "
                    "cut: source only; insert: output only)");
    }
    return {};
}

}  // namespace

TimeMap::TimeMap(std::vector<TimeSpan> spans, Seconds source_length, Seconds output_length,
                 bool identity) noexcept
    : spans_{std::move(spans)}, source_length_{source_length}, output_length_{output_length},
      identity_{identity} {}

TimeMap TimeMap::identity(Seconds source_length) {
    const double length = source_length.value();
    if (!(length > 0.0)) {
        return TimeMap{{}, Seconds{0.0}, Seconds{0.0}, true};
    }
    return TimeMap{{warp(0.0, length, 0.0, length)}, source_length, source_length, true};
}

Result<TimeMap> TimeMap::make(std::vector<TimeSpan> spans) {
    double source_cursor = 0.0;
    double output_cursor = 0.0;
    for (const TimeSpan& s : spans) {
        if (!all_finite(s)) {
            return fail(ErrorCode::invalid_argument, "time map span time is not finite");
        }
        if (raw(s.source_start) != source_cursor || raw(s.output_start) != output_cursor) {
            return fail(ErrorCode::invalid_argument,
                        "time map spans must tile both axes from (0, 0) without gaps or overlaps");
        }
        if (auto r = check_shape(s); !r) {
            return Unexpected{r.error()};
        }
        source_cursor = raw(s.source_end);
        output_cursor = raw(s.output_end);
    }
    const bool identity = spans.empty() || (spans.size() == 1 && spans[0].kind == SpanKind::warp &&
                                            source_cursor == output_cursor);
    return TimeMap{std::move(spans), Seconds{source_cursor}, Seconds{output_cursor}, identity};
}

Result<TimeMap> TimeMap::from_anchors(std::vector<TimePair> anchors, Seconds source_length) {
    const double length = source_length.value();
    if (anchors.empty()) {
        return fail(ErrorCode::invalid_argument, "a time map needs at least one anchor");
    }
    if (!std::isfinite(length) || length < 0.0) {
        return fail(ErrorCode::invalid_argument, "time map source length is not valid");
    }
    bool all_equal = true;
    for (std::size_t i = 0; i < anchors.size(); ++i) {
        const double s = raw(anchors[i].source);
        const double t = raw(anchors[i].output);
        if (!std::isfinite(s) || !std::isfinite(t)) {
            return fail(ErrorCode::invalid_argument, "time map anchor is not finite");
        }
        if (i == 0 ? (s < 0.0 || (s == 0.0 && t < 0.0))
                   : (s <= raw(anchors[i - 1].source) || t <= raw(anchors[i - 1].output))) {
            return fail(ErrorCode::invalid_argument,
                        "time map anchors must be strictly increasing in source and output and "
                        "start at or after (0, 0)");
        }
        all_equal = all_equal && s == t;
    }
    if (raw(anchors.back().source) > length) {
        return fail(ErrorCode::invalid_argument, "a time map anchor lies beyond the source length");
    }
    if (all_equal) {
        return identity(source_length);
    }
    std::vector<TimeSpan> spans;
    spans.reserve(anchors.size() + 1);
    double s0 = 0.0;
    double t0 = 0.0;
    if (raw(anchors.front().source) == 0.0 && raw(anchors.front().output) > 0.0) {
        // The audio at source 0 starts later than output 0: silence before it.
        t0 = raw(anchors.front().output);
        spans.push_back(TimeSpan{.kind = SpanKind::insert,
                                 .source_start = make_source(0.0),
                                 .source_end = make_source(0.0),
                                 .output_start = make_output(0.0),
                                 .output_end = make_output(t0),
                                 .segment = std::nullopt});
    }
    for (const TimePair& a : anchors) {
        const double s1 = raw(a.source);
        const double t1 = raw(a.output);
        if (s1 > s0) {
            spans.push_back(warp(s0, s1, t0, t1));
        }
        s0 = s1;
        t0 = t1;
    }
    if (s0 < length) {
        spans.push_back(warp(s0, length, t0, t0 + (length - s0)));
    }
    return make(std::move(spans));
}

std::optional<OutputTime> TimeMap::to_output(SourceTime s) const noexcept {
    if (identity_) {
        return make_output(raw(s));
    }
    const double length = source_length_.value();
    assert(raw(s) >= 0.0 && raw(s) <= length);
    const double x = std::clamp(raw(s), 0.0, length);
    if (x == length) {
        return make_output(output_length_.value());
    }
    // First span with source_start > x; the answering span is the last non-insert one before it.
    auto it = std::ranges::upper_bound(spans_, x, {}, source_start_of);
    while (it != spans_.begin()) {
        --it;
        if (it->kind != SpanKind::insert) {
            break;
        }
    }
    if (it->kind == SpanKind::cut) {
        return std::nullopt;
    }
    const double slope = output_length_of(*it) / source_length_of(*it);
    return make_output(raw(it->output_start) + (x - raw(it->source_start)) * slope);
}

std::optional<OutputTime> TimeMap::to_output_end(SourceTime s) const noexcept {
    if (identity_) {
        return make_output(raw(s));
    }
    const double length = source_length_.value();
    assert(raw(s) > 0.0 && raw(s) <= length);
    const double x = std::clamp(raw(s), 0.0, length);
    // First span with source_start >= x; the answering span is the last non-insert one before it.
    auto it = std::ranges::lower_bound(spans_, x, {}, source_start_of);
    while (it != spans_.begin()) {
        --it;
        if (it->kind != SpanKind::insert) {
            break;
        }
    }
    if (it->kind == SpanKind::cut) {
        return std::nullopt;
    }
    const double slope = output_length_of(*it) / source_length_of(*it);
    return make_output(raw(it->output_start) + (x - raw(it->source_start)) * slope);
}

SourceLookup TimeMap::to_source(OutputTime tau) const noexcept {
    if (identity_) {
        return make_source(raw(tau));
    }
    const double length = output_length_.value();
    assert(raw(tau) >= 0.0 && raw(tau) <= length);
    const double y = std::clamp(raw(tau), 0.0, length);
    if (spans_.empty() || length == 0.0) {
        return make_source(0.0);
    }
    const auto answer = [&](const TimeSpan& span, double at) -> SourceLookup {
        if (span.kind == SpanKind::insert) {
            return GeneratedTime{.span = static_cast<std::size_t>(&span - spans_.data()),
                                 .offset = Seconds{at - raw(span.output_start)}};
        }
        const double inverse_slope = source_length_of(span) / output_length_of(span);
        return make_source(raw(span.source_start) + (at - raw(span.output_start)) * inverse_slope);
    };
    if (y == length) {
        const auto last =
            std::ranges::find_if(spans_ | std::views::reverse,
                                 [](const TimeSpan& sp) { return sp.kind != SpanKind::cut; });
        if (last != spans_.rend()) {
            return answer(*last, raw(last->output_end));
        }
    }
    // First span with output_start > y; the answering span is the last non-cut one before it.
    auto it = std::ranges::upper_bound(spans_, y, {}, output_start_of);
    while (it != spans_.begin()) {
        --it;
        if (it->kind != SpanKind::cut) {
            break;
        }
    }
    return answer(*it, y);
}

Ratio TimeMap::slope_at(OutputTime tau) const noexcept {
    if (identity_) {
        return Ratio{1.0};
    }
    const double length = output_length_.value();
    assert(raw(tau) >= 0.0 && raw(tau) <= length);
    const double y = std::clamp(raw(tau), 0.0, length);
    if (spans_.empty()) {
        return Ratio{0.0};
    }
    auto it = std::ranges::upper_bound(spans_, y, {}, output_start_of);
    while (it != spans_.begin()) {
        --it;
        if (it->kind != SpanKind::cut) {
            break;
        }
    }
    if (it == spans_.end() || it->kind != SpanKind::warp) {
        return Ratio{0.0};
    }
    return Ratio{output_length_of(*it) / source_length_of(*it)};
}

}  // namespace anno
