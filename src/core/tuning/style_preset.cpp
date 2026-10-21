#include "core/tuning/style_preset.h"

namespace anno {

StyleDefaults style_defaults(StylePreset preset) noexcept {
    StyleDefaults d;
    switch (preset) {
    case StylePreset::pop:
        d.provisional = false;  // the defaults above; golden-checked
        break;
    case StylePreset::classical:
        d.tolerance_low = Cents{-10.0};
        d.tolerance_high = Cents{10.0};
        d.glissando_g = 0.16;
        break;
    case StylePreset::jazz_blues:
        d.tolerance_low = Cents{-15.0};
        d.tolerance_high = Cents{20.0};
        d.skip_consistent_off_grid = true;  // blue notes (EMR study)
        break;
    case StylePreset::gamaka:
        d.tolerance_low = Cents{-20.0};
        d.tolerance_high = Cents{20.0};
        d.skip_consistent_off_grid = true;
        d.ornaments = OrnamentHandling::leave_alone;
        d.detect_gamaka = true;
        d.quantise_turning_points_only = true;  // ISMIR 2017
        break;
    }
    return d;
}

}  // namespace anno
