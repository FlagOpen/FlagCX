#pragma once

#include "flagcx.h"
#include <c10/util/CallOnce.h>
#include <optional>
#include <string>

// Macro to throw on a non-successful Flagcx return value.
#define C10D_FLAGCX_CHECK(cmd, failureReason)                                  \
  do {                                                                         \
    flagcxResult_t result = cmd;                                               \
    if (result != flagcxSuccess) {                                             \
      std::string err = "FLAGCX error in: " + std::string(__FILE__) + ":" +    \
                        std::to_string(__LINE__) +                             \
                        ", " /*+ flagcxGetErrorWithVersion(result)*/ + "\n" +  \
                        getFlagcxErrorDetailStr(result, failureReason);        \
      TORCH_CHECK_WITH(DistBackendError, false, err);                          \
    }                                                                          \
  } while (0)

namespace c10d {

// RAII helper class to manage Flagcx group API.
// The destructor is allowed to throw since this helper class only
// manages group lifetimes.
struct flagcxGroupGuard final {
  flagcxGroupGuard(flagcxComm_t comm) {
    comm_ = comm;
    flagcxGroupStart(comm_);
  }
  ~flagcxGroupGuard() noexcept(false) { flagcxGroupEnd(comm_); }
  flagcxComm_t comm_ = nullptr;
};

std::string getFlagcxVersion();

std::string flagcxGetErrorWithVersion(flagcxResult_t error);

// Provides additional detail into Flagcx error codes based on when these are
// thrown in the Flagcx codebase.
std::string getFlagcxErrorDetailStr(
    flagcxResult_t error,
    std::optional<std::string> processGroupFailureReason = std::nullopt);

} // namespace c10d