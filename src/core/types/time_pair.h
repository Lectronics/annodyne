#pragma once

#include "core/types/units.h"

namespace anno {

/// A source instant and where it lands in the output. One anchor of a TimeMap, and one time handle
/// of a `time_handle` edit.
struct TimePair {
    SourceTime source;
    OutputTime output;

    friend bool operator==(const TimePair&, const TimePair&) = default;
};

}  // namespace anno
