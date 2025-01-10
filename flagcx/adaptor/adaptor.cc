#include "adaptor.h"

#ifdef USE_NVIDIA_ADAPTOR
#ifdef USE_GLOO_ADAPTOR
  struct flagcxCCLAdaptor* cclAdaptors[NCCLADAPTORS] = {&glooAdaptor, &ncclAdaptor};
#else
  struct flagcxCCLAdaptor* cclAdaptors[NCCLADAPTORS] = {NULL, &ncclAdaptor};
#endif
  struct flagcxDeviceAdaptor* deviceAdaptor = &cudaAdaptor;
#elif USE_ILUVATAR_COREX_ADAPTOR
#ifdef USE_GLOO_ADAPTOR
  struct flagcxCCLAdaptor* cclAdaptors[NCCLADAPTORS] = {&glooAdaptor, &ixncclAdaptor};
#else
  struct flagcxCCLAdaptor* cclAdaptors[NCCLADAPTORS] = {NULL, &ixncclAdaptor};
#endif
  struct flagcxDeviceAdaptor* deviceAdaptor = &ixcudaAdaptor;
#elif USE_CAMBRICON_ADAPTOR
#ifdef USE_GLOO_ADAPTOR
  struct flagcxCCLAdaptor* cclAdaptors[NCCLADAPTORS] = {&glooAdaptor, &cnclAdaptor};
#else
  struct flagcxCCLAdaptor* cclAdaptors[NCCLADAPTORS] = {NULL, &cnclAdaptor};
#endif
  struct flagcxDeviceAdaptor* deviceAdaptor = &mluAdaptor;
#endif