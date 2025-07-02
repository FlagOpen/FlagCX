#ifdef USE_ASCEND_ADAPTOR
/*
https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/82RC1alpha002/API/hcclapiref/hcclcpp_07_0001.html
https://www.hiascend.com/document/detail/zh/CANNCommunityEdition/82RC1alpha002/appdevg/acldevg/aclcppdevg_000005.html
*/
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

#define HCCLCHECK(func)                                                         \
  {                                                                            \
    int ret = func;                                                            \
    if (ret != HCCL_SUCCESS)                                                    \
      return flagcxUnhandledDeviceError;                                       \
  }

#endif // USE_ASCEND_ADAPTOR