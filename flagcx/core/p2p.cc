#include "p2p.h"
#include "adaptor.h"
#include "info.h"
#include <algorithm>

flagcxResult_t flagcxP2pProxySend(struct flagcxP2pResources* resources, void *data,
                                  size_t size, struct flagcxProxyArgs* args) {
  if (!args->semaphore->pollStart()) return flagcxSuccess;
  if (args->copied < args->chunkSteps && 
      args->copied - args->transmitted < MAXSTEPS) {
    int step = args->copied & args->sendStepMask;
    volatile uint64_t* recvTail = &resources->proxyInfo.shm->recvMem.tail;
    
    if (*recvTail > args->copied) {
      args->subs[step].stepSize = std::min(args->chunkSize, size - args->totalCopySize);
      args->subs[step].stepBuff = resources->proxyInfo.recvFifo + (args->chunkSize * step);
      
      FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
          args->subs[step].stepBuff, 
          (char *)data + args->totalCopySize,
          args->subs[step].stepSize, 
          flagcxMemcpyDeviceToDevice, 
          resources->proxyInfo.stream, 
          NULL));
      FLAGCXCHECK(deviceAdaptor->eventRecord(
          resources->proxyInfo.events[step], 
          resources->proxyInfo.stream));
      
      args->totalCopySize += args->subs[step].stepSize;
      args->copied++;
    }
  }
  
  if (args->transmitted < args->copied) {
    int step = args->transmitted & args->sendStepMask;
    flagcxResult_t res = deviceAdaptor->eventQuery(resources->proxyInfo.events[step]);
    
    if (res == flagcxSuccess) {
      args->transmitted++;
      volatile uint64_t* sendHead = &resources->proxyInfo.shm->sendMem.head;
      *sendHead = args->transmitted;
    }
  }
  
  if (args->transmitted >= args->chunkSteps) {
    if (args->done != 1) {
      args->semaphore->signalCounter(1);
      if (deviceAsyncLoad && deviceAsyncStore) {
        if (args->deviceFuncRelaxedOrdering == 1) {
          FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
              args->dlArgs, (void *)&args->hlArgs, sizeof(bool),
              flagcxMemcpyHostToDevice, resources->proxyInfo.stream, NULL));
        }
      }
      args->done = 1;
    }
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxP2pProxyRecv(struct flagcxP2pResources* resources, void *data,
                                  size_t size, struct flagcxProxyArgs* args) {
  if (!args->semaphore->pollStart()) return flagcxSuccess;
  
  // Step 1: Copy data from recvFifo (peer's send buffer in our GPU memory) to user buffer
  if (args->copied < args->chunkSteps && 
      args->copied - args->transmitted < MAXSTEPS) {
    int step = args->copied & args->sendStepMask;
    volatile uint64_t* sendHead = &resources->proxyInfo.shm->sendMem.head;
    
    // Check if sender has sent enough data (flow control)
    if (*sendHead > args->copied) {
      args->subs[step].stepSize = std::min(args->chunkSize, size - args->totalCopySize);
      args->subs[step].stepBuff = resources->proxyInfo.recvFifo + (args->chunkSize * step);
      
      FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
          (char *)data + args->totalCopySize,
          args->subs[step].stepBuff,
          args->subs[step].stepSize,
          flagcxMemcpyDeviceToDevice,
          resources->proxyInfo.stream,
          NULL));
      FLAGCXCHECK(deviceAdaptor->eventRecord(
          resources->proxyInfo.events[step],
          resources->proxyInfo.stream));
      
      args->totalCopySize += args->subs[step].stepSize;
      args->copied++;
    }
  }
  
  // Step 2: Check if copy events have completed and notify sender
  if (args->transmitted < args->copied) {
    int step = args->transmitted & args->sendStepMask;
    flagcxResult_t res = deviceAdaptor->eventQuery(resources->proxyInfo.events[step]);
    
    if (res == flagcxSuccess) {
      args->transmitted++;
      // Update recvMem.tail to notify sender we consumed data
      // Add MAXSTEPS for flow control window
      volatile uint64_t* recvTail = &resources->proxyInfo.shm->recvMem.tail;
      *recvTail = args->transmitted + MAXSTEPS;
    }
  }
  
  // Step 3: Mark as done when all chunks received (same as NET recv logic)
  if (args->transmitted >= args->chunkSteps) {
    if (args->done != 1) {
      args->semaphore->signalCounter(1);
      if (deviceAsyncLoad && deviceAsyncStore) {
        if (args->deviceFuncRelaxedOrdering == 1) {
          FLAGCXCHECK(deviceAdaptor->deviceMemcpy(
              args->dlArgs, (void *)&args->hlArgs, sizeof(bool),
              flagcxMemcpyHostToDevice, resources->proxyInfo.stream, NULL));
        }
      }
      args->done = 1;
    }
  }

  return flagcxSuccess;
}

