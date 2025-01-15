/*************************************************************************
 * Copyright (c) 2015-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#ifndef FLAGCX_BOOTSTRAP_H_
#define FLAGCX_BOOTSTRAP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include "flagcx.h"
#include "socket.h"

struct flagcxBootstrapHandle {
  uint64_t magic;
  union flagcxSocketAddress addr;
};
static_assert(sizeof(struct flagcxBootstrapHandle) <= sizeof(flagcxUniqueId), "Bootstrap handle is too large to fit inside FLAGCX unique ID");

struct bootstrapState {
  struct flagcxSocket listenSock;
  struct flagcxSocket ringRecvSocket;
  struct flagcxSocket ringSendSocket;
  union flagcxSocketAddress* peerCommAddresses;
  union flagcxSocketAddress* peerProxyAddresses;
  struct unexConn* unexpectedConnections;
  int rank;
  int nranks;
  uint64_t magic;
  volatile uint32_t* abortFlag;
};

flagcxResult_t bootstrapNetInit();
flagcxResult_t bootstrapCreateRoot(struct flagcxBootstrapHandle* handle, bool idFromEnv);
flagcxResult_t bootstrapGetUniqueId(struct flagcxBootstrapHandle* handle);
flagcxResult_t bootstrapInit(struct flagcxBootstrapHandle* handle, void* commState);
flagcxResult_t bootstrapAllGather(void* commState, void* allData, int size);
/*
 * All-Reduce
 *
 * Reduces data arrays of length count(NOT bytes size) in sendbuff using op operation, and
 * leaves identical copies of result on each recvbuff.
 *
 * In-place operation will happen if sendbuff == recvbuff.
 */
flagcxResult_t bootstrapAllReduce(void* commState, const void* sendbuff, void* recvbuff, size_t count,
                                 flagcxDataType_t datatype, flagcxRedOp_t op);
flagcxResult_t bootstrapSend(void* commState, int peer, int tag, void* data, int size);
flagcxResult_t bootstrapRecv(void* commState, int peer, int tag, void* data, int size);
flagcxResult_t bootstrapBarrier(void* commState, int rank, int nranks, int tag);
flagcxResult_t bootstrapBroadcast(void* commState, int rank, int nranks, int root, void* bcastData, int size);
flagcxResult_t bootstrapIntraNodeBarrier(void* commState, int *ranks, int rank, int nranks, int tag);
flagcxResult_t bootstrapIntraNodeBroadcast(void* commState, int *ranks, int rank, int nranks, int root, void* bcastData, int size);
flagcxResult_t bootstrapClose(void* commState);
flagcxResult_t bootstrapAbort(void* commState);

#ifdef __cplusplus
} // end extern "C"
#endif

#endif
