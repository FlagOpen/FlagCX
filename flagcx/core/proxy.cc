/*************************************************************************
 * Copyright (c) 2016-2022, NVIDIA CORPORATION. All rights reserved.
 *
 * See LICENSE.txt for license information
 ************************************************************************/

#include "proxy.h"
#include "adaptor.h"
#include "collectives.h"
#include "comm.h"
#include "info.h"
#include "net.h"
#include "socket.h"
#include "transport.h"
#define ENABLE_TIMER 0
#include "timer.h"

#include <assert.h>
#include <string>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
using namespace std;

enum { proxyRecv = 0, proxySend = 1 };
extern union flagcxSocketAddress bootstrapNetIfAddr;

static bool proxyMatchOpType(int type) {
  switch (type) {
    case flagcxProxyMsgInit:
    case flagcxProxyMsgSharedInit:
    case flagcxProxyMsgSetup:
    case flagcxProxyMsgConnect:
    case flagcxProxyMsgGetFd:
    case flagcxProxyMsgRegister:
    case flagcxProxyMsgDeregister:
    case flagcxProxyMsgRegMr:
    case flagcxProxyMsgDeregMr:
    case flagcxProxyMsgSendRecv:
      return true;
    default:
      return false;
  }
}

FLAGCX_TEMPLETELIST_DEFINE(ProdProgChannel, struct flagcxProxyOps,
                           prodPrevChannel, prodNextChannel);
FLAGCX_TEMPLETELIST_DEFINE(ConsProgChannel, struct flagcxProxyOps,
                           consPrevChannel, consNextChannel);
FLAGCX_TEMPLETELIST_DEFINE(ProgPeer, struct flagcxProxyOps::consPeer, prevPeer,
                           nextPeer);

flagcxResult_t
flagcxProxyProgressChannelJoin(struct flagcxProxyState *proxyState,
                               struct flagcxProxyState *) {

  return flagcxSuccess;
}

static flagcxResult_t asyncProxyOpEnqueue(flagcxProxyAsyncOp **opHead,
                                          flagcxProxyAsyncOp *newOp) {
  flagcxProxyAsyncOp *list = *opHead;
  if (list == NULL)
    *opHead = newOp;
  else {
    while (list->next)
      list = list->next;
    list->next = newOp;
    newOp->prev = list;
  }
  return flagcxSuccess;
}

static flagcxResult_t asyncProxyOpDequeue(flagcxProxyAsyncOp **opHead,
                                          flagcxProxyAsyncOp *op) {
  if (*opHead == op)
    *opHead = op->next;
  if (op->next)
    op->next->prev = op->prev;
  if (op->prev)
    op->prev->next = op->next;
  if (op->reqSize)
    free(op->reqBuff);
  if (op->respSize)
    free(op->respBuff);
  free(op);
  return flagcxSuccess;
}

