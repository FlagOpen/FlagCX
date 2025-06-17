#ifdef USE_ASCEND_ADAPTOR

#include "adaptor.h"
#include "alloc.h"
#include "comm.h"
#include "flagcx.h"
#include "hccl.h"
#include <acl/acl.h>
#include <map>
struct flagcxInnerComm {
    hcclComm_t base;
};

struct flagcxStream {
    ascendStream_t base;
};

struct flagcxEvent {
    ascendEvent_t base;
};

#define DEVCHECK(func)                                                         \
  {                                                                            \
    int ret = func;                                                            \
    if (ret != ascendSuccess)                                                    \
      return flagcxUnhandledDeviceError;                                       \
  }

#endif // USE_ASCEND_ADAPTOR
