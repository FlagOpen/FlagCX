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
  struct flagcxP2pShmProxyInfo* proxyInfo;
  size_t shmSize;

  if (respSize != sizeof(struct flagcxP2pShmProxyInfo)) return flagcxInternalError;
  FLAGCXCHECK(flagcxCalloc(&proxyInfo, 1));
  connection->transportResources = proxyInfo;

  // create shared memory segment
  shmSize = sizeof(struct flagcxP2pShm);
  FLAGCXCHECK(flagcxShmAllocateShareableBuffer(shmSize, &proxyInfo->desc, (void**)&proxyInfo->shm, NULL));
  memcpy(respBuff, proxyInfo, sizeof(struct flagcxP2pShmProxyInfo));
  *done = 1;
  return flagcxSuccess;
}

// 需要回过头来再看一下这里的p2pBuff在后面会被强转为 (struct flagcxP2pShmProxyInfo*) 使用，这里只传了一个p2pbuff的地址给自己使用
flagcxResult_t flagcxP2pRecvProxySetup(struct flagcxProxyConnection* connection,
                                        struct flagcxProxyState* proxyState,
                                        void* reqBuff, int reqSize,
                                        void* respBuff, int respSize,
                                        int* done) {
  struct flagcxP2pRequest* req = (struct flagcxP2pRequest*)reqBuff;
  if (reqSize != sizeof(struct flagcxP2pRequest)) return flagcxInternalError;
  int size = req->size;
  if (respSize != sizeof(struct flagcxP2pBuff)) return flagcxInternalError;
  struct flagcxP2pBuff* p2pBuff = (struct flagcxP2pBuff*)respBuff;
  FLAGCXCHECK(flagcxP2pAllocateShareableBuffer(size, req->refcount, &p2pBuff->ipcDesc, &p2pBuff->directPtr));
  p2pBuff->size = size;
  connection->transportResources = p2pBuff->directPtr;
  *done = 1;
  return flagcxSuccess; 
}

flagcxResult_t flagcxP2pSendProxyConnect(struct flagcxProxyConnection* connection,
                                         struct flagcxProxyState* proxyState,
                                         void* reqBuff, int reqSize,
                                         void* respBuff, int respSize,
                                         int* done) {
  ;
  return flagcxSuccess;
}

flagcxResult_t flagcxP2pRecvProxyConnect(struct flagcxProxyConnection* connection,
                                         struct flagcxProxyState* proxyState,
                                         void* reqBuff, int reqSize,
                                         void* respBuff, int respSize,
                                         int* done) {
  ;
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
  deviceAdaptor->ipcMemHandleOpen(ipcDesc->devIpc, devMemPtr);
  INFO(FLAGCX_P2P, "Imported shareable buffer from peer %d device %d size %zu ptr %p", 
       peer, comm->cudaDev, size, *devMemPtr);

  return flagcxSuccess;
}