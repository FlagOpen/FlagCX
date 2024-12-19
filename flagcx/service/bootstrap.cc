/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "check.h"
#include "debug.h"
#include "utils.h"
#include "alloc.h"
#include "bootstrap.h"
#include <unistd.h>
#include <sys/types.h>
#include "param.h"
#include "comm.h"

struct bootstrapRootArgs {
  struct flagcxSocket* listenSock;
  uint64_t magic;
};

/* Init functions */
static char bootstrapNetIfName[MAX_IF_NAME_SIZE+1];
union flagcxSocketAddress bootstrapNetIfAddr;
static int bootstrapNetInitDone = 0;
pthread_mutex_t bootstrapNetLock = PTHREAD_MUTEX_INITIALIZER;

flagcxResult_t bootstrapNetInit() {
  if (bootstrapNetInitDone == 0) {
    pthread_mutex_lock(&bootstrapNetLock);
    if (bootstrapNetInitDone == 0) {
      const char* env = flagcxGetEnv("FLAGCX_COMM_ID");
      if (env) {
        union flagcxSocketAddress remoteAddr;
        if (flagcxSocketGetAddrFromString(&remoteAddr, env) != flagcxSuccess) {
          WARN("Invalid FLAGCX_COMM_ID, please use format: <ipv4>:<port> or [<ipv6>]:<port> or <hostname>:<port>");
          pthread_mutex_unlock(&bootstrapNetLock);
          return flagcxInvalidArgument;
        }
        if (flagcxFindInterfaceMatchSubnet(bootstrapNetIfName, &bootstrapNetIfAddr, &remoteAddr, MAX_IF_NAME_SIZE, 1) <= 0) {
          WARN("NET/Socket : No usable listening interface found");
          pthread_mutex_unlock(&bootstrapNetLock);
          return flagcxSystemError;
        }
      } else {
        int nIfs = flagcxFindInterfaces(bootstrapNetIfName, &bootstrapNetIfAddr, MAX_IF_NAME_SIZE, 1);
        if (nIfs <= 0) {
          WARN("Bootstrap : no socket interface found");
          pthread_mutex_unlock(&bootstrapNetLock);
          return flagcxInternalError;
        }
      }
      char line[SOCKET_NAME_MAXLEN+MAX_IF_NAME_SIZE+2];
      sprintf(line, " %s:", bootstrapNetIfName);
      flagcxSocketToString(&bootstrapNetIfAddr, line+strlen(line));
      INFO(FLAGCX_NET, "Bootstrap : Using%s", line);
      bootstrapNetInitDone = 1;
    }
    pthread_mutex_unlock(&bootstrapNetLock);
  }
  return flagcxSuccess;
}

/* Socket Interface Selection type */
enum bootstrapInterface_t { findSubnetIf = -1, dontCareIf = -2 };

// Additional sync functions
static flagcxResult_t bootstrapNetSend(struct flagcxSocket* sock, void* data, int size) {
  FLAGCXCHECK(flagcxSocketSend(sock, &size, sizeof(int)));
  FLAGCXCHECK(flagcxSocketSend(sock, data, size));
  return flagcxSuccess;
}
static flagcxResult_t bootstrapNetRecv(struct flagcxSocket* sock, void* data, int size) {
  int recvSize;
  FLAGCXCHECK(flagcxSocketRecv(sock, &recvSize, sizeof(int)));
  if (recvSize > size) {
    WARN("Message truncated : received %d bytes instead of %d", recvSize, size);
    return flagcxInternalError;
  }
  FLAGCXCHECK(flagcxSocketRecv(sock, data, std::min(recvSize, size)));
  return flagcxSuccess;
}
static flagcxResult_t bootstrapNetSendRecv(struct flagcxSocket* sendSock, void* sendData, int sendSize, struct flagcxSocket* recvSock, void* recvData, int recvSize) {
  int senderRecvSize;
  FLAGCXCHECK(flagcxSocketSendRecv(sendSock, &sendSize, sizeof(int), recvSock, &senderRecvSize, sizeof(int)));
  if (senderRecvSize > recvSize) {
    WARN("Message truncated : received %d bytes instead of %d", senderRecvSize, recvSize);
    return flagcxInternalError;
  }
  FLAGCXCHECK(flagcxSocketSendRecv(sendSock, sendData, sendSize, recvSock, recvData, recvSize));
  return flagcxSuccess;
}

