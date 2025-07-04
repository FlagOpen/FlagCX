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
#elif USE_MPI_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&mpiAdaptor,
                                                      &ncclAdaptor};
#endif
struct flagcxDeviceAdaptor *deviceAdaptor = &cudaAdaptor;
#elif USE_ASCEND_ADAPTOR
#ifdef USE_BOOTSTRAP_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&bootstrapAdaptor,
                                                      &hcclAdaptor};
#elif USE_GLOO_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&glooAdaptor,
                                                      &hcclAdaptor};
#elif USE_MPI_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&mpiAdaptor,
                                                      &hcclAdaptor};
#endif
struct flagcxDeviceAdaptor *deviceAdaptor = &cannAdaptor;
#elif USE_ILUVATAR_COREX_ADAPTOR
#ifdef USE_BOOTSTRAP_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&bootstrapAdaptor,
                                                      &ixncclAdaptor};
#elif USE_GLOO_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&glooAdaptor,
                                                      &ixncclAdaptor};
#elif USE_MPI_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&mpiAdaptor,
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
#elif USE_MPI_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&mpiAdaptor,
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
#elif USE_MPI_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&mpiAdaptor,
                                                      &mcclAdaptor};
#endif
struct flagcxDeviceAdaptor *deviceAdaptor = &macaAdaptor;
#elif USE_KUNLUNXIN_ADAPTOR
#ifdef USE_BOOTSTRAP_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&bootstrapAdaptor,
                                                      &xcclAdaptor};
#elif USE_GLOO_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&glooAdaptor,
                                                      &xcclAdaptor};
#elif USE_MPI_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&mpiAdaptor,
                                                      &xcclAdaptor};
#endif
struct flagcxDeviceAdaptor *deviceAdaptor = &kunlunAdaptor;
#elif USE_DU_ADAPTOR
#ifdef USE_BOOTSTRAP_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&bootstrapAdaptor,
                                                      &duncclAdaptor};
#elif USE_GLOO_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&glooAdaptor,
                                                      &duncclAdaptor};
#elif USE_MPI_ADAPTOR
struct flagcxCCLAdaptor *cclAdaptors[NCCLADAPTORS] = {&mpiAdaptor,
                                                      &duncclAdaptor};
#endif
struct flagcxDeviceAdaptor *deviceAdaptor = &ducudaAdaptor;
#endif
