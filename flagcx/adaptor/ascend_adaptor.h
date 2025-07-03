#ifdef USE_ASCEND_ADAPTOR
#include "adaptor.h"
#include "alloc.h"
#include "comm.h"
#include "flagcx.h"
#include "hccl/hccl.h"
#include "acl/acl.h"
#include <map>
struct flagcxInnerComm {
  HcclComm base;
};

struct flagcxStream {
  aclrtStream base;
};

struct flagcxEvent {
  aclrtEvent base;
};

#define DEVCHECK(func)                                                         \
  {                                                                            \
    int ret = func;                                                            \
    if (ret != ACL_SUCCESS)                                                    \
      return flagcxUnhandledDeviceError;                                       \
  }
#endif // USE_ASCEND_ADAPTOR
