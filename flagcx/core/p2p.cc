#include "p2p.h"
#include "adaptor.h"
#include "info.h"

flagcxResult_t flagcxP2pProxySend(struct flagcxProxyState* proxyState, struct flagcxProxyArgs* args) {
  ;
  return flagcxSuccess;
}

flagcxResult_t flagcxP2pProxyRecv(struct flagcxProxyState* proxyState, struct flagcxProxyArgs* args) {
  ;
  return flagcxSuccess;
}

flagcxResult_t flagcxP2pSendProxySetup(struct flagcxProxyConnection* connection,
                                        struct flagcxProxyState* proxyState,
                                        void* reqBuff,  int reqSize,
                                        void* respBuff, int respSize,
                                        int* done) {
  INFO(FLAGCX_INIT, "flagcxP2pSendProxySetup: reqSize=%d respSize=%d expectedRespSize=%zu",
       reqSize, respSize, sizeof(struct flagcxP2pShmProxyInfo));
  
  struct flagcxP2pShmProxyInfo* proxyInfo;
  size_t shmSize;

  if (respSize != sizeof(struct flagcxP2pShmProxyInfo)) return flagcxInternalError;
  FLAGCXCHECK(flagcxCalloc(&proxyInfo, 1));
  connection->transportResources = proxyInfo;

  // create shared memory segment
  shmSize = sizeof(struct flagcxP2pShm);
  INFO(FLAGCX_INIT, "flagcxP2pSendProxySetup: Allocating shared memory size=%zu", shmSize);
  FLAGCXCHECK(flagcxShmAllocateShareableBuffer(shmSize, &proxyInfo->desc, (void**)&proxyInfo->shm, NULL));
  
  INFO(FLAGCX_INIT, "flagcxP2pSendProxySetup: Copying response, shm=%p", proxyInfo->shm);
  memcpy(respBuff, proxyInfo, sizeof(struct flagcxP2pShmProxyInfo));
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
  struct flagcxP2pShmProxyInfo* proxyInfo;
  FLAGCXCHECK(flagcxCalloc(&proxyInfo, 1));
  connection->transportResources = proxyInfo;
  *done = 1;
  return flagcxSuccess; 
}

flagcxResult_t flagcxP2pSendProxyConnect(struct flagcxProxyConnection* connection,
                                         struct flagcxProxyState* proxyState,
                                         void* reqBuff, int reqSize,
                                         void* respBuff, int respSize,
                                         int* done) {
  // Get proxyInfo that was set up in RecvProxySetup
  struct flagcxP2pShmProxyInfo* proxyInfo = 
      (struct flagcxP2pShmProxyInfo*)connection->transportResources;
  
  if (proxyInfo == NULL) {
    WARN("flagcxP2pSendProxyConnect: proxyInfo is NULL");
    return flagcxInternalError;
  }
  
  // Recv sends recvFifo pointer to us
  if (reqSize != sizeof(void*)) {
    WARN("flagcxP2pSendProxyConnect: Invalid reqSize %d, expected %zu",
         reqSize, sizeof(void*));
    return flagcxInternalError;
  }
  
  proxyInfo->recvFifo = *((char**)reqBuff);
  
  // Create CUDA stream and events for data transfers
  FLAGCXCHECK(deviceAdaptor->streamCreate(&proxyInfo->stream));
  for (int i = 0; i < MAXSTEPS; i++) {
    FLAGCXCHECK(deviceAdaptor->eventCreate(&proxyInfo->events[i]));
  }
  
  *done = 1;
  INFO(FLAGCX_INIT, "flagcxP2pSendProxyConnect: Completed, recvFifo=%p", proxyInfo->recvFifo);
  return flagcxSuccess;
}

flagcxResult_t flagcxP2pRecvProxyConnect(struct flagcxProxyConnection* connection,
                                         struct flagcxProxyState* proxyState,
                                         void* reqBuff, int reqSize,
                                         void* respBuff, int respSize,
                                         int* done) {
  // Get proxyInfo that was set up in RecvProxySetup
  struct flagcxP2pShmProxyInfo* proxyInfo = 
      (struct flagcxP2pShmProxyInfo*)connection->transportResources;
  
  if (proxyInfo == NULL) {
    WARN("flagcxP2pRecvProxyConnect: proxyInfo is NULL");
    return flagcxInternalError;
  }
  
  // Create CUDA stream and events for data transfers
  FLAGCXCHECK(deviceAdaptor->streamCreate(&proxyInfo->stream));
  for (int i = 0; i < MAXSTEPS; i++) {
    FLAGCXCHECK(deviceAdaptor->eventCreate(&proxyInfo->events[i]));
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