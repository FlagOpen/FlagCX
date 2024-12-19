/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef FLAGCX_INT_NET_H_
#define FLAGCX_INT_NET_H_

#include "flagcx_net.h"
#include "comm.h"
#include "check.h"
#include <socket.h>

typedef char flagcxNetHandle_t[FLAGCX_NET_HANDLE_MAXSIZE];

#define REGMRBUFFERSIZE (64ULL*1024*1024)
#define CHUNCKSIZE (4ULL*1024*1024)
#define MAXSENDSTEP (REGMRBUFFERSIZE/CHUNCKSIZE)
static_assert((MAXSENDSTEP&(MAXSENDSTEP-1))==0, "send step must a power of 2");

flagcxResult_t flagcxNetPluginInit();
flagcxResult_t flagcxNetInit(struct flagcxHeteroComm* comm);
int flagcxNetVersion(struct flagcxHeteroComm* comm);

// Test whether the current GPU support GPU Direct RDMA.
flagcxResult_t flagcxGpuGdrSupport(struct flagcxHeteroComm* comm, int* gdrSupport);

extern flagcxNet_t flagcxNetIb;
extern flagcxNet_t flagcxNetSocket;

struct sendNetResources {
  void* netSendComm;
  struct flagcxSendMem* sendMem;
  struct flagcxRecvMem* recvMem;

  int tpRank;
  int tpLocalRank;
  int tpRemoteRank;
  int netDev;
  int useGdr;
  int useDmaBuf;
  int maxRecvs;
  uint64_t* gdcSync;
  void* gdrDesc;
  int shared;
  int channelId;
  int connIndex;
  char* buffers[FLAGCX_NUM_PROTOCOLS];
  int buffSizes[FLAGCX_NUM_PROTOCOLS];
  void* mhandles[1];/*just one for memory copy from device to gdr buffer*/
  uint64_t step;
  uint64_t llLastCleaning;
  int netDeviceVersion;
  flagcxNetDeviceType netDeviceType;
  flagcxNetDeviceHandle_t* netDeviceHandle;
  flagcxStream_t cpStream; 
};

struct recvNetResources {
  void* netListenComm;
  void* netRecvComm;
  struct flagcxSendMem* sendMem;
  struct flagcxRecvMem* recvMem;

  int tpRank;
  int tpLocalRank;
  int tpRemoteRank;
  int tpRemoteProxyRank;
  int netDev;
  int useGdr;
  int useDmaBuf;
  int needFlush;
  int maxRecvs;
  uint64_t* gdcSync;
  uint64_t* gdcFlush;
  void* gdrDesc;
  int shared;
  int channelId;
  int connIndex;
  char* buffers[FLAGCX_NUM_PROTOCOLS];
  int buffSizes[FLAGCX_NUM_PROTOCOLS];
  void* mhandles[FLAGCX_NUM_PROTOCOLS];
  uint64_t step;
  uint64_t llLastCleaning;
  int netDeviceVersion;
  flagcxNetDeviceType netDeviceType;
  flagcxNetDeviceHandle_t* netDeviceHandle;
  flagcxStream_t cpStream; 
};

enum flagcxIbCommState {
  flagcxIbCommStateStart = 0,
  flagcxIbCommStateConnect = 1,
  flagcxIbCommStateAccept = 3,
  flagcxIbCommStateSend = 4,
  flagcxIbCommStateRecv = 5,
  flagcxIbCommStateConnecting = 6,
  flagcxIbCommStateConnected = 7,
  flagcxIbCommStatePendingReady = 8,
};

struct flagcxIbCommStage {
  enum flagcxIbCommState state;
  int offset;
  void* buffer;
  void* comm;
};

struct sendRecvDataInfo{
  void*  data;
  size_t size;
};

struct flagcxIbHandle {
  union flagcxSocketAddress connectAddr; // Filled by the target
  uint64_t magic; // random number to help debugging
  struct flagcxIbCommStage stage; // Used by the other side when connecting
};

flagcxResult_t flagcxSendRegMr(flagcxHeteroComm_t comm, void* data, size_t size, int peer, int channel);
flagcxResult_t flagcxRecvRegMr(flagcxHeteroComm_t comm, void* data, size_t size, int peer, int channel);
flagcxResult_t flagcxProxySend(sendNetResources *resources, void* data, size_t size, flagcxProxyArgs *args);
flagcxResult_t flagcxProxyRecv(recvNetResources *resources, void* data, size_t size, flagcxProxyArgs *args);
flagcxResult_t flagcxSend(flagcxHeteroComm_t comm, void* data, size_t size, int peer, int channel);
flagcxResult_t flagcxRecv(flagcxHeteroComm_t comm, void* data, size_t size, int peer, int channel);
flagcxResult_t flagcxSendProxyFree(sendNetResources *resources);
flagcxResult_t flagcxRecvProxyFree(recvNetResources *resources);

#endif
