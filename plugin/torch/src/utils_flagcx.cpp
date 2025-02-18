#include "utils_flagcx.hpp"

namespace c10d {

std::string getFlagcxVersion() {
  static c10::once_flag flagcxGetVersionFlag;
  static std::string versionString;

  c10::call_once(flagcxGetVersionFlag, []() {
    int version = 0;
    flagcxResult_t status = flagcxGetVersion(&version);
    if (status != flagcxSuccess) {
      versionString = "Unknown Flagcx version";
    } else {
      versionString = std::to_string(version);
    }
  });
  return versionString;
}

std::string flagcxGetErrorWithVersion(flagcxResult_t error) {
  return std::string(flagcxGetErrorString(error)) + ", Flagcx version " +
         getFlagcxVersion();
}

// Provides additional detail into Flagcx error codes based on when these are
// thrown in the Flagcx codebase.
std::string getFlagcxErrorDetailStr(
    flagcxResult_t error,
    std::optional<std::string> processGroupFailureReason /* = std::nullopt */) {
  // Prioritize failure reason provided by PG Flagcx first, as it can abort
  // communicators when it encounters collective timeouts, etc.
  if (processGroupFailureReason != std::nullopt) {
    return *processGroupFailureReason;
  }
  std::string interpret;
  std::string err;
  auto ret = flagcxGetLastError(nullptr);
  if (ret) {
    err = "\nLast error:\n" + std::string(ret);
  } else {
    err = "\nLast error: Unknown Flagcx Error\n";
  }

  switch (error) {
    case flagcxUnhandledDeviceError:
      interpret = "flagcxUnhandledDeviceError: Call to Device function failed.";
      break;
    case flagcxSystemError:
      interpret = "flagcxSystemError: System call (e.g. socket, malloc) or "
                  "external library call failed or device error. ";
      break;
    case flagcxRemoteError:
      interpret = "flagcxRemoteError: A call failed possibly due to a network "
                  "error or a remote process exiting prematurely.";
      break;
    case flagcxInternalError:
      interpret = "flagcxInternalError: Internal check failed.";
      break;
    case flagcxInvalidArgument:
      interpret = "flagcxInvalidArgument: Invalid value for an argument.";
      break;
    case flagcxInvalidUsage:
      interpret = "flagcxInvalidUsage: This usually reflects invalid usage of "
                  "Flagcx library.";
      break;
    default:
      interpret = "Unknown Flagcx error!";
  }
  return interpret + err;
}
} // namespace c10d