struct extInfo {
  int rank;
  int nranks;
  union flagcxSocketAddress extAddressListenRoot;
  union flagcxSocketAddress extAddressListen;
};

#include <sys/resource.h>

static flagcxResult_t setFilesLimit() {
  struct rlimit filesLimit;
  SYSCHECK(getrlimit(RLIMIT_NOFILE, &filesLimit), "getrlimit");
  filesLimit.rlim_cur = filesLimit.rlim_max;
  SYSCHECK(setrlimit(RLIMIT_NOFILE, &filesLimit), "setrlimit");
  return flagcxSuccess;
}

static void *bootstrapRoot(void* rargs) {
  struct bootstrapRootArgs* args = (struct bootstrapRootArgs*)rargs;
  struct flagcxSocket* listenSock = args->listenSock;
  uint64_t magic = args->magic;
  flagcxResult_t res = flagcxSuccess;
  int nranks = 0, c = 0;
  struct extInfo info;
  union flagcxSocketAddress *rankAddresses = NULL;
  union flagcxSocketAddress *rankAddressesRoot = NULL; // for initial rank <-> root information exchange
  union flagcxSocketAddress *zero = NULL;
  FLAGCXCHECKGOTO(flagcxCalloc(&zero, 1), res, out);
  setFilesLimit();

  TRACE(FLAGCX_INIT, "BEGIN");
  /* Receive addresses from all ranks */
  do {
    struct flagcxSocket sock;
    FLAGCXCHECKGOTO(flagcxSocketInit(&sock), res, out);
    FLAGCXCHECKGOTO(flagcxSocketAccept(&sock, listenSock), res, out);
    FLAGCXCHECKGOTO(bootstrapNetRecv(&sock, &info, sizeof(info)), res, out);
    FLAGCXCHECKGOTO(flagcxSocketClose(&sock), res, out);

    if (c == 0) {
      nranks = info.nranks;
      FLAGCXCHECKGOTO(flagcxCalloc(&rankAddresses, nranks), res, out);
      FLAGCXCHECKGOTO(flagcxCalloc(&rankAddressesRoot, nranks), res, out);
    }

    if (nranks != info.nranks) {
      WARN("Bootstrap Root : mismatch in rank count from procs %d : %d", nranks, info.nranks);
      goto out;
    }

    if (memcmp(zero, &rankAddressesRoot[info.rank], sizeof(union flagcxSocketAddress)) != 0) {
      WARN("Bootstrap Root : rank %d of %d ranks has already checked in", info.rank, nranks);
      goto out;
    }

    INFO(FLAGCX_INIT, "Bootstrap Root : rank %d of %d ranks checked in", info.rank, nranks);

    // Save the connection handle for that rank
    memcpy(rankAddressesRoot+info.rank, &info.extAddressListenRoot, sizeof(union flagcxSocketAddress));
    memcpy(rankAddresses+info.rank, &info.extAddressListen, sizeof(union flagcxSocketAddress));

    ++c;
    TRACE(FLAGCX_INIT, "Received connect from rank %d total %d/%d",  info.rank, c, nranks);
  } while (c < nranks);
  TRACE(FLAGCX_INIT, "COLLECTED ALL %d HANDLES", nranks);

  // Send the connect handle for the next rank in the AllGather ring
  for (int r=0; r<nranks; ++r) {
    int next = (r+1) % nranks;
    struct flagcxSocket sock;
    FLAGCXCHECKGOTO(flagcxSocketInit(&sock, rankAddressesRoot+r, magic, flagcxSocketTypeBootstrap), res, out);
    FLAGCXCHECKGOTO(flagcxSocketConnect(&sock), res, out);
    FLAGCXCHECKGOTO(bootstrapNetSend(&sock, rankAddresses+next, sizeof(union flagcxSocketAddress)), res, out);
    FLAGCXCHECKGOTO(flagcxSocketClose(&sock), res, out);
  }
  INFO(FLAGCX_INIT, "SENT OUT ALL %d HANDLES", nranks);

out:
  if (listenSock != NULL) {
    flagcxSocketClose(listenSock);
    free(listenSock);
  }
  if (rankAddresses) free(rankAddresses);
  if (rankAddressesRoot) free(rankAddressesRoot);
  if (zero) free(zero);
  free(rargs);

  TRACE(FLAGCX_INIT, "DONE");
  return NULL;
}

