#ifdef USE_AMD_ADAPTOR

#include "adaptor.h"
#include "alloc.h"
#include "comm.h"
#include "flagcx.h"
#include "rccl.h"
#include <hip/hip_runtime.h>
#include <map>
struct flagcxInnerComm {
  ncclComm_t base;
};

struct flagcxStream {
  hipStream_t base;
};

struct flagcxEvent {
  hipEvent_t base;
};

#define DEVCHECK(func)                                                         \
  {                                                                            \
    int ret = func;                                                            \
    if (ret != hipSuccess)                                                     \
      return flagcxUnhandledDeviceError;                                       \
  }

#endif // USE_AMD_ADAPTOR