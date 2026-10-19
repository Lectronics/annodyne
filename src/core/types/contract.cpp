#include "core/types/contract.h"

#include <cstdio>
#include <cstdlib>

#ifdef _WIN32
#include <crtdbg.h>
#endif

namespace anno::detail {

void contract_violation(const char* expression, std::source_location where) noexcept {
    std::fprintf(stderr, "anno: contract violation: %s\n  at %s:%u in %s\n", expression,
                 where.file_name(), static_cast<unsigned>(where.line()), where.function_name());
    std::fflush(stderr);
#ifdef _WIN32
    // The Debug CRT would otherwise show a modal "abort() has been called" dialog and hang CI.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_DEBUG);
#endif
    std::abort();
}

}  // namespace anno::detail
