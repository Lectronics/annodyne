#include "core/render/render_key.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cstdio>

namespace anno {

namespace {

constexpr std::uint32_t k_key_format_version =
    3;  // 3: the stretch settings; 2: the whole TimeMap is part of the key

constexpr std::uint64_t k_fnv_basis_a = 0xcbf29ce484222325ULL;
constexpr std::uint64_t k_fnv_prime_a = 0x100000001b3ULL;
constexpr std::uint64_t k_basis_b = 0x9e3779b97f4a7c15ULL;
constexpr std::uint64_t k_prime_b = 0xff51afd7ed558ccdULL;

/// Appends typed values to a byte string, or to two hash streams.
class Writer {
public:
    void u64(std::uint64_t v) {
        for (unsigned i = 0; i < 8U; ++i) {
            byte(static_cast<unsigned char>((v >> (8U * i)) & 0xFFU));
        }
    }
    void i64(std::int64_t v) { u64(static_cast<std::uint64_t>(v)); }
    void f64(double v) { u64(std::bit_cast<std::uint64_t>(v)); }
    void f32(float v) { u64(std::bit_cast<std::uint32_t>(v)); }
    void boolean(bool v) { byte(v ? 1 : 0); }
    void text(std::string_view s) {
        u64(s.size());
        for (const char c : s) {
            byte(static_cast<unsigned char>(c));
        }
    }
    void bytes_of(const std::string& s) { text(s); }

    [[nodiscard]] const std::string& data() const { return data_; }
    [[nodiscard]] std::uint64_t hash_a() const { return a_; }
    [[nodiscard]] std::uint64_t hash_b() const { return b_; }

private:
    void byte(unsigned char c) {
        data_.push_back(static_cast<char>(c));
        a_ = (a_ ^ c) * k_fnv_prime_a;
        b_ = (b_ + c + 1U) * k_prime_b;
        b_ ^= b_ >> 29U;
    }