static flagcxResult_t SaveProxy(struct flagcxHeteroComm *comm,
                                struct flagcxChannel *channel, int type,
                                int peer, struct flagcxProxyOp *op,
                                int connIndex, bool *justInquire) {
  if (peer < 0)
    return flagcxSuccess;

  if (justInquire)
    *justInquire = true;
  else {
    struct flagcxProxyOps *proxyOps;
    struct flagcxIntruQueue<struct flagcxProxyOp, &flagcxProxyOp::next> *queue;

    proxyOps = &comm->proxyState->proxyOps[op->channelId];
    queue = type == proxySend ? &proxyOps->prodPeers.sendQueue
                              : &proxyOps->prodPeers.recvQueue;

    pthread_mutex_lock(&comm->proxyState->mutex);
    flagcxProdProgChannelListEnList(&comm->proxyState->prodProgChannelHead,
                                    proxyOps);
    flagcxIntruQueueEnqueue(queue, op);
    pthread_mutex_unlock(&comm->proxyState->mutex);
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxProxySaveOp(struct flagcxHeteroComm *comm,
                                 struct flagcxProxyOp *op, bool *justInquire) {
  struct flagcxChannel *channel = &comm->channels[op->channelId];
  if (justInquire)
    *justInquire = false;
  switch (op->pattern) {
    case flagcxPatternSend:
    case flagcxPatternRecv: {
      if (op->root == comm->rank)
        return flagcxSuccess;
      FLAGCXCHECK(
          SaveProxy(comm, channel,
                    op->pattern == flagcxPatternSend ? proxySend : proxyRecv,
                    op->root, op, 0, justInquire));
    } break;
  }
  return flagcxSuccess;
}

static void flagcxProgressQueEmptyCheck(struct flagcxProxyState *proxyState) {
  bool error = 0;
  if (!flagcxProdProgChannelListEmpty(proxyState->prodProgChannelHead) ||
      !flagcxConsProgChannelListEmpty(proxyState->consProgChannelHead)) {
    error = 1;
  }
  for (int i = 0; i < MAXCHANNELS; i++) {
    if (!flagcxProgPeerListEmpty(proxyState->proxyOps[i].consProgPeerHead))
      error = 1;
    for (int r = 0; r < proxyState->nRanks; r++) {
      if (!flagcxIntruQueueEmpty(
              &proxyState->proxyOps[i].consPeers[r].sendQueue) ||
          !flagcxIntruQueueEmpty(
              &proxyState->proxyOps[i].consPeers[r].recvQueue))
        error = 1;
    }
    if (!flagcxIntruQueueEmpty(&proxyState->proxyOps[i].prodPeers.sendQueue) ||
        !flagcxIntruQueueEmpty(&proxyState->proxyOps[i].prodPeers.recvQueue))
      error = 1;
  }
  if (error)
    INFO(FLAGCX_INIT, "progress queue is not empty");
}

inline void *flagcxProxyProgress(void *proxyState_) {
  struct flagcxProxyState *proxyState = (flagcxProxyState *)proxyState_;
  bool commplete = false;
  deviceAdaptor->setDevice(proxyState->cudaDev);

  int stop = 0;
  while (!stop || !commplete) {
    stop = proxyState->progressState.stop;
    commplete = true;
    if (!flagcxConsProgChannelListEmpty(proxyState->consProgChannelHead)) {
      struct flagcxProxyOps *proxyOps = proxyState->consProgChannelHead;
      do {
        struct flagcxProxyOps *next = proxyOps->consNextChannel;

        if (!flagcxProgPeerListEmpty(proxyOps->consProgPeerHead)) {
          struct flagcxProxyOps::consPeer *peer = proxyOps->consProgPeerHead;
          do {
            struct flagcxProxyOps::consPeer *next = peer->nextPeer;
            struct flagcxIntruQueue<struct flagcxProxyOp, &flagcxProxyOp::next>
                *queue;
            queue = &peer->sendQueue;
            if (!flagcxIntruQueueEmpty(queue)) {
              commplete = false;
              struct flagcxProxyOp *op = flagcxIntruQueueHead(queue);
              struct sendNetResources *resources =
                  (sendNetResources *)op->connection->transportResources;
              flagcxProxySend(resources, op->recvbuff, op->nbytes, &op->args);
              if (op->args.done) {
                flagcxIntruQueueDelete(queue, op);
                free(op);
              }
            }
            queue = &peer->recvQueue;
            if (!flagcxIntruQueueEmpty(queue)) {
              commplete = false;
              struct flagcxProxyOp *op = flagcxIntruQueueHead(queue);
              struct recvNetResources *resources =
                  (recvNetResources *)op->connection->transportResources;
              flagcxProxyRecv(resources, op->recvbuff, op->nbytes, &op->args);
              if (op->args.done) {
                flagcxIntruQueueDelete(queue, op);
                free(op);
              }
            }
            if (flagcxIntruQueueEmpty(&peer->sendQueue) &&
                flagcxIntruQueueEmpty(&peer->recvQueue)) {
              flagcxProgPeerListDeLete(&proxyOps->consProgPeerHead, peer);
            }
            peer = next;
          } while (peer != NULL);
        }
        if (flagcxProgPeerListEmpty(proxyOps->consProgPeerHead)) {
          flagcxConsProgChannelListDeLete(&proxyState->consProgChannelHead,
                                          proxyOps);
        }
        proxyOps = next;
      } while (proxyOps != NULL);
    }
    pthread_mutex_lock(&proxyState->mutex);

    while (!flagcxProdProgChannelListEmpty(proxyState->prodProgChannelHead)) {
      commplete = false;
      struct flagcxProxyOps *proxyOps =
          flagcxProdProgChannelListDeList(&proxyState->prodProgChannelHead);

      flagcxConsProgChannelListEnList(&proxyState->consProgChannelHead,
                                      proxyOps);
      struct flagcxIntruQueue<struct flagcxProxyOp, &flagcxProxyOp::next>
          *queue;
      queue = &proxyOps->prodPeers.sendQueue;
      while (!flagcxIntruQueueEmpty(queue)) {
        struct flagcxProxyOp *op = flagcxIntruQueueDequeue(queue);
        flagcxProgPeerListEnList(&proxyOps->consProgPeerHead,
                                 &proxyOps->consPeers[op->root]);
        flagcxIntruQueueEnqueue(&proxyOps->consPeers[op->root].sendQueue, op);
      }
      queue = &proxyOps->prodPeers.recvQueue;
      while (!flagcxIntruQueueEmpty(queue)) {
        struct flagcxProxyOp *op = flagcxIntruQueueDequeue(queue);
        flagcxProgPeerListEnList(&proxyOps->consProgPeerHead,
                                 &proxyOps->consPeers[op->root]);
        flagcxIntruQueueEnqueue(&proxyOps->consPeers[op->root].recvQueue, op);
      }
    }
    pthread_mutex_unlock(&proxyState->mutex);
  }

  flagcxProgressQueEmptyCheck(proxyState);

  return NULL;
}

static flagcxResult_t expectedProxyResponseStore(struct flagcxProxyState *state,
                                                 void *opId, void *respBuff,
                                                 int respSize,
                                                 flagcxResult_t res) {
  struct flagcxExpectedProxyResponse *elem = state->expectedResponses;
  while (elem) {
    if (elem->opId == opId) {
      if (respSize != elem->respSize) {
        WARN("Mismatched response size for opId=%p", opId);
        return flagcxInternalError;
      }

      if (elem->done) {
        WARN("Storing response for already completed opId=%p", opId);
        return flagcxInternalError;
      }

      memcpy(elem->respBuff, respBuff, respSize);
      free(respBuff);
      elem->done = true;
      elem->res = res;
      return flagcxSuccess;
    }
    elem = elem->next;
  }

  WARN("Proxy response for opId=%p doesn't match any expected response", opId);
  return flagcxInternalError;
}

static flagcxResult_t
expectedProxyResponseEnqueue(struct flagcxProxyState *state, void *opId,
                             int respSize) {
  struct flagcxExpectedProxyResponse *ex;
  FLAGCXCHECK(flagcxCalloc(&ex, 1));
  ex->opId = opId;

  // Pre-alloc response buffer
  ex->respBuff = malloc(respSize);
  ex->respSize = respSize;
  ex->res = flagcxInternalError;
  ex->done = false;

  // Enqueue
  struct flagcxExpectedProxyResponse *list = state->expectedResponses;
  if (list == NULL) {
    state->expectedResponses = ex;
    return flagcxSuccess;
  }
  while (list->next)
    list = list->next;
  list->next = ex;
  return flagcxSuccess;
}

static flagcxResult_t
expectedProxyResponseDequeue(struct flagcxProxyState *state, void *opId,
                             void *respBuff, int *found) {
  struct flagcxExpectedProxyResponse *elem = state->expectedResponses;
  struct flagcxExpectedProxyResponse *prev = NULL;
  *found = 0;
  while (elem) {
    if ((elem->opId == opId) && elem->done) {
      if (prev == NULL) {
        state->expectedResponses = elem->next;
      } else {
        prev->next = elem->next;
      }
      memcpy(respBuff, elem->respBuff, elem->respSize);
      flagcxResult_t res = elem->res;
      free(elem->respBuff);
      free(elem);
      *found = 1;
      return res;
    }
    prev = elem;
    elem = elem->next;
  }
  return flagcxSuccess;
}

static flagcxResult_t
expectedProxyResponseRemove(struct flagcxProxyState *state, void *opId) {
  struct flagcxExpectedProxyResponse *elem = state->expectedResponses;
  struct flagcxExpectedProxyResponse *prev = NULL;
  while (elem) {
    if (elem->opId == opId) {
      if (prev == NULL) {
        state->expectedResponses = elem->next;
      } else {
        prev->next = elem->next;
      }
      free(elem->respBuff);
      free(elem);
      return flagcxSuccess;
    }
    prev = elem;
    elem = elem->next;
  }
  WARN("Couldn't find opId=%p", opId);
  return flagcxInternalError;
}

flagcxResult_t flagcxPollProxyResponse(struct flagcxHeteroComm *comm,
                                       struct flagcxProxyConnector *proxyConn,
                                       void *respBuff, void *opId) {
  struct flagcxProxyState *sharedProxyState = comm->proxyState;
  // Check response queue
  int found = 0;
  flagcxResult_t res =
      expectedProxyResponseDequeue(sharedProxyState, opId, respBuff, &found);

  if (found == 0) {
    // Attempt to read in a new response header from the proxy thread
    struct flagcxSocket *sock = &sharedProxyState->peerSock;
    flagcxProxyRpcResponseHeader resp = {0};
    int offset = 0;
    if (flagcxSuccess != flagcxSocketProgress(FLAGCX_SOCKET_RECV, sock, &resp,
                                              sizeof(resp), &offset)) {
      WARN("Socket recv failed while polling for opId=%p", opId);
      return flagcxInternalError;
    }

    if (offset == 0) {
      return flagcxInProgress;
      // If we've returned a partial response, block to receive the rest of it
    } else if (offset < sizeof(resp)) {
      while (offset < sizeof(resp))
        FLAGCXCHECK(flagcxSocketProgress(FLAGCX_SOCKET_RECV, sock, &resp,
                                         sizeof(resp), &offset));
    }

    INFO(FLAGCX_PROXY, "flagcxPollProxyResponse Received new opId=%p",
         resp.opId);

    // If there's a respSize to recv
    if (resp.respSize > 0) {
      if (resp.opId != opId) {
        // Unexpected response, need to buffer the socket data
        respBuff = malloc(resp.respSize);
      }
      assert(respBuff != NULL);
      FLAGCXCHECK(flagcxSocketRecv(sock, respBuff, resp.respSize));
    }

    if (resp.opId == opId) {
      INFO(FLAGCX_PROXY, "resp.opId=%p matches expected opId=%p", resp.opId,
           opId);
      FLAGCXCHECK(expectedProxyResponseRemove(sharedProxyState, resp.opId));
      return resp.res;
    } else {
      INFO(FLAGCX_PROXY, "Queuing opId=%p respBuff=%p respSize=%d", resp.opId,
           respBuff, resp.respSize);
      // Store the result and mark response as completed
      FLAGCXCHECK(expectedProxyResponseStore(
          sharedProxyState, resp.opId, respBuff, resp.respSize, resp.res));
      return flagcxInProgress;
    }
  } else {
    INFO(FLAGCX_PROXY, "flagcxPollProxyResponse Dequeued cached opId=%p", opId);
  }

  return res;
}

static flagcxResult_t proxyProgressAsync(flagcxProxyAsyncOp **opHead,
                                         flagcxProxyAsyncOp *op) {
  int done = 0;
  // flagcxResult_t res = flagcxInternalError;
  if (op->type == flagcxProxyMsgConnect) {
    if (op->connection->send) {
      struct sendNetResources *resources =
          (struct sendNetResources *)op->connection->transportResources;
      if (!resources->netSendComm) {
        FLAGCXCHECK(flagcxNetIb.connect(resources->netDev, (void *)op->reqBuff,
                                        &resources->netSendComm, NULL));
      } else {
        FLAGCXCHECK(flagcxNetIb.regMr(
            resources->netSendComm, resources->buffers[0],
            resources->buffSizes[0], 2, &resources->mhandles[0]));
        done = 1;
      }
    } else {
      struct recvNetResources *resources =
          (struct recvNetResources *)op->connection->transportResources;
      if (!resources->netRecvComm) {
        FLAGCXCHECK(flagcxNetIb.accept(resources->netListenComm,
                                       &resources->netRecvComm, NULL));
      } else {
        FLAGCXCHECK(flagcxNetIb.regMr(
            resources->netRecvComm, resources->buffers[0],
            resources->buffSizes[0], 2, &resources->mhandles[0]));
        done = 1;
      }
    }
  } else
    return flagcxInternalError;

  if (done) {
    INFO(FLAGCX_PROXY,
         "proxyProgressAsync opId=%p op.type=%d op.reqBuff=%p op.respSize=%d "
         "done",
         op->opId, op->type, op->reqBuff, op->respSize);
    if (op->type == flagcxProxyMsgSetup)
      __atomic_store_n(&op->connection->state, connSetupDone, __ATOMIC_RELEASE);
    else if (op->type == flagcxProxyMsgConnect)
      __atomic_store_n(&op->connection->state, connConnected, __ATOMIC_RELEASE);

    /* if setup or connect is done, we should not return any error at this point
     * since flagcxSocketSend might already send the respBuff to the requester.
     * If we still choose to abort and close the connection, it can cause
     * segfault if the requester is using the respBuff. */

    flagcxProxyRpcResponseHeader resp = {op->opId, flagcxSuccess, op->respSize};

    // Send the opId for referencing async operation
    FLAGCXCHECK(flagcxSocketSend(op->connection->sock, &resp, sizeof(resp)));
    if (op->respSize) {
      // Send the response
      FLAGCXCHECK(
          flagcxSocketSend(op->connection->sock, op->respBuff, op->respSize));
    }

    asyncProxyOpDequeue(opHead, op);
    return flagcxSuccess;
  }

  return flagcxInProgress;
}

flagcxResult_t flagcxProxyCallAsync(struct flagcxHeteroComm *comm,
                                    struct flagcxProxyConnector *proxyConn,
                                    int type, void *reqBuff, int reqSize,
                                    int respSize, void *opId) {
  struct flagcxSocket *sock;
  flagcxResult_t ret = flagcxSuccess;
  struct flagcxProxyState *sharedProxyState = comm->proxyState;

  sock = &sharedProxyState->peerSock;
  if (sock == NULL)
    return flagcxInternalError;

  FLAGCXCHECKGOTO(flagcxSocketSend(sock, &type, sizeof(int)), ret, error);
  FLAGCXCHECKGOTO(
      flagcxSocketSend(sock, &proxyConn->connection, sizeof(void *)), ret,
      error);
  FLAGCXCHECKGOTO(flagcxSocketSend(sock, &reqSize, sizeof(int)), ret, error);
  FLAGCXCHECKGOTO(flagcxSocketSend(sock, &respSize, sizeof(int)), ret, error);
  if (reqSize)
    FLAGCXCHECKGOTO(flagcxSocketSend(sock, reqBuff, reqSize), ret, error);

  // Send opId to proxy
  FLAGCXCHECKGOTO(flagcxSocketSend(sock, &opId, sizeof(opId)), ret, error);

  FLAGCXCHECK(expectedProxyResponseEnqueue(sharedProxyState, opId, respSize));

  return flagcxSuccess;
error:
  return ret;
}

static flagcxResult_t proxyServiceInitOp(int type, struct flagcxSocket *sock,
                                         struct flagcxProxyAsyncOp **opHead,
                                         flagcxHeteroComm_t comm) {
  struct flagcxProxyAsyncOp *asyncOp;
  FLAGCXCHECK(flagcxCalloc(&asyncOp, 1));

  asyncOp->type = type;
  FLAGCXCHECK(flagcxSocketRecv(sock, &asyncOp->connection, sizeof(void *)));

  FLAGCXCHECK(flagcxSocketRecv(sock, &asyncOp->reqSize, sizeof(int)));
  FLAGCXCHECK(flagcxSocketRecv(sock, &asyncOp->respSize, sizeof(int)));
  if (asyncOp->reqSize) {
    FLAGCXCHECK(flagcxCalloc(&asyncOp->reqBuff, asyncOp->reqSize));
    FLAGCXCHECK(flagcxSocketRecv(sock, asyncOp->reqBuff, asyncOp->reqSize));
  }

  // Store opId for completion response
  FLAGCXCHECK(flagcxSocketRecv(sock, &asyncOp->opId, sizeof(asyncOp->opId)));

  asyncOp->connection->sock = sock;
  if (asyncOp->respSize)
    FLAGCXCHECK(flagcxCalloc(&asyncOp->respBuff, asyncOp->respSize));

  FLAGCXCHECK(asyncProxyOpEnqueue(opHead, asyncOp));

  FLAGCXCHECK(proxyProgressAsync(opHead, asyncOp));
  return flagcxSuccess;
}

flagcxResult_t flagcxProxyCallBlocking(struct flagcxHeteroComm *comm,
                                       struct flagcxProxyConnector *proxyConn,
                                       int type, void *reqBuff, int reqSize,
                                       void *respBuff, int respSize) {
  // Alloc some memory to act as a handle
  flagcxResult_t res = flagcxSuccess;
  void *opId = malloc(1);

  FLAGCXCHECKGOTO(flagcxProxyCallAsync(comm, proxyConn, type, reqBuff, reqSize,
                                       respSize, opId),
                  res, fail);

  do {
    res = flagcxPollProxyResponse(comm, proxyConn, respBuff, opId);
  } while (res == flagcxInProgress);

exit:
  free(opId);
  return res;
fail:
  goto exit;
}

flagcxResult_t flagcxProxyInit(struct flagcxHeteroComm *comm) {

  FLAGCXCHECK(flagcxSocketInit(&comm->proxyState->ipcSock, &bootstrapNetIfAddr,
                               comm->magic, flagcxSocketTypeProxy, NULL, 1));
  FLAGCXCHECK(flagcxSocketListen(&comm->proxyState->ipcSock));

  flagcxSocket *proxySock = &comm->proxyState->peerSock;
  FLAGCXCHECK(flagcxSocketInit(proxySock, &comm->proxyState->ipcSock.addr,
                               comm->magic, flagcxSocketTypeProxy));
  FLAGCXCHECK(flagcxSocketConnect(proxySock));

  char proxyMsg[10];
  memcpy(proxyMsg, (string("Proxy: ") + to_string(comm->rank)).c_str(), 10);
  flagcxSocketSend(proxySock, proxyMsg, 10);

  comm->proxyState->cudaDev = comm->cudaDev;
  pthread_create(&comm->proxyState->thread, NULL, flagcxProxyService,
                 (void *)comm);
  pthread_create(&comm->proxyState->progressState.thread, NULL,
                 flagcxProxyProgress, comm->proxyState);
  return flagcxSuccess;
}

void *flagcxProxyService(void *args) {
  struct flagcxHeteroComm *comm = (struct flagcxHeteroComm *)args;
  struct flagcxSocket sock;
  flagcxResult_t res;
  struct flagcxProxyAsyncOp *opHead = NULL;
  int stop = 0;

  // flagcxSetDevice(comm->cudaDev);
  deviceAdaptor->setDevice(comm->cudaDev);
  FLAGCXCHECKGOTO(flagcxSocketInit(&sock), res, out);
  FLAGCXCHECKGOTO(flagcxSocketAccept(&sock, &comm->proxyState->ipcSock), res,
                  out);

  // pthread_create(&comm->proxyState->progressState.thread, NULL,
  // flagcxProxyProgress, comm->proxyState);

  char proxyMsg[10];
  flagcxSocketRecv(&sock, proxyMsg, 10);
  INFO(FLAGCX_INIT, "proxy msg : \033[31m%s\033[0m", proxyMsg);

  while (!stop || (stop && opHead)) {
    int type, closed;
    res = flagcxSocketTryRecv(&sock, &type, sizeof(int), &closed,
                              false /*blocking*/);
    if (res != flagcxSuccess && res != flagcxInProgress) {
      WARN("[Service thread] Could not receive type from localRank %d, res=%u, "
           "closed=%d",
           comm->rank, res, closed);
    } else if (closed) {
      INFO(FLAGCX_NET, "[Service thread] Connection closed by localRank %d",
           comm->rank);
    } else if (res == flagcxSuccess) {
      if (type == flagcxProxyMsgStop) {
        stop = 1;
      } else if (proxyMatchOpType(type)) {
        res = proxyServiceInitOp(type, &sock, &opHead, comm);
      }
    }
    struct flagcxProxyAsyncOp *list;
    list = opHead;
    while (list) {
      struct flagcxProxyAsyncOp *opNext = list->next;
      FLAGCXCHECKGOTO(proxyProgressAsync(&opHead, list), res, out);
      list = opNext;
    }
  }
out:
  return NULL;
}

flagcxResult_t flagcxProxyFree(struct flagcxHeteroComm *comm) {
  for (int peer = 0; peer < comm->nRanks; peer++) {
    for (int c = 0; c < MAXCHANNELS; c++) {
      if (comm->channels[c].peers[peer]->recv[0].connected == 1) {
        struct flagcxConnector *conn = comm->channels[c].peers[peer]->recv;
        struct recvNetResources *resources =
            (struct recvNetResources *)
                conn->proxyConn.connection->transportResources;
        flagcxRecvProxyFree(resources);
      }
      if (comm->channels[c].peers[peer]->send[0].connected == 1) {
        struct flagcxConnector *conn = comm->channels[c].peers[peer]->send;
        struct sendNetResources *resources =
            (struct sendNetResources *)
                conn->proxyConn.connection->transportResources;
        flagcxSendProxyFree(resources);
      }
    }
  }
  return flagcxSuccess;
}

flagcxResult_t flagcxProxyDestroy(struct flagcxHeteroComm *comm) {
  int type = flagcxProxyMsgStop;
  flagcxSocketSend(&comm->proxyState->peerSock, &type, sizeof(int));
  comm->proxyState->progressState.stop = 1;
  pthread_join(comm->proxyState->thread, nullptr);
  pthread_join(comm->proxyState->progressState.thread, nullptr);
  flagcxProxyFree(comm);
  return flagcxSuccess;
}
