#pragma once

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

// Reader for the Vocadito corpus in its native layout (tests/golden/vocals/README.md): CSV files
// without header, LF or CRLF line ends, numbers in the C locale. Test support only.

namespace anno::test {

struct RefNote {
    double onset_s = 0.0;
    double pitch_hz = 0.0;
    double duration_s = 0.0;
};

struct VocaditoTrack {
    int id = 0;
    std::filesystem::path wav;
    std::vector<double> f0_hz;  // 0 = unvoiced; frame k is at k * f0_hop_s
    double f0_hop_s = 0.0;
    std::vector<RefNote> notes_a1;
    std::vector<RefNote> notes_a2;
};

namespace detail {

/// Rows of a headerless CSV as doubles; nullopt if the file cannot be read or a field is not a
/// number.
inline std::optional<std::vector<std::vector<double>>> read_csv(const std::filesystem::path& p) {
    std::ifstream in{p};
    if (!in) {
        return std::nullopt;
    }
    std::vector<std::vector<double>> rows;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) {
            line.pop_back();
        }
        if (line.empty()) {
            continue;
        }
        std::vector<double> row;
        std::istringstream fields{line};
        std::string field;
        while (std::getline(fields, field, ',')) {
            char* end = nullptr;
            const double v = std::strtod(field.c_str(), &end);  // C locale unless changed
            if (end == field.c_str()) {
                return std::nullopt;
            }
            row.push_back(v);
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

inline std::optional<std::vector<RefNote>> read_notes(const std::filesystem::path& p) {
    const auto rows = read_csv(p);
    if (!rows) {
        return std::nullopt;
    }
    std::vector<RefNote> out;
    for (const auto& r : *rows) {
        if (r.size() < 3) {
            return std::nullopt;
        }
        out.push_back({.onset_s = r[0], .pitch_hz = r[1], .duration_s = r[2]});
    }
    return out;
}

}  // namespace detail

/// Loads track `id` from `root` (the directory holding Audio/ and Annotations/). nullopt if any
/// file is missing or malformed.
inline std::optional<VocaditoTrack> load_vocadito(const std::filesystem::path& root, int id) {
    namespace fs = std::filesystem;
    const std::string stem = "vocadito_" + std::to_string(id);
    VocaditoTrack t;
    t.id = id;
    t.wav = root / "Audio" / (stem + ".wav");
    const auto f0 = detail::read_csv(root / "Annotations" / "F0" / (stem + "_f0.csv"));
    const auto a1 = detail::read_notes(root / "Annotations" / "Notes" / (stem + "_notesA1.csv"));
    const auto a2 = detail::read_notes(root / "Annotations" / "Notes" / (stem + "_notesA2.csv"));
    if (!fs::exists(t.wav) || !f0 || !a1 || !a2 || f0->size() < 2) {
        return std::nullopt;
    }
    for (const auto& row : *f0) {
        if (row.size() < 2) {
            return std::nullopt;
        }
        t.f0_hz.push_back(row[1]);
    }
    t.f0_hop_s = (*f0)[1][0] - (*f0)[0][0];
    t.notes_a1 = *a1;
    t.notes_a2 = *a2;
    return t;
}

/// Reference f0 as absolute cents (A4 = 6900) on a grid of `hop_s` with `n_frames` frames, NaN
/// where unvoiced; each frame takes the nearest annotation sample.
inline std::vector<double> reference_cents(const VocaditoTrack& t, double hop_s,
                                           std::size_t n_frames) {
    std::vector<double> out(n_frames, std::nan(""));
    for (std::size_t i = 0; i < n_frames; ++i) {
        const auto k =
            static_cast<std::size_t>(std::llround(static_cast<double>(i) * hop_s / t.f0_hop_s));
        if (k < t.f0_hz.size() && t.f0_hz[k] > 0.0) {
            out[i] = 6900.0 + 1200.0 * std::log2(t.f0_hz[k] / 440.0);
        }
    }
    return out;
}

/// Pitch of an annotated note in absolute cents (A4 = 6900).
inline double note_cents(const RefNote& n) {
    return 6900.0 + 1200.0 * std::log2(n.pitch_hz / 440.0);
}

}  // namespace anno::test