flagcxResult_t bootstrapCreateRoot(struct flagcxBootstrapHandle* handle, bool idFromEnv) {
  struct flagcxSocket* listenSock;
  struct bootstrapRootArgs* args;
  pthread_t thread;

  FLAGCXCHECK(flagcxCalloc(&listenSock, 1));
  FLAGCXCHECK(flagcxSocketInit(listenSock, &handle->addr, handle->magic, flagcxSocketTypeBootstrap, NULL, 0));
  FLAGCXCHECK(flagcxSocketListen(listenSock));
  FLAGCXCHECK(flagcxSocketGetAddr(listenSock, &handle->addr));

  FLAGCXCHECK(flagcxCalloc(&args, 1));
  args->listenSock = listenSock;
  args->magic = handle->magic;
  NEQCHECK(pthread_create(&thread, NULL, bootstrapRoot, (void*)args), 0);
  flagcxSetThreadName(thread, "FLAGCX bootstrapRoot");
  NEQCHECK(pthread_detach(thread), 0); // will not be pthread_join()'d
  return flagcxSuccess;
}

flagcxResult_t bootstrapGetUniqueId(struct flagcxBootstrapHandle* handle) {
  memset(handle, 0, sizeof(flagcxBootstrapHandle));
  FLAGCXCHECK(getRandomData(&handle->magic, sizeof(handle->magic)));

  const char* env = flagcxGetEnv("FLAGCX_COMM_ID");
  if (env) {
    INFO(FLAGCX_ENV, "FLAGCX_COMM_ID set by environment to %s", env);
    if (flagcxSocketGetAddrFromString(&handle->addr, env) != flagcxSuccess) {
      WARN("Invalid FLAGCX_COMM_ID, please use format: <ipv4>:<port> or [<ipv6>]:<port> or <hostname>:<port>");
      return flagcxInvalidArgument;
    }
  } else {
    memcpy(&handle->addr, &bootstrapNetIfAddr, sizeof(union flagcxSocketAddress));
    FLAGCXCHECK(bootstrapCreateRoot(handle, false));
  }

  return flagcxSuccess;
}

struct unexConn {
  int peer;
  int tag;
  struct flagcxSocket sock;
  struct unexConn* next;
};

