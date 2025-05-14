/*************************************************************************
 * Copyright (c) 2025 by MetaX Integrated Circuits (Shanghai) Co., Ltd. All
 *Rights Reserved. Copyright (c) 2025 by DU. All Rights Reserved.
 ************************************************************************/

#ifndef FLAGCX_ADAPTOR_H_
#define FLAGCX_ADAPTOR_H_

#include "bootstrap.h"
#include "flagcx.h"
#include "global_comm.h"
#include "topo.h"

#ifdef __cplusplus
extern "C" {
#endif

#define NCCLADAPTORS 2
#define flagcxCCLAdaptorHost 0
#define flagcxCCLAdaptorDevice 1

extern struct flagcxCCLAdaptor bootstrapAdaptor;
extern struct flagcxCCLAdaptor glooAdaptor;
extern struct flagcxCCLAdaptor ncclAdaptor;
extern struct flagcxCCLAdaptor ixncclAdaptor;
extern struct flagcxCCLAdaptor cnclAdaptor;
extern struct flagcxCCLAdaptor mcclAdaptor;
extern struct flagcxCCLAdaptor duncclAdaptor;
extern struct flagcxCCLAdaptor *cclAdaptors[];

extern struct flagcxDeviceAdaptor cudaAdaptor;
extern struct flagcxDeviceAdaptor ixcudaAdaptor;
extern struct flagcxDeviceAdaptor mluAdaptor;
extern struct flagcxDeviceAdaptor macaAdaptor;
extern struct flagcxDeviceAdaptor ducudaAdaptor;
extern struct flagcxDeviceAdaptor *deviceAdaptor;

inline bool flagcxCCLAdaptorNeedSendrecv(size_t value) { return value != 0; }

struct flagcxCCLAdaptor {
  const char name[32];
  // Basic functions
  flagcxResult_t (*getVersion)(int *version);
  flagcxResult_t (*getUniqueId)(flagcxUniqueId_t *uniqueId);
  const char *(*getErrorString)(flagcxResult_t result);
  const char *(*getLastError)(flagcxInnerComm_t comm);

  // Communicator functions
  flagcxResult_t (*commInitRank)(flagcxInnerComm_t *comm, int nranks,
                                 flagcxUniqueId *commId, int rank,
                                 bootstrapState *bootstrap);
  flagcxResult_t (*commFinalize)(flagcxInnerComm_t comm);
  flagcxResult_t (*commDestroy)(flagcxInnerComm_t comm);
  flagcxResult_t (*commAbort)(flagcxInnerComm_t comm);
  flagcxResult_t (*commResume)(flagcxInnerComm_t comm);
  flagcxResult_t (*commSuspend)(flagcxInnerComm_t comm);
  flagcxResult_t (*commCount)(const flagcxInnerComm_t comm, int *count);
  flagcxResult_t (*commGetDeviceNumber)(const flagcxInnerComm_t comm,
                                        int *device);
  flagcxResult_t (*commUserRank)(const flagcxInnerComm_t comm, int *rank);
  flagcxResult_t (*commGetAsyncError)(flagcxInnerComm_t comm,
                                      flagcxResult_t asyncError);

  // Communication functions
  flagcxResult_t (*reduce)(const void *sendbuff, void *recvbuff, size_t count,
                           flagcxDataType_t datatype, flagcxRedOp_t op,
                           int root, flagcxInnerComm_t comm,
                           flagcxStream_t stream);
  flagcxResult_t (*gather)(const void *sendbuff, void *recvbuff, size_t count,
                           flagcxDataType_t datatype, int root,
                           flagcxInnerComm_t comm, flagcxStream_t stream);
  flagcxResult_t (*scatter)(const void *sendbuff, void *recvbuff, size_t count,
                            flagcxDataType_t datatype, int root,
                            flagcxInnerComm_t comm, flagcxStream_t stream);
  flagcxResult_t (*broadcast)(const void *sendbuff, void *recvbuff,
                              size_t count, flagcxDataType_t datatype, int root,
                              flagcxInnerComm_t comm, flagcxStream_t stream);
  flagcxResult_t (*allReduce)(const void *sendbuff, void *recvbuff,
                              size_t count, flagcxDataType_t datatype,
                              flagcxRedOp_t op, flagcxInnerComm_t comm,
                              flagcxStream_t stream);
  flagcxResult_t (*reduceScatter)(const void *sendbuff, void *recvbuff,
                                  size_t recvcount, flagcxDataType_t datatype,
                                  flagcxRedOp_t op, flagcxInnerComm_t comm,
                                  flagcxStream_t stream);
  flagcxResult_t (*allGather)(const void *sendbuff, void *recvbuff,
                              size_t sendcount, flagcxDataType_t datatype,
                              flagcxInnerComm_t comm, flagcxStream_t stream);
  flagcxResult_t (*alltoAll)(const void *sendbuff, void *recvbuff, size_t count,
                             flagcxDataType_t datatype, flagcxInnerComm_t comm,
                             flagcxStream_t stream);
  flagcxResult_t (*alltoAllv)(const void *sendbuff, size_t *sendcounts,
                              size_t *sdispls, void *recvbuff,
                              size_t *recvcounts, size_t *rdispls,
                              flagcxDataType_t datatype, flagcxInnerComm_t comm,
                              flagcxStream_t stream);
  flagcxResult_t (*send)(const void *sendbuff, size_t count,
                         flagcxDataType_t datatype, int peer,
                         flagcxInnerComm_t comm, flagcxStream_t stream);
  flagcxResult_t (*recv)(void *recvbuff, size_t count,
                         flagcxDataType_t datatype, int peer,
                         flagcxInnerComm_t comm, flagcxStream_t stream);

  // Group semantics
  flagcxResult_t (*groupStart)();
  flagcxResult_t (*groupEnd)();
};

const int MAX_VENDOR_LEN = 128;
typedef struct {
  char internal[MAX_VENDOR_LEN];
} flagcxVendor;

struct flagcxDeviceAdaptor {
  char name[32];
  // Basic functions
  flagcxResult_t (*deviceSynchronize)();
  flagcxResult_t (*deviceMemcpy)(void *dst, void *src, size_t size,
                                 flagcxMemcpyType_t type, flagcxStream_t stream,
                                 void *args);
  flagcxResult_t (*deviceMemset)(void *ptr, int value, size_t size,
                                 flagcxMemType_t type, flagcxStream_t stream);
  flagcxResult_t (*deviceMalloc)(void **ptr, size_t size, flagcxMemType_t type,
                                 flagcxStream_t stream);
  flagcxResult_t (*deviceFree)(void *ptr, flagcxMemType_t type,
                               flagcxStream_t stream);
  flagcxResult_t (*setDevice)(int dev);
  flagcxResult_t (*getDevice)(int *dev);
  flagcxResult_t (*getDeviceCount)(int *count);
  flagcxResult_t (*getVendor)(char *vendor);

  // GDR functions
  flagcxResult_t (*memHandleInit)(int dev_id, void **memHandle);
  flagcxResult_t (*memHandleDestroy)(int dev, void *memHandle);
  flagcxResult_t (*gdrMemAlloc)(void **ptr, size_t size, void *memHandle);
  flagcxResult_t (*gdrMemFree)(void *ptr, void *memHandle);
  flagcxResult_t (*hostShareMemAlloc)(void **ptr, size_t size, void *memHandle);
  flagcxResult_t (*hostShareMemFree)(void *ptr, void *memHandle);

  // Stream functions
  flagcxResult_t (*streamCreate)(flagcxStream_t *stream);
  flagcxResult_t (*streamDestroy)(flagcxStream_t stream);
  flagcxResult_t (*streamCopy)(flagcxStream_t *newStream, void *oldStream);
  flagcxResult_t (*streamFree)(flagcxStream_t stream);
  flagcxResult_t (*streamSynchronize)(flagcxStream_t stream);
  flagcxResult_t (*streamQuery)(flagcxStream_t stream);
  flagcxResult_t (*streamWaitEvent)(flagcxStream_t stream, flagcxEvent_t event);

  // Event functions
  flagcxResult_t (*eventCreate)(flagcxEvent_t *event);
  flagcxResult_t (*eventDestroy)(flagcxEvent_t event);
  flagcxResult_t (*eventRecord)(flagcxEvent_t event, flagcxStream_t stream);
  flagcxResult_t (*eventSynchronize)(flagcxEvent_t event);
  flagcxResult_t (*eventQuery)(flagcxEvent_t event);

  // Kernel launch
  // TODO: verify if we do need these funcs, if so, figure out a way to
  // eliminate overly fine-grained arguments such as block_xxx, grid_xxx, etc.
  // And define more generic kernel launch APIs
  flagcxResult_t (*launchKernel)(void *func, unsigned int block_x,
                                 unsigned int block_y, unsigned int block_z,
                                 unsigned int grid_x, unsigned int grid_y,
                                 unsigned int grid_z, void **args,
                                 size_t share_mem, void *stream,
                                 void *memHandle);
  flagcxResult_t (*copyArgsInit)(void **args);
  flagcxResult_t (*copyArgsFree)(void *args);

  // Others
  // TODO: this one shall be moved into Flagcx Core Topology APIs
  // Here we only define several low-level APIs required by topology detection
  flagcxResult_t (*getDeviceProperties)(struct flagcxDevProps *props, int dev);
  flagcxResult_t (*getDevicePciBusId)(char *pciBusId, int len, int dev);
  flagcxResult_t (*getDeviceByPciBusId)(int *dev, const char *pciBusId);

  // HostFunc launch
  flagcxResult_t (*launchHostFunc)(flagcxStream_t stream, void (*fn)(void *),
                                   void *args);
};

#ifdef __cplusplus
} // end extern "C"
#endif

#endif // end include guard
