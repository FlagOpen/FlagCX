#include "p2p.h"
#include "adaptor.h"
#include "info.h"
#include <algorithm>
#include <string.h>  // for memcpy

flagcxResult_t flagcxP2pProxySend(struct flagcxP2pResources* resources, void *data,
                                  size_t size, struct flagcxProxyArgs* args) {
  if (!args->semaphore->pollStart()) return flagcxSuccess;
  
  if (args->copied < args->chunkSteps && 
      args->copied - args->transmitted <= args->sendStepMask) {
    int step = args->copied & args->sendStepMask;
    volatile uint64_t* recvTail = &resources->proxyInfo.shm->recvMem.tail;
    
    if (*recvTail > args->copied) {
      args->subs[step].stepSize = std::min(args->chunkSize, size - args->totalCopySize);
      args->subs[step].stepBuff = resources->proxyInfo.recvFifo + (args->stepSize * step);
      
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
  
  if (args->copied < args->chunkSteps && 
      args->copied - args->transmitted <= args->sendStepMask) {
    int step = args->copied & args->sendStepMask;
    volatile uint64_t* sendHead = &resources->proxyInfo.shm->sendMem.head;
    
    if (*sendHead > args->copied) {
      args->subs[step].stepSize = std::min(args->chunkSize, size - args->totalCopySize);
      args->subs[step].stepBuff = resources->proxyInfo.recvFifo + (args->stepSize * step);
      
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
  
  if (args->transmitted < args->copied) {
    int step = args->transmitted & args->sendStepMask;
    flagcxResult_t res = deviceAdaptor->eventQuery(resources->proxyInfo.events[step]);
    
    if (res == flagcxSuccess) {
      args->transmitted++;
      volatile uint64_t* recvTail = &resources->proxyInfo.shm->recvMem.tail;
      *recvTail = args->transmitted + MAXSTEPS;
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

flagcxResult_t flagcxP2pSendProxySetup(struct flagcxProxyConnection* connection,
                                        struct flagcxProxyState* proxyState,
                                        void* reqBuff,  int reqSize,
                                        void* respBuff, int respSize,
                                        int* done) {
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
  flagcxIpcMemHandle_t handlePtr = NULL;
  flagcxResult_t res = deviceAdaptor->ipcMemHandleCreate(&handlePtr, &ipcSize);
  if (res != flagcxSuccess) {
    WARN("deviceAdaptor->ipcMemHandleCreate failed");
    deviceAdaptor->deviceFree(*ptr, flagcxMemDevice, NULL);
    *ptr = NULL;
    return res;
  }
  
  // Get the actual IPC handle data (fills handlePtr->base for CUDA)
  res = deviceAdaptor->ipcMemHandleGet(handlePtr, *ptr);
  if (res != flagcxSuccess) {
    WARN("deviceAdaptor->ipcMemHandleGet failed for ptr %p size %zu", *ptr, size);
    deviceAdaptor->ipcMemHandleFree(handlePtr);
    deviceAdaptor->deviceFree(*ptr, flagcxMemDevice, NULL);
    *ptr = NULL;
    return res;
  }
  memcpy(&ipcDesc->handleData, handlePtr, sizeof(flagcxIpcHandleData));
  ipcDesc->size = size;
  
  // Free the temporary handle wrapper
  deviceAdaptor->ipcMemHandleFree(handlePtr);
  return flagcxSuccess;
}

flagcxResult_t flagcxP2pImportShareableBuffer(struct flagcxHeteroComm *comm, 
                                              int peer, size_t size, 
                                              struct flagcxP2pIpcDesc *ipcDesc, 
                                              void **devMemPtr) {
  *devMemPtr = NULL;
  
  // CRITICAL: Set device context before opening IPC handle
  FLAGCXCHECK(deviceAdaptor->setDevice(comm->cudaDev));
  flagcxIpcMemHandle_t handlePtr = (flagcxIpcMemHandle_t)&ipcDesc->handleData;
  
  flagcxResult_t res = deviceAdaptor->ipcMemHandleOpen(handlePtr, devMemPtr);
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

flagcxResult_t flagcxP2pSendProxyFree(struct flagcxP2pResources* resources) {
  if (resources == NULL) return flagcxSuccess;
  
  // Destroy CUDA events
  for (int s = 0; s < MAXSTEPS; s++) {
    if (resources->proxyInfo.events[s] != NULL) {
      FLAGCXCHECK(deviceAdaptor->eventDestroy(resources->proxyInfo.events[s]));
    }
  }
  
  // Destroy CUDA stream
  if (resources->proxyInfo.stream != NULL) {
    FLAGCXCHECK(deviceAdaptor->streamDestroy(resources->proxyInfo.stream));
  }
  
  // Close shared memory
  if (resources->shm != NULL) {
    FLAGCXCHECK(flagcxShmIpcClose(&resources->desc));
  }
  
  INFO(FLAGCX_P2P, "P2P Send proxy resources freed");
  return flagcxSuccess;
}

flagcxResult_t flagcxP2pRecvProxyFree(struct flagcxP2pResources* resources) {
  if (resources == NULL) return flagcxSuccess;
  
  // Destroy CUDA events
  for (int s = 0; s < MAXSTEPS; s++) {
    if (resources->proxyInfo.events[s] != NULL) {
      FLAGCXCHECK(deviceAdaptor->eventDestroy(resources->proxyInfo.events[s]));
    }
  }
  
  // Destroy CUDA stream
  if (resources->proxyInfo.stream != NULL) {
    FLAGCXCHECK(deviceAdaptor->streamDestroy(resources->proxyInfo.stream));
  }
  
  // Close shared memory (if not already closed)
  if (resources->shm != NULL) {
    FLAGCXCHECK(flagcxShmIpcClose(&resources->desc));
  }
  
  INFO(FLAGCX_P2P, "P2P Recv proxy resources freed");
  return flagcxSuccess;
}