flagcxResult_t bootstrapInit(struct flagcxBootstrapHandle* handle, void* commState) {
  struct bootstrapState* state = (struct bootstrapState*)commState;
  int rank = state->rank;
  int nranks = state->nranks;
  flagcxSocketAddress nextAddr;
  struct flagcxSocket sock, listenSockRoot;
  struct extInfo info = { 0 };

  TRACE(FLAGCX_INIT, "rank %d nranks %d", rank, nranks);

  info.rank = rank;
  info.nranks = nranks;
  // Create socket for other ranks to contact me
  FLAGCXCHECK(flagcxSocketInit(&state->listenSock, &bootstrapNetIfAddr, state->magic, flagcxSocketTypeBootstrap, state->abortFlag));
  FLAGCXCHECK(flagcxSocketListen(&state->listenSock));
  FLAGCXCHECK(flagcxSocketGetAddr(&state->listenSock, &info.extAddressListen));

  // Create socket for root to contact me
  FLAGCXCHECK(flagcxSocketInit(&listenSockRoot, &bootstrapNetIfAddr, state->magic, flagcxSocketTypeBootstrap, state->abortFlag));
  FLAGCXCHECK(flagcxSocketListen(&listenSockRoot));
  FLAGCXCHECK(flagcxSocketGetAddr(&listenSockRoot, &info.extAddressListenRoot));

  // stagger connection times to avoid an overload of the root
  if (nranks > 128) {
    long msec = rank;
    struct timespec tv;
    tv.tv_sec = msec / 1000;
    tv.tv_nsec = 1000000 * (msec % 1000);
    TRACE(FLAGCX_INIT, "rank %d delaying connection to root by %ld msec", rank, msec);
    (void) nanosleep(&tv, NULL);
  }

  // send info on my listening socket to root
  FLAGCXCHECK(flagcxSocketInit(&sock, &handle->addr, state->magic, flagcxSocketTypeBootstrap, state->abortFlag));
  FLAGCXCHECK(flagcxSocketConnect(&sock));
  FLAGCXCHECK(bootstrapNetSend(&sock, &info, sizeof(info)));
  FLAGCXCHECK(flagcxSocketClose(&sock));

  // get info on my "next" rank in the bootstrap ring from root
  FLAGCXCHECK(flagcxSocketInit(&sock));
  FLAGCXCHECK(flagcxSocketAccept(&sock, &listenSockRoot));
  FLAGCXCHECK(bootstrapNetRecv(&sock, &nextAddr, sizeof(union flagcxSocketAddress)));
  FLAGCXCHECK(flagcxSocketClose(&sock));
  FLAGCXCHECK(flagcxSocketClose(&listenSockRoot));

  FLAGCXCHECK(flagcxSocketInit(&state->ringSendSocket, &nextAddr, state->magic, flagcxSocketTypeBootstrap, state->abortFlag));
  FLAGCXCHECK(flagcxSocketConnect(&state->ringSendSocket));
  // Accept the connect request from the previous rank in the AllGather ring
  FLAGCXCHECK(flagcxSocketInit(&state->ringRecvSocket));
  FLAGCXCHECK(flagcxSocketAccept(&state->ringRecvSocket, &state->listenSock));

  // AllGather all listen handlers
  FLAGCXCHECK(flagcxCalloc(&state->peerCommAddresses, nranks));
  FLAGCXCHECK(flagcxSocketGetAddr(&state->listenSock, state->peerCommAddresses+rank));
  FLAGCXCHECK(bootstrapAllGather(state, state->peerCommAddresses, sizeof(union flagcxSocketAddress)));

  INFO(FLAGCX_INIT, "rank %d nranks %d - DONE", rank, nranks);

  return flagcxSuccess;
}

// Bootstrap send/receive functions
//
// We do not keep connections opened with all ranks at all times, and we have no guarantee
// that connections to our unique listen socket will arrive in the same order as we need
// them. Therefore, when establishing a connection, the sender sends a (peer, tag) tuple to
// allow the receiver to identify the flow, and keep it in an unexpected queue if needed.

flagcxResult_t bootstrapConnect(void* commState, int peer, int tag, struct flagcxSocket* sock) {
  flagcxResult_t ret = flagcxSuccess;
  struct bootstrapState* state = (struct bootstrapState*)commState;

  FLAGCXCHECKGOTO(flagcxSocketInit(sock, state->peerCommAddresses+peer, state->magic, flagcxSocketTypeBootstrap), ret, fail);
  FLAGCXCHECKGOTO(flagcxSocketConnect(sock), ret, fail);
  FLAGCXCHECKGOTO(bootstrapNetSend(sock, &state->rank, sizeof(int)), ret, fail);
  FLAGCXCHECKGOTO(bootstrapNetSend(sock, &tag, sizeof(int)), ret, fail);
  return flagcxSuccess;
fail:
  FLAGCXCHECK(flagcxSocketClose(sock));
  return ret;
}

flagcxResult_t bootstrapSend(void* commState, int peer, int tag, void* data, int size) {
  flagcxResult_t ret = flagcxSuccess;
  struct flagcxSocket sock;

  TRACE(FLAGCX_BOOTSTRAP, "Sending to peer=%d tag=%d size=%d", peer, tag, size);
  FLAGCXCHECK(bootstrapConnect(commState, peer, tag, &sock));
  FLAGCXCHECKGOTO(bootstrapNetSend(&sock, data, size), ret, exit);

  TRACE(FLAGCX_BOOTSTRAP, "Sent to peer=%d tag=%d size=%d", peer, tag, size);

exit:
  FLAGCXCHECK(flagcxSocketClose(&sock));
  return ret;
}

flagcxResult_t unexpectedEnqueue(struct bootstrapState* state, int peer, int tag, struct flagcxSocket* sock) {
  // New unex
  struct unexConn* unex;
  FLAGCXCHECK(flagcxCalloc(&unex, 1));
  unex->peer = peer;
  unex->tag = tag;
  memcpy(&unex->sock, sock, sizeof(struct flagcxSocket));

  // Enqueue
  struct unexConn* list = state->unexpectedConnections;
  if (list == NULL) {
    state->unexpectedConnections = unex;
    return flagcxSuccess;
  }
  while (list->next) list = list->next;
  list->next = unex;
  return flagcxSuccess;
}

