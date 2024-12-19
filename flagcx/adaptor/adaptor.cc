#include "adaptor.h"

#ifdef USE_NVIDIA_ADAPTOR
  struct flagcxCCLAdaptor* cclAdaptor = &ncclAdaptor;
  struct flagcxDeviceAdaptor* deviceAdaptor = &cudaAdaptor;
#elif USE_ILUVATAR_COREX_ADAPTOR
  struct flagcxCCLAdaptor* cclAdaptor = &ixncclAdaptor;
  struct flagcxDeviceAdaptor* deviceAdaptor = &ixcudaAdaptor;
#elif USE_CAMBRICON_ADAPTOR
  struct flagcxCCLAdaptor* cclAdaptor = &cnclAdaptor;
  struct flagcxDeviceAdaptor* deviceAdaptor = &mluAdaptor;
#endif