flagcxResult_t flagcxP2pSendProxySetup(struct flagcxProxyConnection* connection,
                                        struct flagcxProxyState* proxyState,
                                        void* reqBuff,  int reqSize,
                                        void* respBuff, int respSize,
                                        int* done) {
  INFO(FLAGCX_INIT, "flagcxP2pSendProxySetup: reqSize=%d respSize=%d expectedRespSize=%zu",
       reqSize, respSize, sizeof(struct flagcxP2pShmProxyInfo));
  
  if (respSize != sizeof(struct flagcxP2pShmProxyInfo)) return flagcxInternalError;
  
  // Use the resources that was already allocated by transport.cc
  struct flagcxP2pResources* resources = (struct flagcxP2pResources*)connection->transportResources;
  if (resources == NULL) {
    WARN("flagcxP2pSendProxySetup: transportResources is NULL");
    return flagcxInternalError;
  }
  
  // Allocate shared memory and store in resources->proxyInfo
  size_t shmSize = sizeof(struct flagcxP2pShm);
  INFO(FLAGCX_INIT, "flagcxP2pSendProxySetup: Allocating shared memory size=%zu", shmSize);
  FLAGCXCHECK(flagcxShmAllocateShareableBuffer(shmSize, &resources->proxyInfo.desc, 
                                              (void**)&resources->proxyInfo.shm, NULL));
  
  // Initialize shared memory synchronization variables
  resources->proxyInfo.shm->sendMem.head = 0;
  resources->proxyInfo.shm->recvMem.tail = MAXSTEPS;
  
  INFO(FLAGCX_INIT, "flagcxP2pSendProxySetup: Copying response, shm=%p", resources->proxyInfo.shm);
  memcpy(respBuff, &resources->proxyInfo, sizeof(struct flagcxP2pShmProxyInfo));
  *done = 1;
  
  INFO(FLAGCX_INIT, "flagcxP2pSendProxySetup: Completed successfully");
  return flagcxSuccess;
}

flagcxResult_t flagcxP2pRecvProxySetup(struct flagcxProxyConnection* connection,
                                        struct flagcxProxyState* proxyState,
                                        void* reqBuff, int reqSize,
                                        void* respBuff, int respSize,
                                        int* done) {
  INFO(FLAGCX_INIT, "flagcxP2pRecvProxySetup: reqSize=%d respSize=%d expectedReqSize=%zu expectedRespSize=%zu",
       reqSize, respSize, sizeof(struct flagcxP2pRequest), sizeof(struct flagcxP2pBuff));
  
  struct flagcxP2pRequest* req = (struct flagcxP2pRequest*)reqBuff;
  
  if (reqSize != sizeof(struct flagcxP2pRequest)) {
    WARN("flagcxP2pRecvProxySetup: Invalid reqSize %d, expected %zu", 
         reqSize, sizeof(struct flagcxP2pRequest));
    return flagcxInternalError;
  }
  
  int size = req->size;
  if (respSize != sizeof(struct flagcxP2pBuff)) return flagcxInternalError;
  struct flagcxP2pBuff* p2pBuff = (struct flagcxP2pBuff*)respBuff;
  INFO(FLAGCX_INIT, "flagcxP2pRecvProxySetup: Allocating shareable buffer size=%d", size);
  FLAGCXCHECK(flagcxP2pAllocateShareableBuffer(size, req->refcount, &p2pBuff->ipcDesc, &p2pBuff->directPtr));
  p2pBuff->size = size;
  
  // transportResources is already set by transport.cc, no need to modify it
  *done = 1;
  return flagcxSuccess; 
}

