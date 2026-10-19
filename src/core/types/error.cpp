#include "core/types/error.h"

#include <utility>

namespace anno {

std::string_view to_string(ErrorCode code) noexcept {
    switch (code) {
    case ErrorCode::invalid_argument: return "invalid_argument";
    case ErrorCode::out_of_range: return "out_of_range";
    case ErrorCode::io_error: return "io_error";
    case ErrorCode::unsupported_format: return "unsupported_format";
    case ErrorCode::corrupt_data: return "corrupt_data";
    case ErrorCode::not_found: return "not_found";
    case ErrorCode::cancelled: return "cancelled";
    case ErrorCode::internal: return "internal";
    }
    return "unknown";
}

Unexpected fail(ErrorCode code, std::string message) {
    return Unexpected{Error{.code = code, .message = std::move(message)}};
}

}  // namespace anno