    std::string data_;
    std::uint64_t a_ = k_fnv_basis_a;
    std::uint64_t b_ = k_basis_b;
};

void put(Writer& w, Seconds v) {
    w.f64(v.value());
}
void put(Writer& w, Ratio v) {
    w.f64(v.value());
}
void put(Writer& w, Cents v) {
    w.f64(v.value());
}
void put(Writer& w, Decibels v) {
    w.f64(v.value());
}
void put(Writer& w, SourceTime v) {
    w.f64((v - SourceTime{}).value());
}
void put(Writer& w, OutputTime v) {
    w.f64((v - OutputTime{}).value());
}

template <class T>
void put_all(Writer& w, const std::vector<T>& v) {
    w.u64(v.size());
    for (const T& x : v) {
        put(w, x);
    }
}
void put(Writer& w, float v) {
    w.f32(v);
}
void put(Writer& w, Hz v) {
    w.f64(v.value());
}
void put(Writer& w, FrameClass v) {
    w.u64(static_cast<std::uint64_t>(v));
}

void put(Writer& w, const FrameGrid& g) {
    put(w, g.hop);
    put(w, g.start);
    w.i64(g.n_frames);
}
void put(Writer& w, const FrameMatrix& m) {
    w.i64(m.rows);
    w.i64(m.cols);
    put_all(w, m.data);
}
void put(Writer& w, const TimePair& p) {
    put(w, p.source);
    put(w, p.output);
}

void put(Writer& w, const TimeMap& map) {
    w.boolean(map.is_identity());
    put(w, map.source_length());
    put(w, map.output_length());
    if (map.is_identity()) {
        return;
    }
    w.u64(map.spans().size());
    for (const TimeSpan& s : map.spans()) {
        w.u64(static_cast<std::uint64_t>(s.kind));
        put(w, s.source_start);
        put(w, s.source_end);
        put(w, s.output_start);
        put(w, s.output_end);
    }
}

void put(Writer& w, const EffectiveNote& e) {
    w.text(e.note_id.value);
    put(w, e.center);
    put(w, e.mod_scale);
    put(w, e.drift_scale);
    put(w, e.transition_in_scale);
    put(w, e.transition_out_scale);
    put(w, e.formant_offset);
    put(w, e.gain);
    put(w, e.within_note_flatten);
    put(w, e.fade_in);
    put(w, e.fade_out);
    w.u64(static_cast<std::uint64_t>(e.fade_shape));
    put(w, e.noise_gain.voiced);
    put(w, e.noise_gain.sibilant);
    put(w, e.noise_gain.breath);
    put(w, e.noise_gain.plosive);
    put(w, e.onset_shift);
    put(w, e.length_scale);
    put_all(w, e.handles);
    put(w, e.attack_time_scale);
    w.boolean(e.is_edited);
}

void put(Writer& w, const Note& n) {
    w.text(n.id.value);
    w.text(n.track_id.value);
    w.i64(n.voice);
    put(w, n.source_start);
    put(w, n.attack_end);
    put(w, n.release_start);
    put(w, n.source_end);
    put(w, n.pitch_target.target);
    put(w, n.pitch_center);
    put(w, n.grid);
    put_all(w, n.pitch_drift);
    put_all(w, n.pitch_mod);
    put_all(w, n.amp);
    w.f64(n.loudness.value());
    put(w, n.harmonics.grid);
    put(w, n.harmonics.freq_hz);
    put(w, n.harmonics.amp_linear);
    put(w, n.harmonics.phase_rad);
    put(w, n.noise.grid);
    put(w, n.noise.band_energy_linear);
    put_all(w, n.noise.frame_class);
    put(w, n.envelope.grid);
    put(w, n.envelope.log_mag_db);
    put_all(w, n.envelope.freqs);
    w.u64(static_cast<std::uint64_t>(n.separation_in));
    w.f64(n.confidence_prob);
    w.boolean(n.is_active);
}

void put(Writer& w, const StretchParams* p) {
    w.u64(p != nullptr ? 1U : 0U);
    if (p == nullptr) {
        return;
    }
    w.f64(p->class_edge_fade.value());
    w.f64(p->noise_grain.value());
    w.f64(p->noise_jitter_hop_fraction);
    w.u64(p->noise_seed);
    w.f64(p->min_slope.value());
    w.f64(p->max_slope.value());
    w.u64(p->voiced_classes.bits());
}

}  // namespace

std::string note_fingerprint(const Note& note) {
    Writer w;
    put(w, note);
    Writer out;
    out.u64(w.hash_a());
    out.u64(w.hash_b());
    return out.data();
}

std::string make_render_key(const RenderKeyInputs& in) {
    Writer w;
    w.u64(k_key_format_version);
    w.text(in.source_id->value);
    w.u64(in.analysis_version);
    w.text(in.engine->name());
    w.u64(in.engine->version());
    w.text(in.engine->settings_key());
    w.i64(in.margin_samples);
    w.u64(static_cast<std::uint64_t>(in.shape));
    w.i64(in.region_begin);
    w.i64(in.region_end);
    w.f64(in.sample_rate_hz);
    put(w, *in.time_map);
    put(w, in.stretch);

    const auto name_of = [&](int index) {
        return index == k_pristine ? std::string{}
                                   : in.edited[static_cast<std::size_t>(index)].note->id.value;
    };
    std::vector<const Ramp*> meeting;
    for (const Ramp& r : in.ramps) {
        if (r.begin <= in.region_end && r.end >= in.region_begin) {
            meeting.push_back(&r);
        }
    }
    w.u64(meeting.size());
    for (const Ramp* r : meeting) {
        w.i64(r->begin);
        w.i64(r->end);
        w.text(name_of(r->from));
        w.text(name_of(r->to));
    }

    std::vector<const EditedNote*> zone_notes;
    for (const EditedNote& e : in.edited) {
        const std::int64_t zone_begin = e.span.begin - in.margin_samples;
        const std::int64_t zone_end = e.span.end + in.margin_samples;
        if (zone_begin < in.region_end && zone_end > in.region_begin) {
            zone_notes.push_back(&e);
        }
    }
    w.u64(zone_notes.size());
    for (const EditedNote* e : zone_notes) {
        w.text(e->note->id.value);
        w.i64(e->span.begin);
        w.i64(e->span.end);
        put(w, e->eff);
        w.text(note_fingerprint(*e->note));
    }
    return w.data();
}

std::string short_id(std::string_view key) {
    Writer w;
    w.text(key);
    std::array<char, 17> text{};
    std::snprintf(text.data(), text.size(), "%016llx", static_cast<unsigned long long>(w.hash_a()));
    return std::string{text.data()};
}

}  // namespace anno