flagcxResult_t flagcxP2pSendProxyConnect(struct flagcxProxyConnection* connection,
                                         struct flagcxProxyState* proxyState,
                                         void* reqBuff, int reqSize,
                                         void* respBuff, int respSize,
                                         int* done) {
  // Use the resources that was already allocated by transport.cc
  struct flagcxP2pResources* resources = (struct flagcxP2pResources*)connection->transportResources;
  
  if (resources == NULL) {
    WARN("flagcxP2pSendProxyConnect: transportResources is NULL");
    return flagcxInternalError;
  }
  
  // Recv sends recvFifo pointer to us
  if (reqSize != sizeof(void*)) {
    WARN("flagcxP2pSendProxyConnect: Invalid reqSize %d, expected %zu",
         reqSize, sizeof(void*));
    return flagcxInternalError;
  }
  
  resources->proxyInfo.recvFifo = *((char**)reqBuff);
  
  // Create CUDA stream and events for data transfers
  FLAGCXCHECK(deviceAdaptor->streamCreate(&resources->proxyInfo.stream));
  for (int i = 0; i < MAXSTEPS; i++) {
    FLAGCXCHECK(deviceAdaptor->eventCreate(&resources->proxyInfo.events[i], flagcxEventDisableTiming));
  }
  
  *done = 1;
  INFO(FLAGCX_INIT, "flagcxP2pSendProxyConnect: Completed, recvFifo=%p", resources->proxyInfo.recvFifo);
  return flagcxSuccess;
}

flagcxResult_t flagcxP2pRecvProxyConnect(struct flagcxProxyConnection* connection,
                                         struct flagcxProxyState* proxyState,
                                         void* reqBuff, int reqSize,
                                         void* respBuff, int respSize,
                                         int* done) {
  // Use the resources that was already allocated by transport.cc
  struct flagcxP2pResources* resources = (struct flagcxP2pResources*)connection->transportResources;
  
  if (resources == NULL) {
    WARN("flagcxP2pRecvProxyConnect: transportResources is NULL");
    return flagcxInternalError;
  }
  
  // Create CUDA stream and events for data transfers
  FLAGCXCHECK(deviceAdaptor->streamCreate(&resources->proxyInfo.stream));
  for (int i = 0; i < MAXSTEPS; i++) {
    FLAGCXCHECK(deviceAdaptor->eventCreate(&resources->proxyInfo.events[i], flagcxEventDisableTiming));
  }
  
  *done = 1;
  INFO(FLAGCX_INIT, "flagcxP2pRecvProxyConnect: Completed");
  return flagcxSuccess;
}

flagcxResult_t flagcxP2pAllocateShareableBuffer(size_t size, int directMap, 
                                                struct flagcxP2pIpcDesc *ipcDesc, 
                                                void **ptr) {
  FLAGCXCHECK(deviceAdaptor->deviceMalloc(ptr, size, flagcxMemDevice, NULL));
  size_t ipcSize = 0;
  flagcxResult_t res = deviceAdaptor->ipcMemHandleCreate(&ipcDesc->devIpc, &ipcSize);
  if (res != flagcxSuccess) {
    WARN("deviceAdaptor->ipcMemHandleCreate failed");
    deviceAdaptor->deviceFree(*ptr, flagcxMemDevice, NULL);
    *ptr = NULL;
    return res;
  }
  res = deviceAdaptor->ipcMemHandleGet(ipcDesc->devIpc, *ptr);
  if (res != flagcxSuccess) {
    WARN("deviceAdaptor->ipcMemHandleGet failed for ptr %p size %zu", *ptr, size);
    deviceAdaptor->ipcMemHandleFree(ipcDesc->devIpc);
    deviceAdaptor->deviceFree(*ptr, flagcxMemDevice, NULL);
    *ptr = NULL;
    return res;
  }
  INFO(FLAGCX_P2P | FLAGCX_ALLOC, "Allocated shareable buffer size %zu ptr %p", size, *ptr);

  return flagcxSuccess;
}

flagcxResult_t flagcxP2pFreeShareableBuffer(struct flagcxP2pIpcDesc *ipcDesc) {
  return flagcxSuccess;
}

flagcxResult_t flagcxP2pImportShareableBuffer(struct flagcxHeteroComm *comm, 
                                              int peer, size_t size, 
                                              struct flagcxP2pIpcDesc *ipcDesc, 
                                              void **devMemPtr) {
  *devMemPtr = NULL;
  flagcxResult_t res = deviceAdaptor->ipcMemHandleOpen(ipcDesc->devIpc, devMemPtr);
  if (res != flagcxSuccess) {
    WARN("Failed to open IPC handle for peer %d: error %d", peer, res);
    return res;
  }
  if (*devMemPtr == NULL) {
    WARN("IPC handle opened but devMemPtr is NULL for peer %d", peer);
    return flagcxInternalError;
  }
  INFO(FLAGCX_P2P, "Imported shareable buffer from peer %d device %d size %zu ptr %p", 
       peer, comm->cudaDev, size, *devMemPtr);

  return flagcxSuccess;
}