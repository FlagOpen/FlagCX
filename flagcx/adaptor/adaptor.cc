/*************************************************************************
 * Copyright (c) 2025 by MetaX Integrated Circuits (Shanghai) Co., Ltd. All
 *Rights Reserved. Copyright (c) 2025 by DU. All Rights Reserved.
 ************************************************************************/

#include "adaptor.h"

#ifdef USE_NVIDIA_ADAPTOR
#ifdef USE_BOOTSTRAP_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&bootstrapAdaptor,
                                                      &ncclAdaptor};
#elif USE_GLOO_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&glooAdaptor,
                                                      &ncclAdaptor};
#endif
struct flagcxDeviceAdaptor *deviceAdaptor = &cudaAdaptor;
#elif USE_ILUVATAR_COREX_ADAPTOR
#ifdef USE_BOOTSTRAP_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&bootstrapAdaptor,
                                                      &ixncclAdaptor};
#elif USE_GLOO_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&glooAdaptor,
                                                      &ixncclAdaptor};
#endif
struct flagcxDeviceAdaptor *deviceAdaptor = &ixcudaAdaptor;
#elif USE_CAMBRICON_ADAPTOR
#ifdef USE_BOOTSTRAP_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&bootstrapAdaptor,
                                                      &cnclAdaptor};
#elif USE_GLOO_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&glooAdaptor,
                                                      &cnclAdaptor};
#endif
struct flagcxDeviceAdaptor *deviceAdaptor = &mluAdaptor;
#elif USE_METAX_ADAPTOR
#ifdef USE_BOOTSTRAP_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&bootstrapAdaptor,
                                                      &mcclAdaptor};
#elif USE_GLOO_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&glooAdaptor,
                                                      &mcclAdaptor};
#endif
struct flagcxDeviceAdaptor *deviceAdaptor = &macaAdaptor;
#elif USE_DU_ADAPTOR
#ifdef USE_BOOTSTRAP_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&bootstrapAdaptor,
                                                      &duncclAdaptor};
#elif USE_GLOO_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&glooAdaptor,
                                                      &duncclAdaptor};
#endif
struct flagcxDeviceAdaptor *deviceAdaptor = &ducudaAdaptor;
#endif