flagcxResult_t unexpectedDequeue(struct bootstrapState* state, int peer, int tag, struct flagcxSocket* sock, int* found) {
  struct unexConn* elem = state->unexpectedConnections;
  struct unexConn* prev = NULL;
  *found = 0;
  while (elem) {
    if (elem->peer == peer && elem->tag == tag) {
      if (prev == NULL) {
        state->unexpectedConnections = elem->next;
      } else {
        prev->next = elem->next;
      }
      memcpy(sock, &elem->sock, sizeof(struct flagcxSocket));
      free(elem);
      *found = 1;
      return flagcxSuccess;
    }
    prev = elem;
    elem = elem->next;
  }
  return flagcxSuccess;
}

static void unexpectedFree(struct bootstrapState* state) {
  struct unexConn* elem = state->unexpectedConnections;
  struct unexConn* prev = NULL;

  while (elem) {
    prev = elem;
    elem = elem->next;
    free(prev);
  }
  return;
}

// We can't know who we'll receive from, so we need to receive everything at once
flagcxResult_t bootstrapAccept(void* commState, int peer, int tag, struct flagcxSocket* sock) {
  flagcxResult_t ret = flagcxSuccess;
  struct bootstrapState* state = (struct bootstrapState*)commState;
  int newPeer, newTag;

  // Search unexpected connections first
  int found;
  FLAGCXCHECK(unexpectedDequeue(state, peer, tag, sock, &found));
  if (found) return flagcxSuccess;

  // Then look for new connections
  while (1) {
    FLAGCXCHECKGOTO(flagcxSocketInit(sock), ret, fail);
    FLAGCXCHECKGOTO(flagcxSocketAccept(sock, &state->listenSock), ret, fail);
    FLAGCXCHECKGOTO(bootstrapNetRecv(sock, &newPeer, sizeof(int)), ret, fail);
    FLAGCXCHECKGOTO(bootstrapNetRecv(sock, &newTag, sizeof(int)), ret, fail);
    if (newPeer == peer && newTag == tag) return flagcxSuccess;
    FLAGCXCHECKGOTO(unexpectedEnqueue(state, newPeer, newTag, sock), ret, fail);
  }
  return flagcxSuccess;
fail:
  FLAGCXCHECK(flagcxSocketClose(sock));
  return ret;
}

// We can't know who we'll receive from, so we need to receive everything at once
flagcxResult_t bootstrapRecv(void* commState, int peer, int tag, void* data, int size) {
  flagcxResult_t ret;
  struct flagcxSocket sock;
  FLAGCXCHECK(bootstrapAccept(commState, peer, tag, &sock));
  TRACE(FLAGCX_BOOTSTRAP, "Receiving tag=%d peer=%d size=%d", tag, peer, size);
  FLAGCXCHECKGOTO(bootstrapNetRecv(&sock, ((char*)data), size), ret, exit);
exit:
  FLAGCXCHECK(flagcxSocketClose(&sock));
  return ret;
}

// Collective algorithms, based on bootstrapSend/Recv, and sometimes bootstrapConnect/Accept

flagcxResult_t bootstrapRingAllGather(struct flagcxSocket* prevSocket, struct flagcxSocket* nextSocket, int rank, int nranks, char* data, int size) {
  /* Simple ring based AllGather
   * At each step i receive data from (rank-i-1) from prev
   * and send previous step's data from (rank-i) to next
   */
  for (int i=0; i<nranks-1; i++) {
    size_t rslice = (rank - i - 1 + nranks) % nranks;
    size_t sslice = (rank - i + nranks) % nranks;

    // Send slice to the right, recv slice from the left
    FLAGCXCHECK(bootstrapNetSendRecv(nextSocket, data+sslice*size, size, prevSocket, data+rslice*size, size));
  }
  return flagcxSuccess;
}

