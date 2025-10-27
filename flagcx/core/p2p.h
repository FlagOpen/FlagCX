#ifndef FLAGCX_INT_P2P_H_
#define FLAGCX_INT_P2P_H_

#include <stddef.h>
#include "comm.h"
#include "device.h"
#include "adaptor.h"
#include "transport.h"
#include "shmutils.h"
#include "check.h"

#ifdef __cplusplus
extern "C" {
#endif

struct flagcxP2pRequest {
  size_t size;
  int refcount;
};

struct flagcxP2pIpcDesc {
  flagcxIpcMemHandle_t devIpc;
  size_t size;
};

struct flagcxP2pBuff {
  void* directPtr;
  size_t size;
  flagcxP2pIpcDesc ipcDesc;
};

struct flagcxP2pConnectInfo {
  int rank;
  int read;
  flagcxP2pBuff p2pBuff;
  flagcxShmIpcDesc_t desc;
};

struct flagcxP2pShm {
  struct flagcxSendMem sendMem;
  struct flagcxRecvMem recvMem;
};

// need to make sure this matches flagcxP2pShmProxyInfo in p2p.cc
struct flagcxP2pShmProxyInfo {
  // CPU side
  struct flagcxP2pShm* shm;
  flagcxShmIpcDesc_t desc;

  // GPU side
  char* recvFifo;
  flagcxStream_t stream;
  flagcxEvent_t events[MAXSTEPS];
};

struct flagcxP2pResources {
  union {
    struct flagcxSendMem* sendDevMem;
    struct flagcxRecvMem* recvDevMem;
  };
  void* sendMemIpc;
  int sendMemSameProc;
  void* recvMemIpc;
  int recvMemSameProc;
  struct flagcxP2pShmProxyInfo proxyInfo;
  struct flagcxP2pShm* shm;
  flagcxShmIpcDesc_t desc;
};

flagcxResult_t flagcxP2pProxySend(struct flagcxProxyState* proxyState, struct flagcxProxyArgs* args);

flagcxResult_t flagcxP2pProxyRecv(struct flagcxProxyState* proxyState, struct flagcxProxyArgs* args);

flagcxResult_t flagcxP2pSendProxySetup(struct flagcxProxyConnection* connection,
                                       struct flagcxProxyState* proxyState,
                                       void* reqBuff,  int reqSize,
                                       void* respBuff, int respSize,
                                       int* done);

flagcxResult_t flagcxP2pRecvProxySetup(struct flagcxProxyConnection* connection,
                                       struct flagcxProxyState* proxyState,
                                       void* reqBuff, int reqSize,
                                       void* respBuff, int respSize,
                                       int* done);

flagcxResult_t flagcxP2pSendProxyConnect(struct flagcxProxyConnection* connection,
                                         struct flagcxProxyState* proxyState,
                                         void* reqBuff, int reqSize,
                                         void* respBuff, int respSize,
                                         int* done);

flagcxResult_t flagcxP2pRecvProxyConnect(struct flagcxProxyConnection* connection,
                                         struct flagcxProxyState* proxyState,
                                         void* reqBuff, int reqSize,
                                         void* respBuff, int respSize,
                                         int* done);

flagcxResult_t flagcxP2pAllocateShareableBuffer(size_t size, int directMap, 
                                                struct flagcxP2pIpcDesc *ipcDesc, 
                                                void **ptr);

flagcxResult_t flagcxP2pImportShareableBuffer(struct flagcxHeteroComm *comm, 
                                              int peer, size_t size, 
                                              struct flagcxP2pIpcDesc *ipcDesc, 
                                              void **devMemPtr);

flagcxResult_t flagcxP2pFreeShareableBuffer(struct flagcxP2pIpcDesc *ipcDesc, void *ptr);

flagcxResult_t flagcxP2pCloseImportedBuffer(void *devMemPtr);

#ifdef __cplusplus
}
#endif

#endif // FLAGCX_INT_P2P_H_