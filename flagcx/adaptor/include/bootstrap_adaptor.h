#ifdef USE_BOOTSTRAP_ADAPTOR

#include "adaptor.h"
#include "alloc.h"
#include "check.h"
#include "comm.h"
#include "flagcx.h"
#include "utils.h"

struct flagcxInnerComm {
  bootstrapState *base;
};

#endif // USE_BOOTSTRAP_ADAPTOR