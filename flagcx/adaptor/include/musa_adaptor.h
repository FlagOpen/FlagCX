#ifdef USE_MUSA_ADAPTOR

#include "adaptor.h"
#include "alloc.h"
#include "comm.h"
#include "flagcx.h"
#include "mccl.h"
#include <map>
#include <musa.h>
#include <musa_runtime.h>
struct flagcxInnerComm {
  mcclComm_t base;
};

struct flagcxStream {
  musaStream_t base;
};

struct flagcxEvent {
  musaEvent_t base;
};

#define DEVCHECK(func)                                                         \
  {                                                                            \
    int ret = func;                                                            \
    if (ret != musaSuccess)                                                    \
      return flagcxUnhandledDeviceError;                                       \
  }

#endif // USE_MUSA_ADAPTOR