flagcxResult_t bootstrapAllGather(void* commState, void* allData, int size) {
  struct bootstrapState* state = (struct bootstrapState*)commState;
  int rank = state->rank;
  int nranks = state->nranks;

  TRACE(FLAGCX_INIT, "rank %d nranks %d size %d", rank, nranks, size);

  FLAGCXCHECK(bootstrapRingAllGather(&state->ringRecvSocket, &state->ringSendSocket, rank, nranks, (char*)allData, size));

  TRACE(FLAGCX_INIT, "rank %d nranks %d size %d - DONE", rank, nranks, size);
  return flagcxSuccess;
}

flagcxResult_t bootstrapIntraNodeBarrier(void* commState, int *ranks, int rank, int nranks, int tag) {
  if (nranks == 1) return flagcxSuccess;
  TRACE(FLAGCX_INIT, "rank %d nranks %d tag %x - ENTER", rank, nranks, tag);

  /* Simple [intra] process barrier
   *
   * Based on the dissemination algorithm by Debra Hensgen, Raphael Finkel, and Udi Manbet,
   * "Two Algorithms for Barrier Synchronization," International Journal of Parallel Programming, 17(1):1-17, 1988"
   */
  int data[1];
  for (int mask=1; mask<nranks; mask<<=1) {
    int src = (rank - mask + nranks) % nranks;
    int dst = (rank + mask) % nranks;
    FLAGCXCHECK(bootstrapSend(commState, ranks ? ranks[dst] : dst, tag, data, sizeof(data)));
    FLAGCXCHECK(bootstrapRecv(commState, ranks ? ranks[src] : src, tag, data, sizeof(data)));
  }

  TRACE(FLAGCX_INIT, "rank %d nranks %d tag %x - DONE", rank, nranks, tag);
  return flagcxSuccess;
}

flagcxResult_t bootstrapBarrier(void* commState, int rank, int nranks, int tag) {
  return bootstrapIntraNodeBarrier(commState, NULL, rank, nranks, tag);
}

// [IntraNode] in-place Broadcast
flagcxResult_t bootstrapIntraNodeBroadcast(void* commState, int *ranks, int rank, int nranks, int root, void* bcastData, int size) {
  if (nranks == 1) return flagcxSuccess;
  TRACE(FLAGCX_INIT, "rank %d nranks %d root %d size %d - ENTER", rank, nranks, root, size);

  if (rank == root) {
    for (int i=0; i<nranks; i++) {
      if (i != root) FLAGCXCHECK(bootstrapSend(commState, ranks ? ranks[i] : i, /*tag=*/ranks ? ranks[i] : i, bcastData, size));
    }
  }
  else {
    FLAGCXCHECK(bootstrapRecv(commState, ranks ? ranks[root] : root, /*tag=*/ranks ? ranks[rank] : rank, bcastData, size));
  }

  TRACE(FLAGCX_INIT, "rank %d nranks %d root %d size %d - DONE", rank, nranks, root, size);
  return flagcxSuccess;
}

flagcxResult_t bootstrapBroadcast(void* commState, int rank, int nranks, int root, void* bcastData, int size) {
  return bootstrapIntraNodeBroadcast(commState, NULL, rank, nranks, root, bcastData, size);
}

flagcxResult_t bootstrapClose(void* commState) {
  struct bootstrapState* state = (struct bootstrapState*)commState;
  if (state->unexpectedConnections != NULL) {
    unexpectedFree(state);
    if (__atomic_load_n(state->abortFlag, __ATOMIC_RELAXED) == 0) {
      WARN("Unexpected connections are not empty");
      return flagcxInternalError;
    }
  }

  FLAGCXCHECK(flagcxSocketClose(&state->listenSock));
  FLAGCXCHECK(flagcxSocketClose(&state->ringSendSocket));
  FLAGCXCHECK(flagcxSocketClose(&state->ringRecvSocket));

  free(state->peerCommAddresses);
  free(state);

  return flagcxSuccess;
}

flagcxResult_t bootstrapAbort(void* commState) {
  struct bootstrapState* state = (struct bootstrapState*)commState;
  if (commState == NULL) return flagcxSuccess;
  FLAGCXCHECK(flagcxSocketClose(&state->listenSock));
  FLAGCXCHECK(flagcxSocketClose(&state->ringSendSocket));
  FLAGCXCHECK(flagcxSocketClose(&state->ringRecvSocket));
  free(state->peerCommAddresses);
  free(state->peerProxyAddresses);
  free(state);
  return flagcxSuccess;
}
